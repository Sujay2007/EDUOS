# EDUOS Interactive Explorer — mechanism usage and learning guide

This guide describes the **current source**, including its simplifications and
presentation quirks. The experiments execute inside the real bootable kernel,
but their subjects are **teaching models**, not live processes, RAM, IRQs or disk
operations. Rendering those models really uses the kernel framebuffer. **Tab →
Tools** is the separate route to the real shell, embedded applications and
sampled live kernel inspector.

## Contents

- [Getting started and common controls](#getting-started-and-common-controls)
- [Quick reference](#quick-reference)
- [1. Processes](#1-processes)
- [2. CPU scheduling](#2-cpu-scheduling)
- [3. Physical memory](#3-physical-memory)
- [4. Virtual memory](#4-virtual-memory)
- [5. Heap allocation](#5-heap-allocation)
- [6. Interrupts](#6-interrupts)
- [7. System calls](#7-system-calls)
- [8. Filesystems](#8-filesystems)
- [9. Concurrency](#9-concurrency)
- [10. Keyboard input](#10-keyboard-input)
- [11. Framebuffer](#11-framebuffer)
- [12. OS architecture](#12-os-architecture)
- [13. Boot dependencies](#13-boot-dependencies)
- [14. CPU execution](#14-cpu-execution)
- [15. Failure investigation](#15-failure-investigation)
- [Glossary](#glossary)
- [Troubleshooting](#troubleshooting)
- [Source and verification scope](#source-and-verification-scope)

## Getting started and common controls

Use the graphical guest window and give it keyboard focus. From a fresh boot,
**Processes** is selected. On Home, **Down or Right moves forward by one concept**;
**Up or Left moves backward by one**, wrapping across all 15. This is linear
selection even when the cards appear in several columns: Down does not jump an
entire visual row. Enter or Space opens the highlighted name directly. Home
numbers in the quick reference are positions, **not numeric launch shortcuts**.

From inside another experiment, Esc returns to Home with that concept selected;
move until the required name is highlighted, then Enter. If an explanation is
open, the first Esc closes it and the next Esc reaches Home. Enter is not a
universal advance key inside experiments: use the experiment's actual controls.

| Key | Actual behavior |
| --- | --- |
| R | Reset the current experiment and its configuration. Use it before every example below. It works on the main experiment canvas, not while an optional view is open. |
| E | Open/close deeper algorithm, purpose, example, edge-case and alternative explanations. |
| I | Open/close source-verified real-kernel notes and references. E/I can also switch between these optional views. |
| ? | Open an optional reasoning question; 1/2/3 selects an answer. There are no scores or completion gates. On a US keyboard, ? is Shift+/. |
| Esc | Close an optional view without changing model data; otherwise return to Home and stop scheduling animation. |
| PgUp/PgDn | Scroll the canvas or explanation by half its viewport. Controls/context stay fixed. |
| Home/End | Jump to the top/bottom of the canvas or explanation. These keys do not select the Home menu. |
| Tab | Switch between Explorer and Tools. It also works while Tools help is paused. |

Letters are case-insensitive. Use one key after another; a sequence such as
`C, C, 3, X` means four keypresses, **without typing commas or pressing Enter**.
`Space × 4` means four separate presses. `+` and `=` both increase supported
numeric settings; `-` decreases them.

The lower **WHAT CHANGED / WHAT IT MEANS** panel describes the last action. Some
selection/movement actions do not replace that message. A stationary message is
not proof that the selection failed: inspect the canvas and its numbers too.
Controls are disabled while an optional view is open; close it with Esc first.

Most models retain data when you leave and reopen them. R restores the stated
initial state. The four supporting walkthroughs share one `tick_step`; resetting
any experiment clears it. Always R after opening one of those walkthroughs, even
if another topic was previously at a later stage. Inspection resets canvas scroll
to the top but retains experiment data. E/I/? pauses scheduling animation.

Explorer keys are routed from **PS/2 guest keyboard input**. Serial input stays
with the shell even while Explorer is visible. Typing `C` in the host serial
terminal will not create a model child. In Tools, commands need Enter and normal
shell typing/backspace applies. Foreground applications may delay UI input until
the shell returns to its input loop.

Build/launch commands, if needed, are those already in the project:

```sh
make -B
make run-graphics
# Existing complete regression entry point:
python3 tools/test_final.py
```

The documentation task itself does not require rebuilding or replacing the ISO.
`make run` is serial-only display mode; use `run-graphics` to operate Explorer.

## Quick reference

Every row opens through **Home → highlight the exact name → Enter**. From the
fresh Processes selection, position N takes Down × (N−1). Every experiment resets
with **R on its canvas**; common E/I/?/Esc/Tab/scroll controls also apply.

| Home position/name | Main controls | What to investigate | Reset |
| --- | --- | --- | --- |
| 1 Processes | 1..8 slot; C child; X exit; S run; B block; U wake; W parent wait; Z collect zombie | Ownership, exit status, orphan versus zombie, reaping | R |
| 2 CPU scheduling | 1..4 task; N add; +/- burst; A arrival; P priority; G policy; Q quantum; B/W block/wake; Space tick; X run/pause | Scheduling order and timing | R |
| 3 Physical memory | Left/Right frame; A allocate; F free | Bitmap allocation, reservations, exhaustion | R |
| 4 Virtual memory | 1..4 address; M/U map/unmap; P writable; W read/write; Space walk | Reachability, translation and protection | R |
| 5 Heap allocation | Left/Right block; +/- request; A allocate; F free | First fit, splitting, merging and fragmentation | R |
| 6 Interrupts | K inject; M mask; Space progress | Pending IRQ, saved return, handler, EOI, return | R |
| 7 System calls | 1/2/3 service; P pointer; C call; Space progress | Privilege boundary and validation | R |
| 8 Filesystems | 1/2/3 file; O offset; L length; D read | Lookup, bounded data and EOF | R |
| 9 Concurrency | 1/2 chosen task; L lock/order; D counter/deadlock; F recovery | Lost updates, mutual exclusion and cycles | R; L/D also restart their submodel |
| 10 Keyboard input | K enqueue A; Space consume | Ring buffering and overflow | R |
| 11 Framebuffer | Arrows cursor; G color; Space pixel; F rectangle | Coordinates, retained pixels and clipping | R |
| 12 OS architecture | Space forward; B back | Layered request route; supporting walkthrough | R |
| 13 Boot dependencies | Space forward; B back | Initialization prerequisites; supporting walkthrough | R |
| 14 CPU execution | Space forward; B back | Fixed register/stack illustration; supporting walkthrough | R |
| 15 Failure investigation | Space forward; B back | Evidence stages; supporting walkthrough | R |

## 1. Processes

### Purpose and exact controls

Open **Processes**. Learn to answer: **Has a child stopped executing, lost its
parent, or had its status collected?** These are distinct events. Real operating
systems must preserve resource ownership and termination information without
freeing an execution context that is still active.

R starts slot 1 = reaper PID 1, PPID 0, READY, one modeled page; slot 2 = PID 10,
PPID 1, RUNNING, three modeled pages. Slot 2 is selected. Total model pages = 4.
All other slots are unused.

| Key | Action and expected effect |
| --- | --- |
| 1..8 | Select a **slot**, not a PID. The context message confirms it. An unused row has no selected-process outline. |
| C | Create a READY child of the selected living parent in the first unused slot, with the next PID and three modeled pages. Selection stays on the parent, allowing siblings. |
| X | Exit selected process with fixed status 7, release its modeled pages, and retain a zombie status record unless a waiting parent collects it immediately. Living children are adopted by PID 1. |
| S | Make selected living process RUNNING; any previous RUNNING process becomes READY. |
| B / U | Force selected living process BLOCKED / READY. These manual overrides also clear its `waiting` flag. U does not immediately dispatch it. |
| W | As the selected parent: collect the first zombie child in slot order, or block waiting if only living children exist. No children means refusal. |
| Z | Select a zombie first. Its **parent** collects its status; the record disappears and selection moves to that parent. This is not the zombie executing wait. |
| R | Restore the two initial processes, next PID 11 and zero collection counters. |

### Worked experiment: two children, zombie, collection and adoption

After opening, follow these numbered actions. The model changes described are
performed by the current controller/model code, not by real process syscalls.

1. **R.** PID 10 is selected. `edu_tree_init` creates the two initial records;
   pages=4, zombies=0, collected=0.
2. **C.** Slot 3 appears as PID 11, PPID 10, READY, pages=3. `edu_process_create`
   fills a free record and increments `next_pid`; total pages becomes 7.
3. **C again.** Slot 4 becomes PID 12, also PPID 10. Total pages=10. Parent
   selection did not move, so these are siblings, not a grandchild.
4. **3, X.** Select PID 11 by its slot, then exit it. Its state is
   **ZOMBIE / terminated**, exit=7, pages=0. Total pages=7; zombies=1. Execution
   resources are released in the model, but its identity/status record remains.
5. **2, W.** PID 10 collects PID 11's status. Slot 3 is now unused; collected=1,
   last status=7, zombies=0, pages still 7. PID 10 remains RUNNING in this case:
   it did not previously block. Reaping removes a record, not another three pages.
6. **2, X.** PID 10 exits and becomes a zombie owned by reaper PID 1. PID 12 is
   still READY, now PPID 1, labeled **ADOPTED / living orphan**. Total pages=4;
   zombies=1. `edu_process_exit` reparents children without terminating them.
7. **4, S, B, U.** PID 12 runs, blocks, then becomes READY. These transitions
   change execution eligibility without changing its PID, PPID or pages.
8. **1, W.** The reaper collects PID 10's status. Slot 2 disappears; collected=2,
   zombies=0. PID 12 remains alive; W did not kill it.
9. **4, X, Z.** PID 12 exits, then its parent collects it. Only the reaper remains;
   pages=1, collected=3, zombies=0. Z selects the reaper after collection.

**Try wait-before-exit:** R, C, W blocks PID 10 with `waiting=1`. Then 3, X exits
the child; the exit path automatically calls collection, removes the child and
makes the parent READY. Selection remains on the now-unused child slot; press 2
to inspect the awakened parent. If you used U/S/B on the waiting parent first,
you canceled its wait flag, so later child exit instead leaves a zombie.

For a nested tree, R, C, 3, C creates PID 12 under PID 11. The UI is a lifecycle
controller; it does not require dispatching every process before applying an
operation and does not simulate instruction-level process creation.

### Internals, real kernel and limits

The model is `edu_tree` containing eight `edu_process` records, with PID, PPID,
state, waiting/adopted flags, page count and status. Collection clears one record;
PIDs increase even if a freed slot is reused. PID 1 cannot exit. Exit also adopts
already-zombie children, but only living adopted children get the living-orphan
label. A zombie is not executing; an orphan label does not mean dead.

Model code: [education.c](kernel/education.c), `process_action`, `process_canvas`,
`reset_model`; [education_model.c](kernel/education_model.c),
`edu_process_create`, `edu_process_exit`, `edu_process_wait`,
`edu_process_reap`, `edu_process_state`; structures in
[education_model.h](kernel/education_model.h).

Real code: [process.h](kernel/process.h), `struct process`;
[process.c](kernel/process.c), `process_create`, `process_exit`,
`process_destroy`; [apps.c](kernel/apps.c), `app_run`;
[thread.c](kernel/thread.c), deferred `reclaim_dead_threads`.
**The real kernel has no PPID/fork/exec/waitpid/adoption/Unix-zombie protocol.**
Its launcher waits with timed sleeps, reads exit status and explicitly destroys
the process. Real user pages are freed at destruction, unlike the model's
three-page release at exit. The one/three page counts are artificial accounting.

At most eight records, including zombies and reaper, fit. Reap zombies to free
slots, or R. Creating under an unused/zombie slot, reaping a living process,
re-exiting a zombie, or waiting without children is refused. The UI fixes exit
status to 7; it cannot enter another status or PID. There is no arbitrary live
process kill control. Close optional views before using these keys.

## 2. CPU scheduling

### Purpose and controls

Open **CPU scheduling**. Ask: **Which eligible task runs next, and what does
that choice do to delay and completion?** A real scheduler balances utilization,
fairness and responsiveness; this model makes its policy and assumptions visible.

R selects task 1 and restores RR, quantum 2, time 0 and these three tasks:

| Task | Arrival | CPU burst | Priority (1 highest) |
| --- | --- | --- | --- |
| 1 | 0 | 4 | 2 |
| 2 | 1 | 3 | 1 |
| 3 | 2 | 2 | 3 |

1..4 selects an existing task. N adds task 4 (arrival 0, burst 2, priority 2) and
selects it. +/- changes selected burst within 1..8; A cycles arrival 0..8 back
to 0; P cycles priority 1..4. G cycles **RR → priority → FCFS → SJF → RR** from
reset. Q cycles quantum 2→3→4→1→2. **Every configuration edit restarts time,
execution counts, completions and blocked state while retaining the edited
configuration.** R instead restores all defaults and removes task 4. There is
no individual task-delete key. Changing Q affects RR execution only, although
Q still restarts time under other policies.

Space executes exactly one model tick. X starts/pauses automatic ticks, at most
one every 20 real PIT ticks while visible. B blocks the selected arrived,
unfinished task; W wakes it. These do not restart the run. A task that has not
arrived or is complete cannot block/wake. No eligible task produces an idle `-`
cell, but time still advances if unfinished tasks exist.

### Worked comparison: the same four tasks under four policies

1. **R, N, Q.** Task 4 is added and quantum becomes 3. Time is again 0; task 4
   is selected. Original three task settings remain as above.
2. **Space × 11.** Run the four tasks to completion under RR. Each press records
   one CPU interval [time,time+1), increments one `ran`, and records first dispatch
   or finish when appropriate. The timeline is `1 1 1 2 2 2 3 3 4 4 1`.
3. Read the table below. Response is visible after first dispatch; finish/wait/
   turnaround remain pending until completion.
4. **G, Space × 11.** Priority is now selected, time was restarted, task count,
   parameters and quantum stayed the same. The high-priority task arriving at
   time 1 does **not** preempt task 1: this is nonpreemptive priority.
5. **G, Space × 11.** Compare FCFS using the same tasks.
6. **G, Space × 11.** Compare nonpreemptive SJF. It initially chooses task 4,
   the shortest arrived burst; task 3 has not yet arrived at time 0.
7. **G** returns to RR and resets time for another comparison. **X** animates it;
   press X again to pause. E/I/? or returning to Home pauses animation too.

| Policy | Eleven completed ticks | Finish times for tasks 1,2,3,4 |
| --- | --- | --- |
| RR, Q=3 | 1 1 1 2 2 2 3 3 4 4 1 | 11, 6, 8, 10 |
| Priority | 1 1 1 1 2 2 2 4 4 3 3 | 4, 7, 11, 9 |
| FCFS | 1 1 1 1 4 4 2 2 2 3 3 | 4, 9, 11, 6 |
| SJF | 4 4 3 3 2 2 2 1 1 1 1 | 11, 7, 4, 2 |

RR results, without blocked time:

| Task | First run | Finish | Turn = finish−arrival | Wait = turn−burst | Response = first−arrival |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 11 | 11 | 7 | 0 |
| 2 | 3 | 6 | 5 | 2 | 2 |
| 3 | 6 | 8 | 6 | 4 | 4 |
| 4 | 8 | 10 | 10 | 8 | 8 |

**Configure a different job:** R, N, 4, +, A, P gives task 4 burst 3, arrival 1,
priority 3 at time 0. It appears NEW until its arrival. These edits do not
retroactively alter previously executed ticks: they erase that run and restart.

**Inspect blocking:** R, B, Space blocks task 1 before tick 0; neither other task
has arrived yet, so tick 0 is idle and task 1 accumulates one blocked tick.
W, Space wakes it and runs it at time 1. Its first dispatch and response are 1,
its `Block` count is 1, and `Ran` is 1 at time 2. Waiting excludes blocked time:
`Wait = Finish − Arrival − Burst − Block`. Response still includes all delay
before first dispatch. Complete the run to obtain final waiting/turnaround.

### Internals, real kernel and troubleshooting

`edu_schedule` stores up to four `edu_task` records, current task, slice,
quantum, time and 48 timeline cells. FCFS picks lowest arrival; SJF compares
**original full bursts**, not a newly estimated next-I/O burst; priority picks
lowest numeric priority. Ties in the actual dispatch scan use task order.
These three policies keep a runnable current task until completion/blocking.
RR scans cyclically after the previous task when quantum expires or it cannot
continue. The ready strip is a derived eligible ordering, not a real kernel run
queue; the continuing RUNNING task is excluded. Tasks can be NEW, READY,
RUNNING, BLOCKED or DONE at a tick boundary. A final RR quantum boundary can
show READY even while that task owns the most recent completed timeline cell.

Model: `scheduling_action`, `scheduling_canvas`, `schedule_restart` in
[education.c](kernel/education.c); `edu_schedule_init`, `edu_schedule_step`,
`edu_schedule_block` and `ready` in [education_model.c](kernel/education_model.c).
Real: [thread.h](kernel/thread.h), `struct thread`, five-tick quantum;
[thread.c](kernel/thread.c), `select_next_thread`, `scheduler_on_interrupt`,
`thread_sleep`, `thread_wake`; [pit.c](kernel/pit.c), `pit_irq_handler`.
The real scheduler is round-robin at 100 Hz, not this selectable-policy model.

No context-switch cost, aging, priorities in the actual kernel, or timed I/O
completion is modeled. Blocked tasks need W; they do not wake automatically.
At 48 ticks or after all finish, extra Space cannot advance. Configuration edits
or R permit another run. X need not advance immediately; wait for the animation
interval. Tools hides/suspends its advancement, and return does not replay a
backlog. Do not compare runs with different arrivals/bursts unless intentional.

## 3. Physical memory

### Purpose and controls

Open **Physical memory**. Ask: **How can a compact bitmap prevent two owners
receiving the same frame, while keeping reserved memory unavailable?** Real
physical allocation supplies aligned backing frames for other subsystems.

The model has frames 0..15, reserved 0,1,15, and 13 usable frames. Left/Right
selects one frame and wraps; R initially selects frame 2. A allocates using a
remembered scan cursor and selects the returned frame. F frees the selected
allocated usable frame. There is no numeric frame-entry control.

### Worked experiment and bitmap calculation

1. **R.** Amber reserved frames have bits set, but other frames are free. `used`
   and `reserved` initially equal hexadecimal `0x8003`; cursor=0.
2. **A.** The scan skips reserved 0 and 1, chooses frame 2, sets bit 2 and moves
   cursor to 3. `used=0x8007`; 12 usable frames remain free. Frame 2 turns teal.
3. **A.** Frame 3 is chosen; `used=0x800f`, cursor=4, 11 usable frames free.
4. **Left, F.** Select frame 2, then clear its bit. `used=0x800b`; frame 3 stays
   allocated. Free lowers the cursor to 2, making that gap reusable.
5. **A.** Frame 2 is allocated again, not a second copy of frame 3.
6. **R, A × 13.** Frames 2..14 fill; all 16 bitmap bits are now set. The next A
   reports allocation failure. Reserved frames did not become usable.
7. **F, A.** The last allocated frame is freed and then reused. To test a
   reserved free, R, Left, F selects reserved frame 1 and refuses the operation.

The bitmap is drawn **frame 0 first**, left-to-right, so its displayed digit
order is least-significant bit first, unlike conventional hexadecimal notation.
A 1 means unavailable: it does not distinguish reserved from allocated by itself;
color and the separate `reserved` mask provide that distinction.

### Internals, kernel connection and limits

Model: `edu_frames {used,reserved,cursor}`, `edu_frame_alloc`, `edu_frame_free`
in [education_model.c](kernel/education_model.c), and `memory_action`/
`memory_canvas` in [education.c](kernel/education.c). Allocation probes at most
16 cyclic candidates; failure does not change bits. Free rejects reserved,
already-free and out-of-range indices.

Real: [pfa.c](kernel/pfa.c), `pfa_init`, `bitmap_test/set/clear`,
`pfa_alloc_page`, `pfa_free_page`, `pfa_get_stats`; [pfa.h](kernel/pfa.h).
The real allocator uses Limine's usable map and protects metadata, allocates
4 KiB physical pages and remembers a scan index. It returns a physical address
(or 0 for exhaustion), not this model's index/-1. The drawn grid is not actual
RAM, and it does not map a frame into a process. Reserved/duplicate frees are
errors rather than ways to manufacture capacity. If A fails, F an allocated
frame or R; use Tools `mem` for real allocator totals.

## 4. Virtual memory

### Purpose, addresses and controls

Open **Virtual memory**. Ask: **Why can an address fail even when RAM exists,
and why is presence different from permission?** A real address space maps
virtual pages to frames and enforces access rights.

R selects address 1, READ access, walk position 0. The model supplies these
choices; arbitrary address entry is not implemented:

| Key | Virtual address | PML4 / PDPT / PD / PT indices | Offset | Initial mapping |
| --- | --- | --- | --- | --- |
| 1 | 0x400123 | 0 / 0 / 2 / 0 | 0x123 | frame 9 (0x9000), writable |
| 2 | 0x600123 | 0 / 0 / 3 / 0 | 0x123 | missing PD entry; M maps frame 10 |
| 3 | 0x401abc | 0 / 0 / 2 / 1 | 0xabc | frame 11 (0xb000), read-only |
| 4 | 0x800000000000 | address-form failure | 0 | noncanonical; cannot map |

M installs the selected canonical mapping; U removes it; P toggles that entry's
writable flag; W toggles requested READ/WRITE access globally. Space progresses
the walk. Selection, M/U/P/W resets its position, but selecting another address
**does not reset READ/WRITE mode**. Mapping preserves the existing writable flag;
M alone does not make a previously read-only mapping writable. P can also change
the saved flag while the mapping is absent. R restores the complete initial set.

### Worked experiment: success, absence and protection

1. **R, Space × 4.** Initial highlight starts at PML4; subsequent positions are
   PDPT, PD, PT and final result. Translation succeeds to **0x00009123**:
   `frame 9 × 4096 + 0x123 = 0x9000 + 0x123`. Indices select entries; offset
   selects the unchanged byte position within the frame.
2. **2, Space × 2.** Translation stops at **PD[3], present=0**. The model's
   stop position is 2 (levels numbered 0..3). No physical byte is produced.
3. **M, Space × 4.** `entry[1].present` becomes 1 and the walk restarts. The
   formerly absent route reaches frame 10; the result is **0x0000a123**.
4. **U, Space × 2.** Presence becomes 0 again. Frame identity remains stored,
   but reachability is removed; it stops at PD[3]. Nothing is freed from real RAM.
5. **R, 3, W, Space × 3.** Request WRITE to the present read-only address.
   Stop at PT permissions: a present mapping is not sufficient for this write.
6. **P, Space × 4.** Leaf writable becomes 1. The write is now permitted by
   the model; physical result is **0x0000babc**. No actual byte is written.
7. **4, Space.** The model rejects address form before walking. Bit 47 is 1,
   but high bits do not sign-extend it; M/U/P on this choice are refused too.

An unmap of choice 1 or 3 stops at PT rather than PD. Repeated Space at a failure
cannot bypass it. All level cards remain drawn for explanation even when a walk
stops; they are not evidence that an absent lower table was actually visited.

### Internals, real kernel and limits

`edu_vm` contains just three `edu_pte` records (`present`, `writable`, `frame`),
not real page-table arrays. `edu_page_index` extracts nine bits at shifts
39,30,21,12; the offset is `address & 4095`. `edu_vm_translate` reports success
or noncanonical/missing/denied, a stop level and optional physical result.
Upper modeled levels permit user writes; leaf presence/write permission and
the special missing-PD case determine the result. `walk` is a UI progress counter:
Space recomputes the model outcome and reveals it when that counter reaches the
stop. It does not dereference the drawn table cards.

Model: [education_model.c](kernel/education_model.c), `edu_vm_init`,
`edu_vm_address`, `edu_vm_translate`, `edu_page_index`;
[education.c](kernel/education.c), `memory_action`, `memory_canvas`.
Real: [vmm.c](kernel/vmm.c), `vmm_map_page_pml4`, `vmm_unmap_page_pml4`,
`vmm_query_mapping`, `vmm_is_canonical`, `vmm_invlpg`,
`vmm_create_address_space`; [vmm.h](kernel/vmm.h).
Real queries walk a selected real root through HHDM, combine effective user/write/
NX permissions and recognize 4 KiB/2 MiB/1 GiB leaves. Process CR3 roots and real
TLB invalidation are not modified here.

The experiment has no typed addresses, NX switch, live CR3 edit, large-page
mapping, page allocation or swapping. If no final result appears, check present,
READ/WRITE mode, permissions and sufficient Space presses; R gives a known start.

## 5. Heap allocation

### Purpose and controls

Open **Heap allocation**. Ask: **Why can an allocation fail despite substantial
free bytes, and how can coalescing make it succeed?** Variable-sized objects need
subdivision and metadata beyond page allocation.

R gives one free extent [0,256), payload request 16 and block 1 selected.
+ or = increases the request by 16 to at most 96; - decreases it to at least 16.
A allocates using first fit and selects the returned block. Left/Right selects a
current block without wrapping. F frees that block, then merges adjacent free
extents. Block numbers can change after split/merge; inspect the table rather
than treating a number as a permanent object ID. There is no numeric block-entry
key or arbitrary byte-size input.

### Worked experiment: fragmentation, failure and coalescing

1. **R, + × 3.** Request is 64 payload bytes. No blocks have been allocated.
   Size changes affect future requests only.
2. **A.** First fit takes the initial gap. A 16-byte model header plus 64-byte
   payload makes an allocated extent of 80 at start 0; a free 176 remains at 80.
3. **A again.** Another allocated 80 appears at start 80; free extent 96 starts
   at 160. The table now contains allocated 80, allocated 80, free 96.
4. **A again.** The final 96-byte gap is allocated whole. Splitting would leave
   only 16 bytes, less than the model's minimum 32-byte remainder. Thus the
   third extent includes 16 extra slack bytes; there is no free block left.
5. **Left, Left, F.** Select block 1 and free its 80-byte extent. The middle
   and last blocks remain allocated, so no neighbors can merge.
6. **Right, Right, F.** Free block 3 at start 160. There are now free extents
   80 and 96 separated by an allocated 80. Total free extent bytes=176, largest=96.
7. **+, +, A.** Request becomes 96 payload bytes, requiring at least 112 with
   header. Allocation is refused: neither 80 nor 96 fits, despite total 176.
   This is **external fragmentation**; first fit cannot join separated gaps.
8. **Left, F.** Select/free the middle allocated block. The free-neighbor scan
   coalesces all three into one [0,256) extent. Selection clamps to block 1.
9. **A.** The 96-byte request now succeeds: allocated extent 112 at 0 and free
   extent 144 at 112. R restores the initial arena and request 16.

Amber marks allocated-block header space; teal marks allocated extents; pale
blocks are free. Numbers include header/slack, not just usable payload.

### Internals, kernel connection and limits

`edu_heap` holds at most 16 `edu_block {start,size,used}` entries. Requests align
up to 16. `edu_heap_alloc` scans from block zero, requires `payload+16`, and splits
only when at least another 32-byte extent remains and the array has capacity.
Otherwise it consumes the entire suitable extent. `edu_heap_free` marks one
allocated block free, scans adjacent free pairs and compacts the array when
merging. There is no moving or merging through an allocated block.

Model: [education_model.c](kernel/education_model.c), `edu_heap_init`,
`edu_heap_alloc`, `edu_heap_free`; [education.c](kernel/education.c),
`memory_action`, `memory_canvas`. Real: [heap.c](kernel/heap.c),
`struct heap_block`, `kmalloc`, `kfree`, `heap_expand`;
[heap.h](kernel/heap.h). Real heap blocks form a doubly linked list with 32-byte
headers and 16-byte alignment; expansion allocates/maps real pages. Real kfree
coalesces neighbors but normally retains mapped pages for reuse. The experiment
never calls kmalloc/kfree for its simulated block operations.

There is no heap expansion in this 256-byte model, arbitrary-address free,
realloc or undo. A failed A leaves selection/layout unchanged; F on a free block
is refused. Free another block or R if exhausted. Check the **payload request**
and header overhead before concluding that enough contiguous space exists.

## 6. Interrupts

### Purpose and controls

Open **Interrupts**. Ask: **What differs between a pending event, handler
execution, acknowledgement and return?** A real IRQ redirects control while
preserving enough execution state to resume; long interrupt work hurts latency.

R gives unmasked model delivery, pending=0, handled=0, phase 0 and saved RIP=0.
K queues a modeled timer event up to four pending events. M toggles the modeled
mask/IF indicator. Space progresses one active event. R clears all these fields.
Neither K nor M changes the hardware PIC, CPU IF or real PIT tick count.

### Worked experiment: masked arrival through return

1. **R, K.** Pending becomes 1; handled remains 0. Injection only increments a
   bounded counter: no handler has executed.
2. **M, Space.** Indicator becomes `CPU IF=0 / MASKED`; entry is delayed.
   Pending stays 1. Masking is not dropping this pending event.
3. **M, Space.** Unmask, then accept the event. Pending becomes 0, phase=1,
   `CPU frame saved`, saved return RIP becomes the fixed **0x4010** placeholder.
4. **Space.** Phase=2, `General registers saved`. This is a phase label, not
   a real register array being copied by this model.
5. **Space.** Phase=3, `Handler counted event`; handled increments to 1.
   The event is processed but EOI has not yet been modeled.
6. **Space.** Phase=4, `EOI acknowledged`. The saved return is still 0x4010.
7. **Space.** Phase returns to 0, `Executing application`. Context describes
   iretq restoration; saved RIP remains displayed, handled remains 1.
8. **K × 5.** Four pending events fit; the fifth is refused. Process one with
   Space × 5. If masked midway through an active handler, the model still allows
   that handler to finish; masking only blocks new entry at phase 0.

### Internals, real kernel and limits

`irq_mask`, `pending`, `irq_phase`, `irq_count` and `saved_rip` in
[education.c](kernel/education.c), `peripheral_action`/`peripheral_canvas`,
are the complete experiment state. There is no real saved stack frame, vector
selection, nested delivery, exception injection or rescheduling in this model.
The constant RIP is illustrative, not sampled application execution.

Real: [idt_stubs.S](kernel/idt_stubs.S), `isr_common_stub`;
[idt.c](kernel/idt.c), `isr_dispatch`; [idt.h](kernel/idt.h),
`interrupt_frame`; [gdt.c](kernel/gdt.c), `gdt_set_kernel_stack` and TSS setup;
[pic.c](kernel/pic.c), `pic_send_eoi`; [pit.c](kernel/pit.c),
`pit_irq_handler`; [thread.c](kernel/thread.c), `scheduler_on_interrupt`.
The actual PIT handler increments real ticks and acknowledges IRQ0; the dispatch
path then permits scheduling. That order is more concrete than the pedagogical
phase labels. User entry may use TSS rsp0; not every kernel-mode interrupt entails
a privilege-stack switch.

If Space does nothing useful, check mask and pending count. At phase 0 an empty
queue reports no event; K supplies one. Five Space presses process one accepted
IRQ fully. Use R rather than expecting an unmask to execute a whole handler.

## 7. System calls

### Purpose and controls

Open **System calls**. Ask: **What may the kernel do only after validating a
user request, and what does a rejected request return?** User mode needs
controlled services rather than arbitrary access to privileged operations.

R selects WRITE, a valid-pointer flag, no active call and zero counters/result.
1 selects WRITE, 2 READFILE, 3 GETPID. These are UI choices, not syscall numbers:
real SYS_WRITE=2, SYS_READFILE=5, SYS_GETPID=3. P toggles a Boolean bad-pointer
scenario, C starts a call, Space progresses it, and R resets. You cannot alter
service/pointer while a call is active; finish its return first.

### Worked experiment: rejection, valid copy and argument-free call

1. **R, P, C.** Mark buffer invalid and enter the modeled Ring 0 side. Phase=1
   (`validate pending`); RAX/result and copied bytes reset to 0. Context describes
   an int 0x80 request and privileged return frame.
2. **Space.** Validation rejects WRITE: phase jumps to 3 (`return pending`),
   RAX is **−2 / EFAULT**, copied bytes=0. It never passes through service phase 2.
3. **Space.** Return to USER / Ring 3; completed calls=1. The error result stays
   visible. Rejected calls are still completed calls after return.
4. **P, C.** Restore valid pointer, enter again. Copied bytes/result clear.
5. **Space.** Validation succeeds; phase=2, `service permitted`.
6. **Space.** Service produces fixed result 8 and copied-byte count 8; phase=3.
7. **Space.** Return to Ring 3; completed calls=2. No real eight-byte buffer
   exists here: the copy is represented by its scalar count.
8. **P, 3, C, Space, Space, Space.** Keep the invalid-pointer flag, but select
   GETPID. Validation does not reject this argument-free service; result is the
   fixed modeled PID **11**, copied bytes=0, completed calls=3. This is not the
   PID of the shell or a live process.

READFILE uses the same fixed successful count 8, not the Filesystems model's
selected file, its offset or real persistent storage. This experiment illustrates
service entry/validation/return; the two models are not a connected file stack.

### Internals, real kernel and limits

`call_kind`, `pointer_bad`, `call_phase`, signed `call_result`, `call_bytes` and
`call_count` are updated by `peripheral_action`; `peripheral_canvas` colors user/
kernel regions according to whether phase is zero. The invalid flag describes a
hypothetical unmapped crossing but does not contain a real address. Register and
stack preservation are explanatory text, not CPU state copied by this model.
C during an active call is refused; Space while idle asks you to start one.

Real: [syscall.c](kernel/syscall.c), `user_buffer_validate`,
`syscall_handler`, `copy_user_filename`; [syscall.h](kernel/syscall.h), ABI
numbers/errors; [idt.c](kernel/idt.c), user-callable vector 0x80;
[usermode_entry.S](kernel/usermode_entry.S), `user_enter_context`;
[apps_blobs.S](kernel/apps_blobs.S), embedded callers. Actual int 0x80 uses RAX
for number/result and RDI/RSI/RDX/R10 for arguments; pointer validation checks
bounds, overflow and required effective page permissions before copying.
The real kernel also offers exit, yield, directory entries, bounded canvas and
sleep calls. They are not selectable in this experiment.

If P or a numeric service key appears ineffective, finish an active call or R.
If GETPID succeeds despite the bad-pointer flag, that is intentional: it does not
use that buffer. No arbitrary syscall numbers, register editing or live fault
injection are available here; Tools applications exercise actual Ring 3 services.

## 8. Filesystems

### Purpose and exact data

Open **Filesystems**. Ask: **How do lookup, offset and destination capacity
bound a read, independently of disk size?** A filesystem separates names and
logical file extents from device sectors.

R selects text `hello.txt`, offset 0, capacity 8 and an empty result. Choices are
1 text, 2 `empty.txt`, 3 `missing.txt`. Selecting a filename clears offset and
previous returned bytes/buffer, but **retains current capacity**. O cycles offset
0→8→16→24→32→40→0. L cycles capacity 8→16→32→8. D performs the modeled read.
Changes to O/L do **not** refresh old returned bytes until D is pressed.

**Source/display discrepancy:** the directory card says `hello.txt / 31 bytes`,
but `file_data` is `Hello from a bounded file model!`, **32 bytes**, and the read
uses `sizeof(file_data)-1`. The examples follow the actual algorithm: offset 24
can return eight bytes, and offset 32 is EOF. This guide does not change the label.

### Worked experiment: chunks, bounds, EOF and lookup failure

1. **R, D.** Return 8 bytes, buffer `Hello fr`. The controller recognizes text
   choice 0 and copies `min(capacity, size-offset)` bytes, then adds a terminator
   outside the returned-byte count for display.
2. **O, D.** Offset 8; return 8 bytes `om a bou`. O alone would have left the
   previous displayed result unchanged.
3. **O, L, L, D.** Offset 16, capacity 32. Only 16 bytes remain, so return
   `nded file model!`, count=16, not 32. EOF caps the logical file read.
4. **O, O, D.** Offset 32; return 0 and an empty display buffer. This is normal
   EOF, not an access to a following file. One further O gives 40, also EOF.
5. **2, D.** Select empty file: offset resets to 0 but capacity stays 32.
   It exists, size=0, so count=0 with EOF explanation.
6. **3, D.** Missing filename: lookup-failed explanation, count=0, no buffer
   copy. The numeric zero count alone is insufficient to distinguish EOF from
   lookup failure; read the context explanation.
7. **R.** Restore text, offset 0, capacity 8 and no old result.

For the represented layout, sector=`extent_start + offset/512`, in-sector byte=
`offset%512`; at offset 8 in the text file this is sector 3, byte 8. That formula
is explanatory: this small model copies a static string, never reads a sector.

### Internals, kernel connection and limits

`file_sel`, `file_offset`, `file_length`, `file_bytes`, `file_buffer[33]` and
`file_data` live in [education.c](kernel/education.c). `peripheral_action`
represents lookup with fixed choice tests; it does not search a directory or call
`fs_lookup`. No model metadata can be corrupted from the controls. Negative,
arbitrary/overflowing offsets, zero capacity, capacity above 32, directory edits,
writes, permission checks and device errors cannot be entered. Offset past EOF
is supported but is not reported as an invalid-range error.

Real: [fs.c](kernel/fs.c), `fs_mount`, `fs_lookup`, `fs_list`, `fs_read`;
[fs.h](kernel/fs.h), `filesystem`, `fs_entry`;
[block.h](kernel/block.h), `block_device`; [block.c](kernel/block.c),
`block_read`; [ata.c](kernel/ata.c), `ata_read`, `ata_init`.
EDUFS validates on-disk metadata and supports a read-only flat root of at most 16
files. `fs_read` clamps to EOF and uses real sector reads, including partial-sector
copies. ATA uses bounded polling and 512-byte PIO/LBA28 sectors. These are separate
from the static teaching data. Tab → Tools `ls`, `cat hello.txt`, `fsinfo`, or
`run view hello.txt` accesses the mounted persistent filesystem when available;
its hello.txt text need not match this experiment's string.

If bytes do not match your new offset/capacity, press D. If the directory's 31
conflicts with a 32-byte result, use the implementation explanation above. R
clears stale results. Missing real disk support in a VM does not disable this
static model and is not diagnosed by successfully reading it.

## 9. Concurrency

### Purpose and controls

Open **Concurrency**. Ask: **Which interleaving loses an update, and which
resource dependencies prevent either task from continuing?** Concurrency can
create races even on a single preemptive CPU; simultaneous multiprocessor
execution is not necessary for an unsafe interleaving.

R restores counter mode, x=0, two local registers=0, phases=READ, no lock/order,
no resource owners and task 1 as the last selected actor. Press 1 or 2 to execute
that task's next operation; selection itself advances it. L toggles protection
and restarts state. D toggles counter/deadlock mode and restarts state **but keeps
the current lock/order setting**. Therefore use R before a prescribed scenario.
In deadlock mode, F releases the resources owned by the **last 1/2 actor** and
resets that actor's phase. There is no separate select-only task key.

### Worked counter experiments

1. **R, 1.** Task 1 reads shared 0 into `local[0]`; it now needs ADD. x remains 0.
2. **2.** Task 2 reads that same shared 0 into `local[1]`.
3. **1, 2.** Each increments only its own local register to 1. x is still 0;
   both now need STORE.
4. **1.** Task 1 stores 1 into x and becomes DONE.
5. **2.** Task 2 stores its independently computed 1, not 2. Final x=1: the
   second store overwrote the effect of the first increment.
6. **R, L.** Restart with critical-section locking enabled. Lock owner starts 0.
7. **1, 2.** Task 1 obtains the lock and reads 0. Task 2 cannot read while its
   peer owns the lock: it reports blocked and remains at READ.
8. **1, 1.** Task 1 increments, stores 1 and releases the lock.
9. **2, 2, 2.** Task 2 can now acquire/read 1, add to 2, store 2 and unlock.
   Both are DONE; x=2, owner=0.

The lock covers **read + add + store**, not merely the final assignment. A task
that is DONE cannot perform a second increment without reset. For comparison,
R then 1,1,1,2,2,2 also produces 2 without locking because you chose a serial
execution order; that one lucky order does not make the unprotected operation safe.

### Worked deadlock and recovery

1. **R, D.** Switch to the resource experiment without global ordering.
   Task 1 requests A then B; task 2 requests B then A. Both owners initially 0.
2. **1.** Task 1 acquires A; owner A=1. It retains A while planning B.
3. **2.** Task 2 acquires B; owner B=2. It retains B while planning A.
4. **1.** Task 1 requests held B; `wait_req[0]=1`. A red request edge appears.
5. **2.** Task 2 requests held A; `wait_req[1]=1`. Context reports DEADLOCK.
   Teal held edges and red request edges form A→T1→B→T2→A. Neither can acquire
   its second resource while the other retains its first.
6. **F.** The last actor is task 2. It releases B and resets its phase. A is
   still held by task 1. This deliberately forced release is modeled recovery,
   not a safe generic action real programs may apply without rollback.
7. **1, 1.** Task 1 obtains the now-free B, then on its next press completes
   and releases both A and B. Acquiring two resources does not release them in
   the same press; completion/release is another operation.
8. **2, 2, 2.** Task 2 acquires B, acquires A and then completes/releases both.
9. **R, D, L.** Restart with global A-before-B order. Press 1,2: task 1 holds A;
   task 2 waits for A without holding B. Press 1,1 to let task 1 acquire B and
   release both; 2,2,2 then completes task 2. Circular hold-and-wait never forms.

**Recovery display limitation:** F clears ownership and phase, but does not clear
`wait_req`. A task may still be labeled waiting, with an obsolete request edge,
after recovery until a subsequent successful acquisition clears that flag.
The owner numbers and the actions above determine whether resources are actually
available. R, or a D/L restart, clears the old wait flags. Do not interpret an old
waiting label alone as proof that the resource cycle persists.

### Internals, real kernel and limits

`shared`, `local[2]`, `phase[2]`, `owner[2]`, `wait_req[2]`, `race_mode`,
`lock_order` and `last_actor` in [education.c](kernel/education.c) implement both
experiments in `peripheral_action`; `peripheral_canvas` draws their state/graph.
Counter phases 0/1/2 are read/add/store, 3 done. Resource phases 0/1 acquire the
two resources, a further press completes with phase 3. No scheduler or real lock
is used for these operations: the learner chooses the interleaving directly.

Real: [idt.h](kernel/idt.h), `interrupts_save_and_disable`/
`interrupts_restore`; queue protection in [keyboard.c](kernel/keyboard.c) and
[gterminal.c](kernel/gterminal.c); bounded snapshots in
[live.c](kernel/live.c), `live_collect`. The single-CPU kernel masks interrupts
for short shared updates; it has no general mutex/semaphore/condition-variable
API. Local interrupt masking is not a multiprocessor lock. The resource model
illustrates mutual exclusion, hold-and-wait, lack of forced ordinary resource
preemption and circular wait; it does not implement deadlock detection in the
actual kernel. No real task can be damaged by this model's forced recovery.

If results differ, R first and count each 1/2 press. L/D erase partial work and
toggle settings; they are not pause keys. No automatic task execution occurs.

## 10. Keyboard input

### Purpose and controls

Open **Keyboard input**. Ask: **How does a finite queue separate event arrival
from processing, and what happens if arrivals outrun the consumer?** Device
handlers should record input promptly rather than perform long rendering work.

R empties the eight-slot model ring and zeros head, tail, drop count and the
last-consumed indicator. K injects one hardcoded capital A event; Space consumes
one. The model does not accept arbitrary typed characters as event payloads.

### Worked ring-buffer experiment

1. **R, K × 8.** All eight boxes contain A. Count=8; head has wrapped from 0 to
   0; tail=0. The explicit count distinguishes full from empty even with equal
   head/tail. No event has been consumed yet.
2. **K again.** The queue stays full, head/tail unchanged, dropped=1. Refusing
   the extra event prevents overwriting unread entries or growing memory.
3. **Space.** Consume slot 0: count=7, tail=1, head=0; last consumed=A. The
   freed box becomes empty, while the remaining boxes retain their As.
4. **K.** Insert at slot 0: count=8, head=1, tail=1, dropped remains 1.
5. **Space × 8.** All events are consumed. Count=0, tail=1, head=1; last
   consumed still shows A because that display is historical, not occupancy.
6. **Space.** Empty-queue explanation appears; no underflow occurs.
7. **R.** Clear the history indicator and dropped count as well as the queue.

### Internals, real kernel and limits

`key_ring[8]`, `key_head`, `key_tail`, `key_count`, `key_drops`, `last_key`
are updated in [education.c](kernel/education.c), `peripheral_action`.
Successful enqueue advances head modulo 8; successful dequeue advances tail
modulo 8. The canvas derives occupied boxes by walking `key_count` entries from
tail. It does not receive actual A scan codes when K is pressed: it assigns `'A'`.
The scancode 0x1e is explanatory; on the real unshifted keyboard it corresponds
to lowercase a, with Shift/Caps influencing capitalization.

Real: [keyboard.c](kernel/keyboard.c), `keyboard_isr`, `push_key`,
`keyboard_drain_serial`, `keyboard_getchar`, `keyboard_getchar_nonblock`;
[keyboard.h](kernel/keyboard.h), buffer size 128;
[thread.c](kernel/thread.c), `thread_wait_input`, `thread_wake_input`;
[shell.c](kernel/shell.c), `shell_run`. The real ring reserves one slot, giving
127 queued entries, and tags serial origin; the eight-slot teaching ring instead
uses a separate count. IRQ1, port 0x60, modifiers, extended make/break handling
and COM1 decoding are actual input infrastructure, not executed by this model K.

If K seems inert, check the context for FULL or close an optional view. Space
only consumes; it is not an inject key. A working model queue is not a test of
USB, mouse-wheel, arbitrary scancodes, key repeat or real input-loss accounting.
Tab reaches real keyboard-operated Tools; R retries the model safely.

## 11. Framebuffer

### Purpose and controls

Open **Framebuffer**. Ask: **Which logical pixels change when a rectangle
crosses an image edge, and how does stored image state enable redraw?** Real
framebuffer writes need correct coordinates, stride, formats and clipping.

R clears a **12 columns × 8 rows** grid, sets cursor (0,0), and color index 1.
Arrows move one logical pixel and clamp at x=0..11, y=0..7. G cycles selectable
colors 1 teal → 2 gold → 3 red → 4 blue → 1. Space paints one pixel; F paints a
3-wide, 2-high rectangle starting at the cursor and clipped to grid bounds.
R is the only exposed full clear; background index 0 is not selectable with G.
These arrows are cursor controls, not concept navigation or lesson scrolling.

### Worked pixel/rectangle experiment

1. **R, Space.** Cell (0,0) becomes teal (index 1). One element of
   `pixels[8][12]` changed. The black top/left cursor marker remains at (0,0).
2. **Right, Down, G.** Cursor becomes (1,1), color becomes gold (2). The
   previously painted pixel remains teal; cursor movement does not paint.
3. **F.** Cells x=1..3, y=1..2 become gold: six logical pixels. The rectangle
   does not clear older pixels outside its area.
4. **Right × 10, Down × 6.** Cursor reaches bottom-right (11,7). Extra Right
   or Down cannot move it outside the grid.
5. **F.** Only (11,7) is painted: the other requested columns/row lie outside
   the image and the loops clip them. No neighboring UI data is part of the grid.
6. **R.** All cells return to pale background index 0; cursor/color return to
   (0,0)/1. This is reset, not an undo stack.

### Internals, real kernel and limits

Model: `pixels[8][12]`, `pixel_x/y`, `pixel_color`, `palette`, and
`peripheral_action`/`peripheral_canvas` in [education.c](kernel/education.c).
Movement changes bounded indices; drawing assigns color indices in bounded
nested loops. The canvas magnifies cells, with the cell scale determined from
available width and capped at 38 pixels. Empty grid cells are model pixels too,
not uninitialized physical framebuffer memory.

Real: [graphics.c](kernel/graphics.c), `gfx_surface_init`, `gfx_pixel`,
`gfx_rect`, `encode`; [graphics.h](kernel/graphics.h), `gfx_surface`;
[font.c](kernel/font.c), `gfx_glyph`; [gterminal.c](kernel/gterminal.c),
`gterminal_flush`; [text_buffer.c](kernel/text_buffer.c), retained terminal
state; [live.c](kernel/live.c), graphics worker ownership. For an actual pixel,
row address advances by **pitch**, then x by bytes-per-pixel, and RGB channels
are encoded using validated masks. The model grid is rendered by these real
primitives but does not let the learner edit raw VRAM or change resolution/pitch.

There is no arbitrary RGB entry, line drawing control, mouse cursor, per-pixel
eraser or GPU/window-manager interface here. The palette has four paint colors.
If F looks like a single pixel at the corner, clipping is working. If controls
have scrolled off the drawing area visually, PgUp/Home restores the canvas;
fixed control/context panels remain available. Movement may leave the previous
action explanation unchanged, so use Cursor X/Y and the marker as evidence.

## 12. OS architecture

### Purpose and controls

Open **OS architecture**, then R. This is a **supporting walkthrough**, not a
transaction simulator. Ask: **Which layer turns a user request into a protected
kernel service and ultimately device work?** Separating responsibilities makes
hardware manageable and constrains untrusted applications.

Space advances one of five highlighted stages, B moves back, R highlights the
first stage. Common E/I/? and navigation controls work. There is no request-entry,
file-choice, packet, return-data, error-injection or live execution control.

### Complete walkthrough

1. **R:** `Application request` is highlighted; `tick_step=0`. Interpret this
   as a user program asking for service, not as an actual application launch.
2. **Space:** `Trust boundary`, `tick_step=1`. The explanation connects entry
   to validation before privileged work; no pointer is checked by this step.
3. **Space:** `Kernel service`, `tick_step=2`. This illustrates dispatch to
   the selected kernel operation, without executing one.
4. **Space:** `Filesystem abstraction`, `tick_step=3`. A name/offset operation
   can be resolved without the application knowing hardware port details.
5. **Space:** `Device access`, `tick_step=4`. A driver would issue device-level
   operations. There is no modeled read buffer or return journey in this trace.
6. **B:** return to Filesystem abstraction; **R:** return to Application request.
   Extra Space at the last stage cannot advance; extra B at the first cannot
   decrement. These operations change only the shared stage and action message.

### Source connection and limits

The stage strings and highlighting are in [education.c](kernel/education.c),
`peripheral_canvas`; `peripheral_action` updates `tick_step`. Optional content
comes from [lessons.c](kernel/lessons.c), Foundations. Do not mistake the
highlight for a request moving through real subsystem queues.

A real example is an embedded Ring 3 file viewer:
[apps_blobs.S](kernel/apps_blobs.S) calls the ABI;
[syscall.c](kernel/syscall.c), `syscall_handler` validates/dispatches READFILE;
[fs.c](kernel/fs.c), `fs_lookup`/`fs_read` resolves and bounds it;
[block.c](kernel/block.c), `block_read` bounds the device request;
[ata.c](kernel/ata.c), `ata_read` performs PIO. These are linked Ring 0 services
in a monolithic kernel, not isolated microkernel servers. To use them, Tab →
Tools → `run view hello.txt` and Enter, with a mounted sample disk.

If no card is highlighted after switching from a six-stage walkthrough, R.
All four supporting topics share the same step variable, so a later step can be
outside this five-stage list. Nothing in the diagram diagnoses or changes that
real request; the Tools command is separate and is not run by Space.

## 13. Boot dependencies

### Purpose and controls

Open **Boot dependencies**, then R. Ask: **Which services must exist before
another subsystem can safely use them?** The available experiment is a six-stage
illustrative order. Space advances, B reverses and R restarts; it cannot actually
reorder initialization, remove a dependency, reboot, configure firmware or
simulate a boot panic. E/I provides the deeper dependency explanation.

### Complete walkthrough with dependency interpretation

1. **R:** `Firmware`. This stage locates a boot path before the kernel exists.
   The already-running Explorer is not returning the machine to firmware.
2. **Space:** `Limine / ELF`. The bootloader reads configuration and loads ELF
   segments/entry. The kernel relies on Limine responses, including memory map,
   HHDM and framebuffer; this diagram does not parse an ELF file.
3. **Space:** `Physical allocator`. The real allocator must know usable versus
   reserved memory before it can return a frame safely.
4. **Space:** `Paging and heap`. Mapping needs frame/table backing; heap
   expansion needs both frame allocation and mappings. These arrows describe
   prerequisites, not a fresh allocation when you press Space.
5. **Space:** `Interrupts / scheduling`. Handlers, a timer and saved contexts
   make preemption, sleeping and input wakeup meaningful.
6. **Space:** `Explorer`. The UI depends on validated graphics/input and an
   ordinary rendering thread. Stage=5; extra Space stays here.
7. **B, R:** reverse one stage, then restore Firmware. Only `tick_step` and
   the context message change; booted subsystem state stays intact.

If physical allocation were missing, heap expansion could not acquire backing
frames. If IDT/PIC/timer setup were wrong, normal preemption/input could fail.
Those are causal explanations, **not failures you can inject with these keys**.

### Actual initialization and source references

[main.c](kernel/main.c), `_start`, does more than the compact six labels:
serial initialization; memory map reporting; PFA, VMM and heap initialization/
verification; GDT/IDT/exception/PIC/PIT setup and timer checks; keyboard/serial
input; thread scheduler; kernel process setup; thread, user-mode and process
suites; storage initialization and filesystem/live tests; then terminal graphics,
`live_start`, shell initialization and `shell_run`. A missing disk is optional
and can leave the UI usable. Drawing starts after timing-sensitive boot tests.

The simplified model groups these operations; it is not a timestamped trace of
all calls. [linker.ld](linker.ld) declares ENTRY(_start), higher-half placement
and segments; [limine.conf](limine.conf) selects the Limine protocol/kernel;
[Makefile](Makefile) packages the ISO. Actual initialization functions include
`pfa_init`, `vmm_init`, `heap_init`, `gdt_init`, `idt_init`, `pic_remap`,
`pit_init`, `keyboard_init`, `thread_init` and `process_init` in their respective
kernel source files. Firmware/Limine stages are outside `_start`.

If you want actual boot evidence, read serial logs and existing boot-suite
results; changing this highlight cannot establish success or failure of any
subsystem. R handles the shared-step issue; Esc leaves without rebooting.

## 14. CPU execution

### Purpose and controls

Open **CPU execution**, then R. Ask: **What execution values and stack contents
must remain consistent across arithmetic, a call, push/pop and return?** Real
thread switching must preserve machine state, not merely a function name.

This is a fixed six-stage illustration. Space advances, B reverses and R resets.
It displays only numeric RAX/RBX and a stack **description**; it has no editable
instruction bytes, general register file, live RIP/RSP, flags, memory inspector,
breakpoints or execution of the shown instructions by a simulated CPU.

### Complete register/stack trace

1. **R:** `MOV RAX,2` is highlighted. RAX=2, RBX=0, “Stack empty before call.”
   The model starts with the MOV's illustrated result already present; it does
   not begin at a zeroed register file and then execute a separate MOV tick.
2. **Space:** `ADD RAX,3`; RAX=5, RBX=0; stack description stays empty.
3. **Space:** `CALL helper`; RAX=5, RBX=0; stack text says saved return address.
   The illustration explains how a call makes return possible.
4. **Space:** `PUSH RAX`; stack text says value 5 above the return address.
   No numerical RSP decrement or memory write is stored by this model.
5. **Space:** `POP RBX`; RBX=5, RAX=5; return address remains on the described
   stack. The pushed operand and the call return address have different roles.
6. **Space:** `RET`; stack text says the instruction pointer was restored and
   the stack is empty. No numeric instruction pointer is displayed or changed.
7. **B:** returns to POP with RBX=5 and return-address text; **R** restores
   MOV/RAX=2/RBX=0. These are deterministic redisplays, not reverse machine execution.

### Internals, kernel connection and limits

[education.c](kernel/education.c), `peripheral_canvas`, computes displayed RAX
as `tick_step ? 5 : 2`, RBX as `tick_step >= 4 ? 5 : 0`, and chooses one of the
stack descriptions. `peripheral_action` only changes that shared counter.
Thus this topic is honestly a supporting illustration, not the same kind of
mutable register experiment as the memory/process models.

Real: [idt_stubs.S](kernel/idt_stubs.S), `isr_common_stub` saves/restores
actual general registers; [idt.h](kernel/idt.h), `interrupt_frame` describes
the saved context; [thread.c](kernel/thread.c), `scheduler_on_interrupt`
chooses the frame to resume; [usermode_entry.S](kernel/usermode_entry.S),
`user_enter_context` prepares Ring 3 entry; [gdt.c](kernel/gdt.c),
`gdt_set_kernel_stack` configures privileged entry stack state. These paths,
not the highlighted six strings, execute real x86-64 operations.

If RAX seems to change before a separately animated instruction, the displayed
values are stage formulas as above. If nothing is highlighted, R after opening;
the shared counter may have been left at another topic's stage. PgDn/End exposes
register/stack rows below the six cards on shorter displays.

## 15. Failure investigation

### Purpose and controls

Open **Failure investigation**, then R. Ask: **Which observation would separate
failure to launch, an expired process, an outdated snapshot and unfinished
rendering?** Reproducible debugging distinguishes stages rather than deleting an
assertion after an unexplained mismatch.

Space advances through six prompts, B reverses, R resets. This is a reasoning
walkthrough. It has no actual fault injection, process selection, log loader,
framebuffer comparison, automated diagnosis or test execution control.

### Complete investigation walkthrough

1. **R:** `Observe launch`. In a real investigation, first establish whether
   creation/launch occurred. The card itself launches nothing.
2. **Space:** `Check lifetime`. A successfully launched task may already have
   exited before it could appear in sampled graphics; distinguish those events.
3. **Space:** `Collect snapshot`. A live process must be present in the data
   collected for the displayed frame; old snapshots are different evidence.
4. **Space:** `Finish rendering`. Serial output may finish before the normal
   graphics worker drains its queue. A serial marker alone does not establish
   that the corresponding pixels are present.
5. **Space:** `Inspect pixels`. An actual screenshot check must examine the
   expected content/identity, not merely show that a screenshot file exists.
6. **Space:** `Assert evidence`. In a real test, retain the original behavioral
   assertion and synchronize with bounded, relevant observations. This card
   **does not run an assertion or emit a PASS result**.
7. **B, R:** return to Inspect pixels, then the first prompt. Only the highlight
   and context stage change. No kernel failure has been reproduced or repaired.

### Actual evidence paths and limitations

Model: [education.c](kernel/education.c), `peripheral_action`,
`peripheral_canvas` and the `debug[]` label list. Reaching the final card proves
only that this UI accepted navigation and selected that stage, not that a real
process, memory invariant, syscall or display test succeeded.

Real investigation tools: [live.c](kernel/live.c), `live_collect`,
`live_observe_context`, `live_dump`, `live_runtime_check`, plus the ordinary
`live_worker`; [gterminal.c](kernel/gterminal.c), `gterminal_flush`;
[thread.c](kernel/thread.c), `scheduler_check_invariants`;
[process.c](kernel/process.c), fault recording and guarded destruction;
[tools/test_live.py](tools/test_live.py), [tools/test_graphics.py](tools/test_graphics.py),
[tools/test_course.py](tools/test_course.py), [tools/test_final.py](tools/test_final.py).
These use actual snapshots, model checks and QEMU/PS2/framebuffer observations as
specified by their code. A full-suite PASS requires running and observing them;
it cannot be inferred from this walkthrough or a successful build.

Tab → Tools `live` prints the sampled real state; `livecheck` invokes real
allocator/process/syscall diagnostics; they are separate commands, not actions
performed by Space here. Do not invoke them expecting this highlight to become
a live progress monitor. Read real outputs and failure records independently.
If this page seems too simple, that is its current limit, not a hidden diagnostics
mode. Use I for source, or R to retry its six prompts.

## Glossary

| Term | Meaning in this guide |
| --- | --- |
| ABI | Agreement for how callers encode arguments/results and preserve state; the real syscall ABI uses specified registers. |
| Allocation | Assigning a free resource to an owner. Physical allocation and virtual mapping are different operations. |
| Bitmap | One bit per item. In the frame model, 1 means unavailable and a separate mask distinguishes reservations. |
| Blocked | Not eligible to execute until an event/wake occurs; different from terminated. |
| Burst | Required CPU execution duration. The scheduling model's bursts are fixed tick counts, not measured live instructions. |
| Canonical address | An address with the required sign-extension of implemented x86-64 virtual address bits; this four-level model uses bit 47. |
| Coalescing | Combining adjacent free heap extents; it cannot combine through a live allocation. |
| Context switch | Saving/selecting execution state so another thread can run. Model timeline cells do not execute real switches. |
| CR3 | x86-64 register selecting a page-table root. The virtual-memory experiment does not edit it. |
| Critical section | A shared-state operation that must not be interleaved unsafely; the counter model protects read/add/store together. |
| Deadlock | A dependency arrangement in which tasks cannot acquire what they need to proceed while retaining what peers need. |
| EFAULT | Bad-address syscall error; the syscall model displays -2 for its invalid-buffer scenario. |
| ELF / entry point | Executable image metadata / location where the loader begins execution; the real entry is _start. |
| EOF | End of a logical file. Starting at/past its size returns zero bytes rather than reading unrelated data. |
| EOI | End-of-interrupt acknowledgement to an interrupt controller; separate from returning to interrupted execution. |
| Extent | A contiguous range. Heap extents include metadata/slack; filesystem extents identify data sectors. |
| FCFS | First Come, First Served: select earliest arrival among eligible work; this model is nonpreemptive. |
| First fit | Choose the first sufficient free heap extent in scan order. |
| Frame / page | Physical allocation unit / virtual mapping unit; the actual kernel uses 4 KiB base pages. The small frame grid is synthetic. |
| Fragmentation, external | Free capacity split across gaps too small for one contiguous request. |
| Fragmentation, internal | Space inside an allocation not used as payload, such as alignment slack or an unsplittable tail. |
| GDT / IDT / TSS | Descriptor table for segments/selectors / interrupt gate table / CPU task-state structure used for privileged stacks and IST support. |
| HHDM | Higher-half direct mapping used by the kernel to access physical memory through a virtual offset. |
| IRQ / exception | External hardware interrupt / synchronous CPU event associated with instruction execution. Only fixed timer IRQs are modeled here. |
| Kernel space / user space | Privileged implementation environment / restricted application environment. The Explorer itself is kernel UI code. |
| Mapping | A virtual-to-physical association with permissions. Removing it need not free its backing frame. |
| Orphan | A living child whose parent has exited; this model adopts it under PID 1. Not synonymous with zombie. |
| PID / PPID / slot | Process identity / parent identity / numbered UI-array position. A slot can be reused without reusing the PID. |
| PIO / MMIO | CPU transfers through device ports / accesses to mapped device registers. Model file reads perform neither. |
| Pitch | Byte distance between actual framebuffer rows, potentially larger than width times bytes-per-pixel. |
| Preemption / quantum | Involuntary scheduling interruption / time allowance before RR rotates. Priority/SJF in this model do not preempt running eligible work. |
| Ready / running | Eligible to be scheduled / currently continuing on the modeled CPU. A completed timeline cell is historical execution, not necessarily current state. |
| Reaping | Collecting termination status and removing a terminated child's retained record. |
| Response time | First dispatch minus arrival. It is not completion time and does not subtract pre-first-run blocking. |
| Ring 0 / Ring 3 | Privileged / restricted x86 execution levels used by this kernel. Model colored regions do not change real privilege. |
| RIP / RSP / RAX / RBX | Real x86 instruction pointer / stack pointer / general registers. CPU walkthrough numbers/text are illustrative formulas. |
| Round Robin (RR) | Cyclic turns among eligible tasks, limited by a quantum. |
| SJF | Shortest Job First; this model compares full configured burst lengths when choosing a new task. |
| Syscall | A controlled request across the user/kernel service boundary, distinct from an ordinary same-privilege function call. |
| TLB | CPU translation cache. Real mapping changes may need invalidation; the model does not maintain a TLB. |
| Turnaround / waiting time | Finish minus arrival / eligible delay excluding execution and modeled blocked ticks. |
| Zombie | A terminated process retaining limited status/identity until collection; not a running child or an ordinary leaked heap block. |

## Troubleshooting

### Keys appear to do nothing

- Give the graphical guest keyboard focus. Serial input intentionally remains
  shell input and does not manipulate Explorer models.
- Read the highlighted concept/title. B means block in Processes/Scheduling,
  back only in the four supporting walkthroughs. F means different operations
  in different experiments; there is no universal next/back button.
- If an optional explanation/source/question is open, Esc closes it before
  experiment controls (including R) work. E/I also toggle their own views.
- Use the exact action key. Enter opens a Home selection, not a model step.
  Numbers on Home do not launch concepts. Numbers in Processes are slots.
- A condition can legitimately refuse the action: full table/ring, reserved
  frame, free heap block, absent child, unarrived/completed task, active syscall,
  noncanonical mapping, or masked/empty IRQ delivery. Read the context panel.
- Allow the normal worker time to draw and press keys deliberately. Holding a
  key may repeat actions; model/message changes are not rendered in IRQ context.

### Reset, navigation or display seems inconsistent

- R on the canvas restores that experiment, not the real kernel. There is no
  global reset of every kernel subsystem and no general undo control.
- Esc from an optional view returns to the experiment first; Esc again reaches
  Home. Tab returns to the same Explorer location after Tools, not necessarily
  Home. Arrow selection on Home advances one concept, not one visual row.
- The supporting topics share `tick_step`; R after opening avoids a stale stage.
- PgDn/End exposes lower model rows or long explanations. Home returns the canvas
  to its top; it is not the Explorer-menu key. Low-resolution canvases may show
  only part of a tree/timeline/register listing at once.
- Cursor/selection movement may update only the canvas, leaving the previous
  context message. Use the visible selection, model values and headings too.
- Filesystem O/L preserves the old result until D; its displayed 31-byte text
  length differs from the 32-byte backing string. Concurrency F preserves some
  old wait flags; successful reacquisition or a restart clears them. These are
  current implementation quirks, not additional controls to discover.
- The source framebuffer path needs supported Limine RGB graphics (the current
  terminal setup requires at least 640×400). A serial fallback is not a graphical
  Explorer. Unsupported graphics are not repaired by pressing navigation keys.

### The experiment works, but real Tools data does not match

This is expected: each mechanism has its own synthetic state. Model PID 11,
frames 0..15, model disk text, a modeled tick or placeholder RIP do not identify
live kernel objects. E/I explains the connection without equating the values.
Tab → Tools `ps`, `threads`, `sched`, `mem`, `vmmap <pid>`, `live`, `fsinfo` and
other existing commands are separate real inspections; use their actual reported
IDs and availability. No model action allocates a real process or writes disk.

A model file read succeeding does not show that ATA mounted a disk. An IRQ
counter advancing does not test the real PIC. A completed Failure investigation
walkthrough does not prove a test suite passed. Actual kernel test results and
serial/graphics observations establish those facts independently.

In Tools, `help` is paged: Space/PgDn continues a page, Enter one line, q quits;
`help all` does not pause. Tab can return to Explorer while help waits, and Tab
back resumes the pager. During a foreground application the main shell thread
may be waiting for its exit rather than consuming UI keys; wait for return.
If a real syscall/app/file command fails, inspect its real error and source rather
than resetting a teaching model in an attempt to repair it.

## Source and verification scope

The controls, initial conditions and state changes were inspected in:

- [kernel/education.c](kernel/education.c): names/order, `education_input`,
  `event`, `reset_model`, `process_action`, `scheduling_action`, `memory_action`,
  `peripheral_action`, all corresponding canvas functions, `controls`,
  `explanation` and `education_render`.
- [kernel/education_model.h](kernel/education_model.h) and
  [kernel/education_model.c](kernel/education_model.c): model structures,
  lifecycle, scheduler, frame, mapping and heap algorithms.
- [kernel/lessons.h](kernel/lessons.h) and
  [kernel/lessons.c](kernel/lessons.c): optional authored explanations, kernel
  references and reasoning feedback. Their content order differs from Home;
  `concepts[]` maps the current names to the matching optional content.
- [kernel/keyboard.c](kernel/keyboard.c), [kernel/shell.c](kernel/shell.c),
  [kernel/gterminal.c](kernel/gterminal.c), [kernel/live.c](kernel/live.c):
  actual input origin/focus and deferred graphics integration.
- The real subsystem files linked in each mechanism section, plus
  [tests/education_test.c](tests/education_test.c) and
  [tools/test_course.py](tools/test_course.py) for the existing asserted model
  and guest workflows. Contextual-help regression checks now extend the guest workflow.

When this guide was originally authored, temporary **host-side C trace harnesses** compiled
the unchanged controller/model code and directly exercised its event handlers
and model functions. They checked the four-policy table, process exit/collection/
adoption, heap fragmentation calculation, file contents/counts, IRQ phases,
syscall results, race/deadlock/recovery values, input-ring counts, pixel updates
and fixed CPU register formulas. Rendering and hardware input timing were not
exercised by those trace harnesses. Examples describe the screen values that the
inspected renderer derives from that state; they are not a claim of a fresh
manual guest test of every listed key.

The original guide was a documentation-only change. The contextual-help update
below adds Explorer behavior, an ISO build and automated host/QEMU coverage.
The model-vs-live distinction, 32-byte file length discrepancy, stale recovery
wait flags and shared walkthrough step remain documented rather than changed.
VirtualBox-specific key delivery is not covered by the QEMU tests.


## Contextual “How to use” panels

Every mechanism now has a teal **[H] How to use** button at the upper right.
Press **H** on the PS/2 keyboard to open its instructions without leaving the
experiment. This is a keyboard button; the OS does not have mouse input.

Each of the 15 panels contains Start here, the actual controls, a numbered
example, expected observations, the underlying rule, an alternative experiment,
real kernel source connections, and reset instructions. Teaching models are
explicitly distinguished from actual kernel operations. Reading an example does
not run it: close help before trying the steps.

- **Up/Down:** scroll one line.
- **Page Up/Page Down or Space:** scroll half a panel.
- **Home/End:** beginning/end of the help text.
- **H or Esc:** close help and restore the prior experiment or inspection view,
  including its scroll position and model selection.
- **Tab:** switch to Tools and back; help remains open.

Experiment controls, including R, are inactive while help is open. A running
scheduling animation is held, then resumes with its remaining delay; time spent
reading does not advance the teaching model. Real kernel scheduling and the
serial shell continue normally. Close help, then press R to deliberately reset.

Regression coverage is integrated into `python3 tools/test_final.py`: the host
checks all model fields, saved views and scroll positions, keyboard isolation,
animation resume timing and framebuffer guards at 640/1024/1280 widths. QEMU
checks all 15 visible buttons, mechanism-specific first paragraphs, scrolling,
Home navigation and pixel-identical return to each experiment. These augment the
existing Explorer, terminal, live inspector, application and persistence tests.


Contextual-help validation (2026-10-22): `make -B` produced the ISO, and
`python3 tools/test_final.py` finished with FINAL PASS. The observed results
include the new host and all-15 QEMU help checks, existing Explorer and terminal
checks, live isolated-process visualization, applications and two storage boots.
The persistent disk SHA-256 remained unchanged. Screenshot comparisons wait for
two identical frames because a deferred redraw can be sampled while partially
cleared; existing behavior assertions remain intact. No VirtualBox run was made.
