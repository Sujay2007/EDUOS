#ifndef LIVE_H
#define LIVE_H
#include "pfa.h"
#include "thread.h"
#include "process.h"
#include "syscall.h"
#include "vmm.h"
#include "graphics.h"

#define LIVE_THREADS 8
#define LIVE_PROCESSES 6
#define LIVE_MAPS 4
struct live_context {
    uint64_t tick, root;
    uint32_t tid, pid;
    int valid, state;
};
struct live_thread { uint32_t tid, pid; int state; uint64_t runs; char name[32]; };
struct live_process { uint32_t pid; int state; uint64_t root; char name[32]; };
struct live_map { const char *label; uint64_t address; struct vmm_mapping mapping; int present; };
struct live_snapshot {
    uint64_t sample, tick, root, map_root, output_drops;
    uint32_t current_tid, current_pid, map_pid, thread_count, process_count;
    int threads_more, processes_more;
    struct pfa_stats memory;
    struct scheduler_stats scheduler;
    struct syscall_stats syscalls;
    struct live_context observed, user;
    struct live_thread threads[LIVE_THREADS];
    struct live_process processes[LIVE_PROCESSES];
    struct live_map maps[LIVE_MAPS];
};
/* Called at scheduler dispatch entry: copies only scalar fields, never renders. */
void live_observe_context(const struct thread *thread, uint64_t root, uint64_t tick);
/* Copies trusted objects under a short interrupt exclusion; no retained pointers. */
void live_collect(struct live_snapshot *out);
void live_start(void);
void live_dump(void);
int live_test_all(void);
int live_runtime_check(void);
/* Desktop hooks. Render called only by the normal live worker thread. */
void live_panel_attach(struct gfx_surface *surface, unsigned x, unsigned y,
                       unsigned width, unsigned height);
void live_panel_render(const struct live_snapshot *snapshot);
void gterminal_flush(void);
uint64_t gterminal_output_drops(void);
#endif
