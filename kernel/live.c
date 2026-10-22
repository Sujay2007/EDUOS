#include "live.h"
#include "education.h"
#include "pit.h"
#include "serial.h"

extern void _start(void);
static uint32_t observer_tid;
static uint64_t next_sample;
static struct live_context observed, last_user;
static struct live_snapshot latest;
static int running;

static void copy_snapshot(struct live_snapshot *out, const struct live_snapshot *in) {
    /* Large structure assignment may emit libc memcpy in this freestanding build. */
    volatile uint8_t *dest = (volatile uint8_t *)out;
    const volatile uint8_t *src = (const volatile uint8_t *)in;
    for (size_t i = 0; i < sizeof(*out); i++) dest[i] = src[i];
}

void live_observe_context(const struct thread *t, uint64_t root, uint64_t tick) {
    if (!t || t->tid == observer_tid || t->state == THREAD_STATE_TERMINATED) return;
    observed.valid = 1;
    observed.tid = t->tid;
    observed.pid = t->process ? t->process->pid : 0;
    observed.state = t->state;
    observed.root = root;
    observed.tick = tick;
    if (t->process && t->process->pid) last_user = observed;
}
static void copy_name(char *out, const char *name) {
    for (unsigned i = 0; i < 31; i++) out[i] = name[i];
    out[31] = 0;
}
void live_collect(struct live_snapshot *s) {
    if (!s) return;
    uint64_t flags = interrupts_save_and_disable();
    /* Only live subsystem objects and four known addresses are followed.
     * No process or page-table pointer survives this critical section. */
    struct thread *current = thread_get_current();
    s->sample = ++next_sample;
    s->tick = timer_get_ticks();
    s->root = vmm_get_current_pml4();
    s->current_tid = current ? current->tid : 0;
    s->current_pid = current && current->process ? current->process->pid : 0;
    s->observed = observed;
    s->user = last_user; /* Historical scalars only; never walk this cached CR3. */
    pfa_get_stats(&s->memory);
    scheduler_get_stats(&s->scheduler);
    syscall_get_stats(&s->syscalls);
    s->output_drops = gterminal_output_drops();
    s->thread_count = 0;
    struct thread *t = thread_get_list_head();
    while (t && s->thread_count < LIVE_THREADS) {
        struct live_thread *out = &s->threads[s->thread_count++];
        out->tid = t->tid; out->pid = t->process ? t->process->pid : 0;
        out->state = t->state; out->runs = t->run_count;
        copy_name(out->name, t->name);
        t = t->next;
    }
    s->threads_more = t != 0;
    s->process_count = 0;
    s->map_root = vmm_get_kernel_pml4();
    s->map_pid = 0;
    uint64_t code = 0x400000, stack = 0x7ff000;
    struct process *p = process_get_list();
    while (p && s->process_count < LIVE_PROCESSES) {
        struct live_process *out = &s->processes[s->process_count++];
        out->pid = p->pid; out->state = p->state; out->root = p->pml4_phys;
        copy_name(out->name, p->name);
        if (!s->map_pid && p->pid && p->pml4_phys && p->state != PROCESS_STATE_TERMINATED) {
            s->map_pid = p->pid; s->map_root = p->pml4_phys;
            code = p->user_code_vaddr ? p->user_code_vaddr : code;
            stack = p->user_stack_vaddr ? p->user_stack_vaddr : stack;
        }
        p = p->next;
    }
    s->processes_more = p != 0;
    s->maps[0].label = "Kernel text";
    s->maps[0].address = (uint64_t)_start;
    s->maps[1].label = "Collector data (heap)";
    s->maps[1].address = (uint64_t)current;
    s->maps[2].label = "User code"; s->maps[2].address = code;
    s->maps[3].label = "User stack"; s->maps[3].address = stack;
    for (unsigned i = 0; i < LIVE_MAPS; i++)
        s->maps[i].present = vmm_query_mapping(s->map_root, s->maps[i].address, &s->maps[i].mapping) == 0;
    interrupts_restore(flags);
}
static void live_worker(void *arg) {
    (void)arg;
    static struct live_snapshot next;
    uint64_t deadline = 0;
    for (;;) {
        /* Both rendering paths have a single owner, in normal thread context. */
        gterminal_flush();
        education_render();
        uint64_t now = timer_get_ticks();
        if (now >= deadline) {
            live_collect(&next);
            if (!education_active()) live_panel_render(&next);
            uint64_t flags = interrupts_save_and_disable();
            copy_snapshot(&latest, &next);
            interrupts_restore(flags);
            deadline = now + 25; /* Four snapshots/second at the existing 100Hz PIT. */
        }
        thread_sleep(2); /* Buffered terminal remains responsive between samples. */
    }
}
void live_start(void) {
    if (running) return;
    uint64_t flags = interrupts_save_and_disable();
    struct thread *t = thread_create("live_view", live_worker, 0);
    if (t) { observer_tid = t->tid; running = 1; }
    interrupts_restore(flags);
    serial_write(t ? "[LIVE] Worker started: 25-tick snapshots; graphics deferred from IRQ/syscalls.\r\n" :
                     "[LIVE] Worker creation failed; serial remains available.\r\n");
}
static void field(const char *label, uint64_t n) {
    serial_write(label); serial_write_dec(n);
}
void live_dump(void) {
    struct live_snapshot s;
    uint64_t flags = interrupts_save_and_disable();
    copy_snapshot(&s, &latest);
    interrupts_restore(flags);
    if (!s.sample) { serial_write("LIVE: waiting for first displayed sample.\r\n"); return; }
    field("LIVE sample=", s.sample); field(" tick=", s.tick);
    field(" usable_pages=", s.memory.usable_pages); field(" allocated_pages=", s.memory.allocated_pages);
    field(" free_pages=", s.memory.free_pages); field(" switches=", s.scheduler.total_switches);
    field(" preemptions=", s.scheduler.timer_preemptions); field(" syscalls=", s.syscalls.total);
    field(" observer_tid=", s.current_tid); field(" observed_tid=", s.observed.tid);
    field(" last_user_pid=", s.user.pid); field(" drops=", s.output_drops); serial_write("\r\n");
    serial_write("LIVE active_cr3="); serial_write_hex64(s.root);
    field(" map_pid=", s.map_pid); serial_write(" map_cr3="); serial_write_hex64(s.map_root);
    field(" threads=", s.thread_count); field(" processes=", s.process_count); serial_write("\r\n");
    for (unsigned i = 0; i < LIVE_MAPS; i++) {
        serial_write("LIVE map "); serial_write(s.maps[i].label);
        serial_write(" va="); serial_write_hex64(s.maps[i].address);
        field(" present=", s.maps[i].present);
        serial_write(" pa="); serial_write_hex64(s.maps[i].mapping.physical);
        serial_write(" flags="); serial_write_hex64(s.maps[i].mapping.flags);
        field(" page_size=", s.maps[i].mapping.page_size); serial_write("\r\n");
    }
}

