#include "live.h"

#define PANEL_BG 0x142338U
#define TEXT 0xdce8f5U
#define ACCENT 0x67d8b4U
static struct gfx_surface *screen;
static unsigned left, top, width, height, line;
static uint64_t last_switches, last_calls;
static unsigned history[32], history_at;
static char buffer[128];
static unsigned used;
static void reset(void) { used = 0; buffer[0] = 0; }
static void text(const char *s) {
    while (*s && used + 1 < sizeof(buffer)) buffer[used++] = *s++;
    buffer[used] = 0;
}
static void number(uint64_t value) {
    char digits[21]; unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n && used + 1 < sizeof(buffer)) buffer[used++] = digits[--n];
    buffer[used] = 0;
}
static void hex(uint64_t n) {
    text("0x");
    const char *digits = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) {
        if (used + 1 < sizeof(buffer)) buffer[used++] = digits[(n >> i) & 15];
    }
    buffer[used] = 0;
}
static void emit(uint32_t color) {
    unsigned y = top + 28 + line++ * 11;
    if (y + 9 > top + height) return;
    for (unsigned i = 0; i < used && 12 + (i + 1) * 6 < width; i++)
        gfx_glyph(screen, left + 12 + i * 6, y, buffer[i], color, PANEL_BG, 1);
    reset();
}
static void metric(const char *label, uint64_t n) { text(label); number(n); emit(TEXT); }
static void heading(const char *label) { text(label); emit(ACCENT); }
static void permissions(const struct vmm_mapping *m) {
    text((m->flags & PTE_USER) ? " U" : " K");
    text((m->flags & PTE_WRITABLE) ? " RW" : " RO");
    text((m->flags & PTE_NX) ? " NX" : " X");
    text(" "); number(m->page_size / 1024); text("K");
}
void live_panel_attach(struct gfx_surface *surface, unsigned x, unsigned y,
                       unsigned w, unsigned h) {
    screen = surface; left = x; top = y; width = w; height = h;
}
void live_panel_render(const struct live_snapshot *s) {
    if (!screen || !screen->pixels) return;
    /* Single rendering owner; interrupts stay enabled throughout graphics work. */
    gfx_rect(screen, left, top, width, height, PANEL_BG);
    const char *title = "LIVE KERNEL";
    for (unsigned i = 0; title[i]; i++)
        gfx_glyph(screen, left + 12 + i * 12, top + 7, title[i], ACCENT, PANEL_BG, 2);
    line = 0; reset();
    int compact = height < 550;
    text("Sample #"); number(s->sample); text(" @tick "); number(s->tick); emit(TEXT);
    heading("PHYSICAL MEMORY (allocator, KiB)");
    metric("Usable    ", s->memory.usable_pages * 4);
    metric("Allocated ", s->memory.allocated_pages * 4);
    metric("Free      ", s->memory.free_pages * 4);
    unsigned bar_width = width - 24;
    unsigned filled = s->memory.usable_pages ?
        s->memory.allocated_pages * (uint64_t)bar_width / s->memory.usable_pages : 0;
    unsigned y = top + 28 + line++ * 11;
    gfx_rect(screen, left + 12, y, bar_width, 5, 0x233e55);
    gfx_rect(screen, left + 12, y, filled, 5, ACCENT);
    uint64_t switches = s->scheduler.total_switches - last_switches;
    uint64_t calls = s->syscalls.total - last_calls;
    last_switches = s->scheduler.total_switches; last_calls = s->syscalls.total;
    text("Switches "); number(s->scheduler.total_switches); text(" (+"); number(switches); text(")"); emit(TEXT);
    text("Ticks "); number(s->tick); text("  Syscalls "); number(s->syscalls.total);
    text(" (+"); number(calls); text(")"); emit(TEXT);
    if (!compact) {
        text("Preempt "); number(s->scheduler.timer_preemptions);
        text(" Yield "); number(s->scheduler.voluntary_yields); emit(TEXT);
        text("Exit "); number(s->syscalls.exits); text(" Write "); number(s->syscalls.writes);
        text(" Getpid "); number(s->syscalls.getpids); emit(TEXT);
        text("Sys_yield "); number(s->syscalls.yields); text(" Unknown "); number(s->syscalls.unknown); emit(TEXT);
        /* Measured switches per snapshot, clipped at 20 for the chart only. */
        history[history_at++ % 32] = switches > 20 ? 20 : switches;
        text("Switch history: last 32 samples (0..20)"); emit(ACCENT);
        y = top + 28 + line * 11;
        for (unsigned i = 0; i < 32; i++) {
            unsigned h = history[(history_at + i) % 32];
            gfx_rect(screen, left + 12 + i * 8, y + 20 - h, 5, h, ACCENT);
        }
        line += 2;
    }
    text("Collector RUNNING TID "); number(s->current_tid); emit(TEXT);
    text("Observed TID "); number(s->observed.tid); text(" PID "); number(s->observed.pid);
    if (!compact) { text(" @"); number(s->observed.tick); }
    emit(TEXT);
    text("Active CR3 "); hex(s->root); emit(TEXT);
    if (s->user.valid) {
        text("Last user PID "); number(s->user.pid); text(" TID "); number(s->user.tid); emit(TEXT);
    } else { text("Last user: none observed"); emit(TEXT); }
    text("MAPPINGS PID "); number(s->map_pid); text(" (effective perms)"); emit(ACCENT);
    if (!compact) { text("Root "); hex(s->map_root); emit(TEXT); }
    unsigned map_count = compact ? 1 : LIVE_MAPS;
    for (unsigned i = 0; i < map_count; i++) {
        const struct live_map *m = &s->maps[i];
        text(m->label); text(" VA "); hex(m->address); emit(TEXT);
        if (m->present) { text("  PA "); hex(m->mapping.physical); permissions(&m->mapping); }
        else text("  Not present");
        emit(TEXT);
    }
    heading("THREADS: TID PID STATE NAME");
    unsigned limit = compact ? 2 : LIVE_THREADS;
    for (unsigned i = 0; i < s->thread_count && i < limit; i++) {
        const struct live_thread *t = &s->threads[i];
        number(t->tid); text(" "); number(t->pid); text(" ");
        text(thread_state_to_string(t->state)); text(" "); text(t->name); emit(TEXT);
    }
    if (!compact && s->threads_more) { text("... more threads (bounded view)"); emit(TEXT); }
    heading("PROCESSES: PID STATE NAME");
    limit = compact ? 1 : LIVE_PROCESSES;
    for (unsigned i = 0; i < s->process_count && i < limit; i++) {
        const struct live_process *p = &s->processes[i];
        number(p->pid); text(" "); text(process_state_to_string(p->state)); text(" "); text(p->name); emit(TEXT);
    }
    if (!compact && s->processes_more) { text("... more processes (bounded view)"); emit(TEXT); }
    if (!compact) metric("Terminal queue drops ", s->output_drops);
}
