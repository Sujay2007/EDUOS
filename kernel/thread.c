#include "thread.h"
#include "heap.h"
#include "gdt.h"
#include "pit.h"
#include "serial.h"

static struct thread *thread_list_head = NULL;
static struct thread *current_thread = NULL;
static struct thread *idle_thread = NULL;
static struct thread *main_thread = NULL;
static struct thread *dead_threads_head = NULL;
static struct thread *input_waiting_thread = NULL;

static tid_t next_tid = 1;
static int scheduler_active = 0;
static struct scheduler_stats sched_stats = {0};

static void *kmemset(void *dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}

static void kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i + 1 < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

const char *thread_state_to_string(thread_state_t state) {
    switch (state) {
        case THREAD_STATE_READY:      return "READY";
        case THREAD_STATE_RUNNING:    return "RUNNING";
        case THREAD_STATE_BLOCKED:    return "BLOCKED";
        case THREAD_STATE_TERMINATED: return "TERMINATED";
        default:                      return "UNKNOWN";
    }
}

static void thread_entry_trampoline(void (*entry)(void *), void *arg) {
    interrupts_enable();

    if (entry != NULL) {
        entry(arg);
    }

    /* Safe thread exit on return */
    thread_exit();
}

static void idle_thread_fn(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void add_thread_to_list(struct thread *t) {
    uint64_t rflags = interrupts_save_and_disable();
    t->next = NULL;

    if (thread_list_head == NULL) {
        thread_list_head = t;
    } else {
        struct thread *curr = thread_list_head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = t;
    }

    interrupts_restore(rflags);
}

static void remove_thread_from_list(struct thread *t) {
    if (thread_list_head == NULL || t == NULL) {
        return;
    }

    if (thread_list_head == t) {
        thread_list_head = t->next;
        t->next = NULL;
        if (input_waiting_thread == t) {
            input_waiting_thread = NULL;
        }
        return;
    }

    struct thread *curr = thread_list_head;
    while (curr->next != NULL && curr->next != t) {
        curr = curr->next;
    }

    if (curr->next == t) {
        curr->next = t->next;
        t->next = NULL;
    }
    if (input_waiting_thread == t) {
        input_waiting_thread = NULL;
    }
}

static void reclaim_dead_threads(void) {
    struct thread *dead = dead_threads_head;
    dead_threads_head = NULL;

    while (dead != NULL) {
        struct thread *next = dead->next;
        if (dead->stack_base != NULL) {
            kfree(dead->stack_base);
        }
        kfree(dead);
        dead = next;
    }
}

int thread_init(void) {
    uint64_t rflags = interrupts_save_and_disable();

    /* 1. Wrap the active bootloader execution context into main_thread */
    main_thread = (struct thread *)kmalloc(sizeof(struct thread));
    if (main_thread == NULL) {
        interrupts_restore(rflags);
        return -1;
    }

    kmemset(main_thread, 0, sizeof(struct thread));
    main_thread->tid = next_tid++;
    kstrncpy(main_thread->name, "main", THREAD_NAME_MAX);
    main_thread->state = THREAD_STATE_RUNNING;
    main_thread->stack_base = NULL; /* Boot stack provided by bootloader */
    main_thread->stack_size = 0;
    main_thread->saved_frame = NULL;
    main_thread->time_slice_ticks = SCHEDULER_QUANTUM_TICKS;
    main_thread->sleep_deadline = 0;
    main_thread->run_count = 1;
    main_thread->total_ticks = 0;
    main_thread->next = NULL;

    current_thread = main_thread;
    thread_list_head = main_thread;

    /* 2. Create the system idle thread */
    idle_thread = thread_create("idle", idle_thread_fn, NULL);
    if (idle_thread == NULL) {
        interrupts_restore(rflags);
        return -2;
    }

    scheduler_active = 1;
    interrupts_restore(rflags);
    return 0;
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg) {
    if (entry == NULL) {
        return NULL;
    }

    struct thread *t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == NULL) {
        return NULL;
    }

    void *stack = kmalloc(THREAD_DEFAULT_STACK_SIZE);
    if (stack == NULL) {
        kfree(t);
        return NULL;
    }

    kmemset(t, 0, sizeof(struct thread));
    kstrncpy(t->name, (name != NULL) ? name : "unnamed", THREAD_NAME_MAX);

    uint64_t stack_top = (uint64_t)stack + THREAD_DEFAULT_STACK_SIZE;
    stack_top &= ~0xFULL; /* 16-byte align */

    /*
     * SysV AMD64 ABI requires (%rsp + 8) to be a multiple of 16 at function entry point.
     * We place a dummy return address pointing to thread_exit at (stack_top - 8)
     * so (frame->rsp + 8) == stack_top (16-byte aligned), and any accidental return exits cleanly.
     */
    *(uint64_t *)(stack_top - 8) = (uint64_t)thread_exit;

    /* Reserve room on stack for struct interrupt_frame */
    struct interrupt_frame *frame = (struct interrupt_frame *)(stack_top - 8 - sizeof(struct interrupt_frame));
    kmemset(frame, 0, sizeof(struct interrupt_frame));

    frame->ss = GDT_KERNEL_DATA_SEL;
    frame->rsp = stack_top - 8;
    frame->rflags = 0x202; /* IF = 1, reserved bit 1 = 1 */
    frame->cs = GDT_KERNEL_CODE_SEL;
    frame->rip = (uint64_t)thread_entry_trampoline;
    frame->rdi = (uint64_t)entry;
    frame->rsi = (uint64_t)arg;
    frame->error_code = 0;
    frame->vector = 0;

    t->tid = next_tid++;
    t->state = THREAD_STATE_READY;
    t->saved_frame = frame;
    t->stack_base = stack;
    t->stack_size = THREAD_DEFAULT_STACK_SIZE;
    t->time_slice_ticks = SCHEDULER_QUANTUM_TICKS;
    t->sleep_deadline = 0;
    t->run_count = 0;
    t->total_ticks = 0;

    add_thread_to_list(t);
    return t;
}