int live_test_all(void) {
    static struct live_snapshot before, during;
    int ok = 1;
    uint64_t flags = interrupts_save_and_disable();
    live_collect(&before);
    struct thread *current = thread_get_current();
    ok &= before.current_tid == current->tid && before.root == vmm_get_current_pml4();
    ok &= before.memory.usable_pages == before.memory.allocated_pages + before.memory.free_pages;
    ok &= before.syscalls.total > 0 && before.syscalls.total == before.syscalls.exits + before.syscalls.writes +
          before.syscalls.getpids + before.syscalls.yields + before.syscalls.unknown +
          before.syscalls.reads + before.syscalls.lists + before.syscalls.draws + before.syscalls.sleeps;
    uint64_t frame = pfa_alloc_page();
    live_collect(&during);
    ok &= frame && during.memory.free_pages + 1 == before.memory.free_pages &&
          during.memory.allocated_pages == before.memory.allocated_pages + 1;
    if (frame) pfa_free_page(frame);
    struct vmm_mapping mapping;
    ok &= vmm_query_mapping(before.root, (uint64_t)_start, &mapping) == 0 &&
          mapping.physical == vmm_virt_to_phys((uint64_t)_start, 0) && !(mapping.flags & PTE_USER);
    uint64_t root = vmm_create_address_space();
    frame = pfa_alloc_page();
    if (!root || !frame || vmm_map_page_pml4(root, 0x400000, frame, PTE_USER | PTE_WRITABLE)) ok = 0;
    else {
        ok &= vmm_query_mapping(root, 0x400123, &mapping) == 0 && mapping.physical == frame + 0x123 &&
              mapping.page_size == PAGE_SIZE && (mapping.flags & (PTE_USER | PTE_WRITABLE)) == (PTE_USER | PTE_WRITABLE);
        uint64_t *pml4 = vmm_phys_to_virt(root);
        uint64_t saved = pml4[0];
        pml4[0] = (saved & ~(PTE_USER | PTE_WRITABLE)) | PTE_NX;
        ok &= vmm_query_mapping(root, 0x400000, &mapping) == 0 &&
              !(mapping.flags & (PTE_USER | PTE_WRITABLE)) && (mapping.flags & PTE_NX);
        pml4[0] = saved;
        uint64_t *pdpt = vmm_phys_to_virt(saved & PTE_ADDR_MASK);
        uint64_t *pd = vmm_phys_to_virt(pdpt[0] & PTE_ADDR_MASK);
        saved = pd[2]; pd[2] = 0x200000 | PTE_PRESENT | PTE_HUGE | PTE_USER | PTE_WRITABLE;
        ok &= vmm_query_mapping(root, 0x400123, &mapping) == 0 &&
              mapping.page_size == 0x200000 && mapping.physical == 0x200123;
        pd[2] = saved;
        saved = pdpt[0]; pdpt[0] = 0x40000000 | PTE_PRESENT | PTE_HUGE | PTE_USER;
        ok &= vmm_query_mapping(root, 0x400123, &mapping) == 0 &&
              mapping.page_size == 0x40000000 && mapping.physical == 0x40400123;
        pdpt[0] = saved;
        ok &= vmm_query_mapping(root, 0x800000, &mapping) != 0 &&
              vmm_query_mapping(root, 0x800000000000ULL, &mapping) != 0;
        vmm_unmap_page_pml4(root, 0x400000);
    }
    if (frame) pfa_free_page(frame);
    if (root) vmm_destroy_address_space(root);
    live_collect(&during);
    ok &= during.memory.free_pages == before.memory.free_pages;
    interrupts_restore(flags);
    serial_write(ok ? "[LIVE TEST] PASS: allocator snapshots, current context, syscall totals, effective permissions, 4K/2M/1G mappings, cleanup.\r\n" :
                      "[LIVE TEST] FAIL: live state diagnostic mismatch.\r\n");
    return ok ? 0 : -1;
}

