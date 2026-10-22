#ifndef EDUCATION_MODEL_H
#define EDUCATION_MODEL_H
#include <stdint.h>
#define EDU_TASKS 4
#define EDU_TIMELINE 48
struct edu_task { unsigned arrival, burst, ran, first, finish, priority, blocked, blocked_ticks; };
struct edu_schedule { struct edu_task task[EDU_TASKS]; unsigned count, algorithm, time, current, slice, quantum; int timeline[EDU_TIMELINE]; };
void edu_schedule_init(struct edu_schedule *s, const unsigned *arrival, const unsigned *burst, unsigned count, unsigned algorithm);
int edu_schedule_step(struct edu_schedule *s);
int edu_schedule_block(struct edu_schedule *s, unsigned task, int blocked);
struct edu_block { unsigned start, size, used; };
struct edu_heap { struct edu_block blocks[16]; unsigned count; };
void edu_heap_init(struct edu_heap *h);
int edu_heap_alloc(struct edu_heap *h, unsigned bytes);
int edu_heap_free(struct edu_heap *h, unsigned index);
unsigned edu_page_index(uint64_t address, unsigned level);
int edu_canonical(uint64_t address);
#define EDU_PROCESSES 8
/* Slot 0 is a protected designated reaper. PIDs are never slot indices. */
enum edu_pstate { EDU_UNUSED, EDU_READY, EDU_RUNNING, EDU_BLOCKED, EDU_ZOMBIE };
struct edu_process { unsigned pid, ppid, state, waiting, adopted, pages, status; };
struct edu_tree { struct edu_process p[EDU_PROCESSES]; unsigned next_pid, last_status, reaped; };
void edu_tree_init(struct edu_tree *t);
int edu_process_create(struct edu_tree *t, unsigned parent);
int edu_process_exit(struct edu_tree *t, unsigned slot, unsigned status);
int edu_process_wait(struct edu_tree *t, unsigned parent);
int edu_process_reap(struct edu_tree *t, unsigned slot);
int edu_process_state(struct edu_tree *t, unsigned slot, unsigned state);
int edu_tree_valid(const struct edu_tree *t);
struct edu_frames { unsigned used, reserved, cursor; };
void edu_frames_init(struct edu_frames *f);
int edu_frame_alloc(struct edu_frames *f);
int edu_frame_free(struct edu_frames *f, unsigned index);
struct edu_pte { unsigned present, writable, frame; };
struct edu_vm { struct edu_pte entry[3]; };
void edu_vm_init(struct edu_vm *v);
uint64_t edu_vm_address(unsigned choice);
/* 0 success; -1 noncanonical; -2 missing entry; -3 write denied. */
int edu_vm_translate(const struct edu_vm *v, unsigned choice, int write, uint64_t *physical, unsigned *stop);
/* All objects here are bounded teaching data, never kernel allocator/process objects. */
#endif