static struct thread *select_next_thread(void) {
    /*
     * Round-robin selection:
     * Search from current_thread->next to the end of list, then wrap to head.
     * Exclude idle_thread from regular round-robin search.
     */
    struct thread *start = (current_thread != NULL && current_thread->next != NULL)
                               ? current_thread->next
                               : thread_list_head;

    struct thread *curr = start;
    while (curr != NULL) {
        if (curr != idle_thread && curr->state == THREAD_STATE_READY) {
            return curr;
        }
        curr = curr->next;
    }

    curr = thread_list_head;
    while (curr != NULL && curr != start) {
        if (curr != idle_thread && curr->state == THREAD_STATE_READY) {
            return curr;
        }
        curr = curr->next;
    }

    /* If current thread is still runnable (not blocked/terminated), keep running it */
    if (current_thread != NULL && current_thread != idle_thread &&
        (current_thread->state == THREAD_STATE_RUNNING || current_thread->state == THREAD_STATE_READY)) {
        return current_thread;
    }

    /* Fallback to idle context when no regular thread is runnable */
    return idle_thread;
}

struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame) {
    if (!scheduler_active || current_thread == NULL) {
        return frame;
    }

    /* 1. Reclaim any threads terminated in previous context switches */
    reclaim_dead_threads();

    uint64_t vec = frame->vector;

    /* 2. Check sleeping threads deadline on Timer Interrupt (Vector 32) */
    if (vec == 32) {
        uint64_t now = timer_get_ticks();
        struct thread *t = thread_list_head;
        while (t != NULL) {
            if (t->state == THREAD_STATE_BLOCKED && t->sleep_deadline > 0) {
                if (now >= t->sleep_deadline) {
                    t->state = THREAD_STATE_READY;
                    t->sleep_deadline = 0;
                }
            }
            t = t->next;
        }

        current_thread->total_ticks++;
    }

    /*
     * 3. Immediate preemption of idle thread:
     * If the idle thread is currently running and any non-idle thread is READY
     * (e.g. woken by timer tick, keyboard IRQ1, or serial IRQ4), immediately
     * switch from idle_thread to that runnable thread.
     */
    if (current_thread == idle_thread) {
        struct thread *check = thread_list_head;
        int has_ready = 0;
        while (check != NULL) {
            if (check != idle_thread && check->state == THREAD_STATE_READY) {
                has_ready = 1;
                break;
            }
            check = check->next;
        }

        if (has_ready) {
            current_thread->saved_frame = frame;
            struct thread *next = select_next_thread();
            if (next != current_thread) {
                next->state = THREAD_STATE_RUNNING;
                next->run_count++;
                next->time_slice_ticks = SCHEDULER_QUANTUM_TICKS;
                sched_stats.total_switches++;
                if (vec == 32) {
                    sched_stats.timer_preemptions++;
                }
                current_thread = next;
                if (next->stack_base != NULL) {
                    gdt_set_kernel_stack((uint64_t)next->stack_base + next->stack_size);
                }
                return next->saved_frame;
            }
        }
        return frame;
    }

    /* 4. Timer Preemption for active non-idle threads (Vector 32) */
    if (vec == 32) {
        int preempt_needed = 0;
        if (current_thread->time_slice_ticks > 0) {
            current_thread->time_slice_ticks--;
        }
        if (current_thread->time_slice_ticks == 0 ||
            current_thread->state != THREAD_STATE_RUNNING) {
            preempt_needed = 1;
        }

        if (preempt_needed) {
            current_thread->time_slice_ticks = SCHEDULER_QUANTUM_TICKS;
            struct thread *next = select_next_thread();

            if (next != current_thread) {
                if (current_thread->state == THREAD_STATE_RUNNING) {
                    current_thread->state = THREAD_STATE_READY;
                }
                current_thread->saved_frame = frame;

                next->state = THREAD_STATE_RUNNING;
                next->run_count++;
                sched_stats.total_switches++;
                sched_stats.timer_preemptions++;

                current_thread = next;
                if (next->stack_base != NULL) {
                    gdt_set_kernel_stack((uint64_t)next->stack_base + next->stack_size);
                }
                return next->saved_frame;
            }
        }
        return frame;
    }

    /* 5. Software Yield / Sleep / Wait / Exit Interrupt (Vector 48) */
    if (vec == 48) {
        current_thread->saved_frame = frame;

        if (current_thread->state == THREAD_STATE_RUNNING) {
            current_thread->state = THREAD_STATE_READY;
        } else if (current_thread->state == THREAD_STATE_TERMINATED) {
            remove_thread_from_list(current_thread);
            current_thread->next = dead_threads_head;
            dead_threads_head = current_thread;
        }

        struct thread *next = select_next_thread();

        next->state = THREAD_STATE_RUNNING;
        next->run_count++;
        next->time_slice_ticks = SCHEDULER_QUANTUM_TICKS;
        sched_stats.total_switches++;
        sched_stats.voluntary_yields++;

        current_thread = next;
        if (next->stack_base != NULL) {
            gdt_set_kernel_stack((uint64_t)next->stack_base + next->stack_size);
        }
        return next->saved_frame;
    }

    return frame;
}