int live_runtime_check(void) {
    /* Focused, opt-in diagnostic: prove the displayed snapshot changes with
     * real allocations and a real isolated int80 workload, then clean it up. */
    static struct live_snapshot s;
    struct pfa_stats baseline, current;
    uint64_t flags = interrupts_save_and_disable();
    pfa_get_stats(&baseline);
    uint64_t page = pfa_alloc_page();
    interrupts_restore(flags);
    if (!page) return -1;
    thread_sleep(60);
    flags = interrupts_save_and_disable();
    copy_snapshot(&s, &latest); pfa_get_stats(&current);
    int ok = s.memory.free_pages == current.free_pages && current.free_pages + 1 == baseline.free_pages;
    pfa_free_page(page);
    interrupts_restore(flags);
    serial_write(ok ? "[LIVE CHECK] PASS displayed allocator change.\r\n" : "[LIVE CHECK] FAIL allocator refresh.\r\n");

    extern const uint8_t live_probe_start[], live_probe_end[], live_probe_stop[];
    flags = interrupts_save_and_disable();
    struct process *p = process_create("live_probe");
    if (!p) { interrupts_restore(flags); return -1; }
    if (process_map_user_code(p, 0x400000, live_probe_start, live_probe_end - live_probe_start) ||
        process_map_user_stack(p, 0x80000000, 1) || !process_spawn_thread(p, 0x400000, 0x80000000)) {
        process_destroy(p); interrupts_restore(flags); return -1;
    }
    struct syscall_stats calls;
    syscall_get_stats(&calls);
    interrupts_restore(flags);
    serial_write("[LIVE CHECK] Isolated probe active for 75 ticks.\r\n");
    thread_sleep(75);
    flags = interrupts_save_and_disable();
    copy_snapshot(&s, &latest); pfa_get_stats(&current);
    ok &= s.map_pid == p->pid && s.map_root == p->pml4_phys &&
          s.maps[2].present && s.maps[2].mapping.physical == p->user_code_phys &&
          (s.maps[2].mapping.flags & PTE_USER) && s.maps[3].present &&
          s.maps[3].mapping.physical == p->user_stack_phys &&
          s.user.valid && s.user.pid == p->pid && s.syscalls.total > calls.total &&
          s.memory.free_pages == current.free_pages;
    volatile uint8_t *stop = (volatile uint8_t *)vmm_phys_to_virt(p->user_code_phys) +
                            (live_probe_stop - live_probe_start);
    *stop = 1;
    interrupts_restore(flags);
    uint64_t deadline = timer_get_ticks() + 200;
    while (p->state != PROCESS_STATE_TERMINATED && timer_get_ticks() < deadline) thread_sleep(1);
    flags = interrupts_save_and_disable();
    ok &= p->state == PROCESS_STATE_TERMINATED && p->exit_code == 0;
    process_destroy(p); /* Existing lifecycle guard refuses to free a live process. */
    interrupts_restore(flags);
    serial_write(ok ? "[LIVE CHECK] PASS process mappings, user context, syscalls and displayed memory.\r\n" :
                      "[LIVE CHECK] FAIL process/syscall snapshot mismatch.\r\n");
    return ok ? 0 : -1;
}
