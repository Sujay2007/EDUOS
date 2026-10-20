#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"

typedef uint32_t tid_t;

typedef enum {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

#define THREAD_NAME_MAX 32
#define THREAD_DEFAULT_STACK_SIZE 8192 /* 8 KiB stack */
#define SCHEDULER_QUANTUM_TICKS 5      /* 5 ticks @ 100 Hz = 50 ms time slice */

struct thread {
    tid_t tid;                      /* Unique monotonic thread ID */
    char name[THREAD_NAME_MAX];     /* Human-readable thread name */
    thread_state_t state;           /* Current thread lifecycle state */

    struct interrupt_frame *saved_frame; /* Saved execution context on stack */
    void *stack_base;               /* Base of allocated stack memory */
    size_t stack_size;              /* Size of allocated stack in bytes */

    /* Scheduling fields */
    uint32_t time_slice_ticks;      /* Remaining ticks in current quantum */
    uint64_t sleep_deadline;        /* Timer tick threshold for sleeping threads */

    /* Execution statistics */
    uint64_t run_count;             /* Number of times scheduled */
    uint64_t total_ticks;           /* Total timer ticks spent running */

    /* Linked list pointer */
    struct thread *next;            /* Next thread in global thread list */
};

struct scheduler_stats {
    uint64_t total_switches;       /* Total context switches performed */
    uint64_t timer_preemptions;    /* Preemptions driven by timer interrupts */
    uint64_t voluntary_yields;     /* Voluntary thread_yield / sleep calls */
    uint32_t active_threads;       /* Total live threads (all states) */
    uint32_t ready_threads;        /* Number of READY threads */
    uint32_t running_threads;      /* Number of RUNNING threads */
    uint32_t blocked_threads;      /* Number of BLOCKED threads */
    uint32_t terminated_threads;   /* Number of TERMINATED threads */
};

/*
 * Initialize the thread and scheduler subsystem.
 * Wraps the current boot execution context into a thread and creates the idle thread.
 * Returns 0 on success, non-zero on failure.
 */
int thread_init(void);

/*
 * Create a new kernel thread with its own stack and initial context.
 * Returns pointer to new thread, or NULL on failure.
 */
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);

/*
 * Voluntarily yield the CPU to another runnable thread.
 */
void thread_yield(void);

/*
 * Block the current thread for the specified number of timer ticks.
 */
void thread_sleep(uint64_t ticks);

/*
 * Wake up a BLOCKED thread, making it READY.
 */
void thread_wake(struct thread *t);

/*
 * Block the calling thread until keyboard or serial input arrives.
 */
void thread_wait_input(void);

/*
 * Wake the thread waiting for input (if any), transitioning it from BLOCKED to READY.
 */
void thread_wake_input(void);

/*
 * Terminate the calling thread and yield CPU.
 * Does not return.
 */
void thread_exit(void) __attribute__((noreturn));

/*
 * Get the currently running thread.
 */
struct thread *thread_get_current(void);

/*
 * Query scheduler execution metrics.
 */
void scheduler_get_stats(struct scheduler_stats *stats);

/*
 * Internal hook called by isr_dispatch() on every interrupt.
 * Returns the interrupt_frame to restore on iretq.
 */
struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame);

/*
 * Check if the scheduler has been initialized and is active.
 */
int scheduler_is_active(void);

/*
 * Convert thread state to human-readable string.
 */
const char *thread_state_to_string(thread_state_t state);

/*
 * Print formatted table of all threads to serial/terminal output.
 */
void thread_print_table(void);

/*
 * Get head of global thread list.
 */
struct thread *thread_get_list_head(void);

/*
 * Verify internal scheduler invariants:
 * - Exactly one thread is in THREAD_STATE_RUNNING state and matches current_thread.
 * - All threads in thread_list_head have valid states (not TERMINATED).
 * Returns 0 if all invariants hold, negative code otherwise.
 */
int scheduler_check_invariants(void);

/*
 * Get the total ticks spent running inside the idle thread.
 */
uint64_t scheduler_get_idle_ticks(void);

#endif /* THREAD_H */