void thread_yield(void) {
    if (!scheduler_active) {
        return;
    }
    __asm__ volatile ("int $48" : : : "memory");
}

void thread_sleep(uint64_t ticks) {
    if (!scheduler_active || ticks == 0) {
        thread_yield();
        return;
    }

    uint64_t rflags = interrupts_save_and_disable();
    current_thread->sleep_deadline = timer_get_ticks() + ticks;
    current_thread->state = THREAD_STATE_BLOCKED;
    __asm__ volatile ("int $48" : : : "memory");
    interrupts_restore(rflags);
}

void thread_wake(struct thread *t) {
    if (t == NULL) {
        return;
    }
    uint64_t rflags = interrupts_save_and_disable();
    if (t->state == THREAD_STATE_BLOCKED) {
        t->state = THREAD_STATE_READY;
        t->sleep_deadline = 0;
    }
    interrupts_restore(rflags);
}

void thread_wait_input(void) {
    if (!scheduler_active) {
        __asm__ volatile ("sti; hlt");
        return;
    }

    input_waiting_thread = current_thread;
    current_thread->state = THREAD_STATE_BLOCKED;
    current_thread->sleep_deadline = 0;

    __asm__ volatile ("int $48" : : : "memory");
}

void thread_wake_input(void) {
    uint64_t rflags = interrupts_save_and_disable();
    if (input_waiting_thread != NULL) {
        if (input_waiting_thread->state == THREAD_STATE_BLOCKED) {
            input_waiting_thread->state = THREAD_STATE_READY;
        }
        input_waiting_thread = NULL;
    }
    interrupts_restore(rflags);
}

