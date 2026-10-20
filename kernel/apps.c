#include "apps.h"
#include "process.h"
#include "thread.h"
#include "vmm.h"
#include "pfa.h"
#include "pit.h"
#include "serial.h"
#include "syscall.h"

#define DECLARE_APP(name) extern const uint8_t app_##name##_start[], app_##name##_end[]
DECLARE_APP(hello); DECLARE_APP(files); DECLARE_APP(view); DECLARE_APP(colors); DECLARE_APP(check);
struct application { const char *name, *description; const uint8_t *start, *end; int argument; };
static const struct application registry[] = {
    {"hello", "Ring 3 identity and system-call output", app_hello_start, app_hello_end, 0},
    {"files", "List persistent root files from user mode", app_files_start, app_files_end, 0},
    {"view", "Read a text file (default hello.txt, max 16 KiB)", app_view_start, app_view_end, 1},
    {"colors", "Draw the top-bar RGB/CMY canvas and sleep", app_colors_start, app_colors_end, 0},
    {"check", "User ABI checks: pointers, EOF and request bounds", app_check_start, app_check_end, 0}
};
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
void apps_list(void) {
    serial_write("Built-in isolated applications (run <name> [file]):\r\n");
    for (unsigned i = 0; i < sizeof(registry) / sizeof(registry[0]); i++) {
        serial_write("  "); serial_write(registry[i].name); serial_write(" - ");
        serial_write(registry[i].description); serial_write("\r\n");
    }
}
int app_run(const char *name, const char *argument, int *exit_status) {
    const struct application *app = 0;
    if (!name || !exit_status) return -1;
    for (unsigned i = 0; i < sizeof(registry) / sizeof(registry[0]); i++)
        if (same(name, registry[i].name)) app = &registry[i];
    if (!app || (argument && *argument && !app->argument)) return -1;
    if (!argument || !*argument) argument = "hello.txt";
    unsigned length = 0;
    while (argument[length] && length < 31) length++;
    if (argument[length]) return -1;
    /* Setup is atomic against the existing single-CPU scheduler; no other
     * thread can run this process before its user argument is installed. */
    uint64_t flags = interrupts_save_and_disable();
    struct process *p = process_create(app->name);
    if (!p) { interrupts_restore(flags); return -1; }
    if (process_map_user_code(p, 0x400000, app->start, app->end - app->start) ||
        process_map_user_stack(p, 0x80000000, 1)) {
        process_destroy(p); interrupts_restore(flags); return -1;
    }
    char *user_argument = (char *)vmm_phys_to_virt(p->user_stack_phys) + PAGE_SIZE - 64;
    for (unsigned i = 0; i <= length; i++) user_argument[i] = argument[i];
    struct thread *t = process_spawn_thread(p, 0x400000, 0x80000000 - 256);
    if (!t) { process_destroy(p); interrupts_restore(flags); return -1; }
    t->saved_frame->rdi = 0x80000000 - 64;
    pid_t pid = p->pid;
    interrupts_restore(flags);
    serial_write("[APP] launch "); serial_write(app->name); serial_write(" PID "); serial_write_dec(pid); serial_write("\r\n");
    uint64_t deadline = timer_get_ticks() + 1000;
    while (p->state != PROCESS_STATE_TERMINATED && timer_get_ticks() < deadline) thread_sleep(1);
    if (p->state != PROCESS_STATE_TERMINATED) {
        serial_write("[APP] timeout: process still live; retained for diagnostics.\r\n");
        return -1; /* Never free a runnable process or its address space. */
    }
    *exit_status = p->exit_code;
    serial_write("[APP] exit "); serial_write(app->name); serial_write(" PID "); serial_write_dec(pid);
    serial_write(" status ");
    if (*exit_status < 0) { serial_putchar('-'); serial_write_dec(-(int64_t)*exit_status); }
    else serial_write_dec(*exit_status);
    serial_write("\r\n");
    process_destroy(p);
    thread_sleep(1); /* Let the scheduler reclaim the deferred user kernel stack. */
    return 0;
}
void apps_command(const char *arguments) {
    char name[16], argument[32]; unsigned n = 0;
    while (*arguments == ' ' || *arguments == '\t') arguments++;
    while (*arguments && *arguments != ' ' && *arguments != '\t') {
        if (n + 1 >= sizeof(name)) goto usage;
        name[n++] = *arguments++;
    }
    name[n] = 0;
    if (!n) goto usage;
    while (*arguments == ' ' || *arguments == '\t') arguments++;
    n = 0;
    while (*arguments && *arguments != ' ' && *arguments != '\t') {
        if (n + 1 >= sizeof(argument)) goto usage;
        argument[n++] = *arguments++;
    }
    argument[n] = 0;
    while (*arguments == ' ' || *arguments == '\t') arguments++;
    if (*arguments) goto usage;
    int status;
    if (app_run(name, argument, &status))
        serial_write("Application launch failed (unknown name, arguments, resources or timeout). Type apps.\r\n");
    return;
usage:
    serial_write("Usage: run <name> [filename]; type apps for the fixed application list.\r\n");
}
int apps_test_all(void) {
    int status, failures = 0;
    serial_write("Application Integration Test Suite:\r\n");
    /* Warm the existing reusable kernel heap before checking physical leaks. */
    if (app_run("hello", "", &status) || status) return -1;
    struct pfa_stats before, after;
    struct syscall_stats calls_before, calls_after;
    pfa_get_stats(&before);
    syscall_get_stats(&calls_before);
    struct { const char *name, *arg; int expected; } tests[] = {
        {"hello", "", 0}, {"files", "", 0}, {"view", "hello.txt", 0},
        {"view", "empty.txt", 0}, {"view", "pattern.txt", 0},
        {"view", "missing.txt", 2}, {"view", "../hello.txt", 2},
        {"colors", "", 0}, {"check", "", 0}
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int ok = !app_run(tests[i].name, tests[i].arg, &status) && status == tests[i].expected;
        serial_write(ok ? "  [APP TEST] PASS " : "  [APP TEST] FAIL ");
        serial_write(tests[i].name); serial_write(" "); serial_write(tests[i].arg); serial_write("\r\n");
        if (!ok) failures++;
    }
    if (app_run("not-an-app", "", &status) == 0 || app_run("hello", "extra", &status) == 0) failures++;
    pfa_get_stats(&after);
    syscall_get_stats(&calls_after);
    int clean = before.free_pages == after.free_pages && scheduler_check_invariants() == 0;
    int abi = calls_after.reads > calls_before.reads && calls_after.lists > calls_before.lists &&
              calls_after.draws > calls_before.draws && calls_after.sleeps > calls_before.sleeps &&
              calls_after.total == calls_after.exits + calls_after.writes + calls_after.getpids +
              calls_after.yields + calls_after.unknown + calls_after.reads + calls_after.lists +
              calls_after.draws + calls_after.sleeps;
    serial_write(clean ? "  [APP TEST] PASS page reclamation and scheduler invariants.\r\n" :
                         "  [APP TEST] FAIL resource/scheduler check.\r\n");
    if (!clean) failures++;
    serial_write(abi ? "  [APP TEST] PASS real filesystem/canvas/sleep syscall accounting.\r\n" :
                       "  [APP TEST] FAIL syscall accounting.\r\n");
    if (!abi) failures++;
    serial_write(failures ? "Application tests FAILED.\r\n" : "Application tests PASSED.\r\n");
    return failures;
}