void thread_exit(void) {
    interrupts_disable();
    if (current_thread != NULL) {
        current_thread->state = THREAD_STATE_TERMINATED;
    }
    __asm__ volatile ("int $48" : : : "memory");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

struct thread *thread_get_current(void) {
    return current_thread;
}

void scheduler_get_stats(struct scheduler_stats *stats) {
    if (stats == NULL) {
        return;
    }

    uint64_t rflags = interrupts_save_and_disable();
    *stats = sched_stats;

    stats->active_threads = 0;
    stats->ready_threads = 0;
    stats->running_threads = 0;
    stats->blocked_threads = 0;
    stats->terminated_threads = 0;

    struct thread *curr = thread_list_head;
    while (curr != NULL) {
        stats->active_threads++;
        switch (curr->state) {
            case THREAD_STATE_READY:      stats->ready_threads++; break;
            case THREAD_STATE_RUNNING:    stats->running_threads++; break;
            case THREAD_STATE_BLOCKED:    stats->blocked_threads++; break;
            case THREAD_STATE_TERMINATED: stats->terminated_threads++; break;
        }
        curr = curr->next;
    }

    /* Also count dead threads queued for reclamation */
    struct thread *dead = dead_threads_head;
    while (dead != NULL) {
        stats->active_threads++;
        stats->terminated_threads++;
        dead = dead->next;
    }

    interrupts_restore(rflags);
}

int scheduler_is_active(void) {
    return scheduler_active;
}

struct thread *thread_get_list_head(void) {
    return thread_list_head;
}

void thread_print_table(void) {
    uint64_t rflags = interrupts_save_and_disable();

    serial_write("  TID  Name             State       Runs   Ticks  Deadline\r\n");
    serial_write("  --------------------------------------------------------\r\n");

    struct thread *t = thread_list_head;
    while (t != NULL) {
        serial_write("  [");
        if (t->tid < 10) {
            serial_putchar(' ');
        }
        serial_write_dec(t->tid);
        serial_write("] ");

        /* Print name with padding */
        serial_write(t->name);
        size_t len = 0;
        while (t->name[len]) len++;
        for (size_t i = len; i < 16; i++) {
            serial_putchar(' ');
        }

        /* Print state with padding */
        const char *st_str = thread_state_to_string(t->state);
        serial_write(st_str);
        len = 0;
        while (st_str[len]) len++;
        for (size_t i = len; i < 11; i++) {
            serial_putchar(' ');
        }

        /* Runs */
        serial_write_dec(t->run_count);
        serial_write("      ");

        /* Ticks */
        serial_write_dec(t->total_ticks);
        serial_write("     ");

        /* Deadline */
        if (t->state == THREAD_STATE_BLOCKED && t->sleep_deadline > 0) {
            serial_write_dec(t->sleep_deadline);
        } else {
            serial_write("-");
        }
        serial_write("\r\n");

        t = t->next;
    }

    interrupts_restore(rflags);
}
