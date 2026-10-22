# Interactive OS Explorer

The default framebuffer interface opens mechanisms directly. There are no
sequential lesson tabs, completion badges, mandatory quizzes or progress gates.
The large canvas shows model state; controls and a contextual explanation stay
visible below it. The shell and real live inspector remain in **Tab: Tools**.
Serial always operates the shell independently of the displayed experiment.

## Navigation

- Home: arrows choose a concept; Enter opens its experiment immediately.
- Esc returns to Home, or closes an optional inspection view first.
- R restores the selected experiment's known initial state (including its
  configuration). Leaving and reopening a concept otherwise retains its model.
- E opens deeper algorithm/data-structure/example/failure/trade-off explanations.
- I opens source-verified implementation details and kernel limitations.
- ? opens an optional reasoning prompt; 1/2/3 gives explanatory feedback.
  It has no score, badge, completion requirement or persistence.
- PgUp/PgDn and Home/End scroll the canvas or optional explanation. Controls
  and the current action explanation remain fixed. Inspection pauses scheduling
  animation. Tab switches to/from Tools, including while help is paused.

All experiments are **teaching models**, never live kernel traces. No experiment
changes actual process relationships, allocators, mappings, IRQ masks or device
metadata. Tools remains the route to real apps, disk files and sampled state.

## Direct controls and assumptions

| Explorer | Working controls and consequences |
| --- | --- |
| Processes | 1..8 selects a slot (distinct from PID); C creates a child of the selected living parent; X exits with status 7; S dispatches; B blocks; U wakes; W waits/collects as the selected parent; Z collects a selected zombie as its parent. Multiple children and nested trees are supported. |
| CPU scheduling | 1..4 selects a task; N adds up to four; +/- changes burst (1..8); A cycles arrival (0..8); P cycles priority (1 highest, 4 lowest); G cycles FCFS/SJF/RR/priority; Q changes RR quantum (1..4); B blocks; W wakes; Space advances one tick; X runs/pauses. Configuration changes restart time for policy comparison. |
| Physical memory | Left/Right selects one of 16 frames; A scans from the remembered cursor and allocates; F frees the selected usable allocation. Reserved and duplicate frees fail; exhaustion is explicit. Three frames are permanently reserved. |
| Virtual memory | 1..4 selects a fixed address; M maps; U unmaps; P changes leaf write permission; W selects read/write access; Space follows translation and reports the stopping level or physical result. One address is noncanonical and cannot be mapped. |
| Heap | +/- changes aligned payload size (16..96); A allocates by first fit; Left/Right selects a block; F frees and coalesces it. The 256-byte arena has simplified 16-byte headers (the real heap has 32-byte headers). Extent size includes metadata. |
| Interrupts | K queues an IRQ (four maximum); M masks/unmasks model delivery; Space progresses through acceptance, register save, handler counter update, EOI and return. Pending, handled and saved-return state change independently. |
| System calls | 1/2/3 selects write/readfile/getpid; P toggles an invalid buffer; C enters the kernel; Space validates, executes or returns. Bad buffers yield -2 before copying. Active arguments cannot be edited until return/reset. |
| Filesystems | 1 selects 31-byte text, 2 empty, 3 missing; O advances offset by 8 (wrap after 40); L cycles capacity 8/16/32; D performs lookup and a bounded read. Destination bytes, EOF and errors are visible. This is model data; real EDUFS remains accessible through Tools. |
| Concurrency | 1/2 chooses a task's next operation. In counter mode each does read/add/store; L restarts with/without a critical-section lock. D switches to a two-resource experiment. Tasks request A/B in opposing orders, or L enforces one order. Hold/request edges expose a cycle; F releases the selected actor's resources as modeled recovery. |
| Keyboard input | K injects a modeled A make event; Space consumes it. An eight-slot queue exposes head/tail, occupancy, consumed event and overflow/drop count. This is separate from the real keyboard ring. |
| Framebuffer | Arrows move a cursor in a 12x8 retained grid; G changes color; Space paints one pixel; F paints a clipped 3x2 rectangle. Reset clears the grid. |

OS architecture, boot dependencies, CPU execution and failure investigation
remain optional supporting step/back/reset walkthroughs. They are not presented
as full direct-manipulation laboratories. All fifteen topics retain the authored
explanations and source references as optional support rather than prerequisites.

### Process semantics

The model has a protected designated reaper PID 1 and an initial process PID 10.
PIDs are monotonic; freed slots can be reused without reusing a PID. A parent can
create several ready children, block waiting, run, wake or exit in any order.
An exit stops execution and releases modeled pages, but retains a zombie status
record. Waiting with an available status collects/reaps immediately; waiting
before exit blocks and then collects automatically when a child exits.
Parent exit reparents children to PID 1, including existing terminated records.
A living adopted child is labeled separately from a zombie. Reaping removes the
status record rather than killing a living process. The model caps eight records
and refuses invalid transitions or termination of the designated reaper.

The page count is artificial accounting (three per ordinary process, one for the
reaper), not real resource usage. Our kernel has **no parent PID, fork, exec,
waitpid, reparenting or Unix zombie protocol**. Its embedded app launcher uses
explicit process_create, timed waits, exit_code and process_destroy. Actual user
pages are reclaimed at destruction; do not infer that they follow the model's
release-at-exit accounting. The source view explains that distinction.

### Scheduling calculations

FCFS, SJF and priority are nonpreemptive until completion/blocking. Lower numeric
priority wins; ties use task order. RR rotates after the selected quantum.
The display includes a ready queue, RUNNING/READY/NEW/BLOCKED/DONE states, a
48-tick timeline and per-task completion, waiting, turnaround, response and
blocked-time counts. Unavailable metrics remain pending.

- Turnaround = finish - arrival.
- Waiting = turnaround - CPU burst - blocked ticks.
- Response = first dispatch - arrival (not reduced by blocked time).

Manual blocking is allowed only for arrived unfinished tasks. Idle ticks still
advance time when no task is eligible. There is no context-switch overhead,
priority aging, I/O-device timing or live kernel task mutation. X advances at
most one model tick per 20 real PIT ticks in the normal renderer, never IRQ
context; Tools/inspection does not accumulate a catch-up backlog.
The actual kernel remains round-robin with a five-tick quantum and interrupt-time
sleep-deadline checks. Kernel scheduling code was not changed.

### Paging and memory

Teaching addresses are 0x400123 (mapped), 0x600123 (initially missing a PD entry),
0x401abc (initially read-only) and noncanonical 0x800000000000. The walker uses
actual bit extraction for PML4/PDPT/PD/PT indices and the 12-bit offset, but fixed
bounded model metadata, not arbitrary kernel page-table pointers. Mapping does
not allocate real frames. Upper modeled levels permit user writes; leaf
permission and presence determine the selected access. Framebuffer operations
are clipped to retained model pixels before the real UI renders them.

## Reproducible experiment

1. Boot, Enter Processes. Initial selection is slot 2, PID 10.
2. C, C creates children PID 11 and 12 in slots 3 and 4.
3. 3, X exits PID 11: zombie, status 7, modeled pages released.
4. 2, W collects its status and removes the child's record.
5. 2, X exits PID 10: PID 12 stays alive and is adopted by PID 1.
6. 4, S runs the adopted child. 1, W collects the old parent's status.
7. 4, X, Z terminates and collects the remaining child.
8. R resets. Try C, W, 3, X to observe wait-before-exit automatic collection.

For a race: open Concurrency, press 1,2,1,2,1,2 to get x=1. R, L and
1,1,1,2,2,2 yields x=2. For deadlock: R, D, 1,2,1,2 creates both waits;
F models recovery, or L restarts with global ordering to prevent the cycle.

## Integration and source map

`education.c` provides explorer selection, event handling, reusable clipping/
wrapping, pinned controls/context, optional inspection and bounded model state.
`education_model.c/.h` provides testable process/frame/paging/scheduling/heap
algorithms. `lessons.c/.h` retains existing authored explanations and references;
its former completion-oriented presentation is removed. No new dependency or
window framework is introduced. `live_worker` is still the only renderer; IRQs
only queue existing input/output. Existing terminal and application canvas state
continue to be retained while hidden.

| Subject | Actual implementation anchors (under kernel/ unless noted) |
| --- | --- |
| Processes/apps | process.h:process; process.c:process_create/process_exit/process_destroy; apps.c:app_run; thread.c:reclaim_dead_threads |
| Scheduling | thread.h:SCHEDULER_QUANTUM_TICKS; thread.c:select_next_thread/scheduler_on_interrupt/thread_sleep; pit.c:pit_irq_handler |
| Frames | pfa.c:pfa_alloc_page/pfa_free_page/pfa_get_stats |
| Paging | vmm.c:vmm_map_page_pml4/vmm_query_mapping/vmm_invlpg/vmm_create_address_space |
| Heap | heap.c:heap_block/kmalloc/kfree/heap_expand |
| Interrupts | gdt.c:gdt_init/gdt_set_kernel_stack; idt.c:isr_dispatch; idt_stubs.S:isr_common_stub; pic.c:pic_send_eoi |
| Syscalls | syscall.c:user_buffer_validate/syscall_handler; syscall.h; usermode_entry.S:user_enter_context |
| Storage | block.h:block_device; ata.c:ata_init/ata_read; fs.c:fs_mount/fs_lookup/fs_read; tools/mkfs.py |
| Concurrency | idt.h:interrupts_save_and_disable/interrupts_restore; keyboard.c and gterminal.c queues; live.c:live_collect. No general mutex/semaphore/condition-variable API. |
| Input/graphics | keyboard.c:keyboard_isr/keyboard_getchar; graphics.c:gfx_surface_init/gfx_rect; font.c:gfx_glyph; text_buffer.c; gterminal.c:gterminal_flush |
| Boot/debugging | main.c:_start; linker.ld; limine.conf; thread.c:scheduler_check_invariants; tools/test_final.py/test_live.py |

Changed for this redesign: `kernel/education.c`, `kernel/education_model.c/.h`,
`kernel/gterminal.c` (Tools shortcut label), `tests/education_test.c`,
`tools/test_course.py`, `README.md`, and this document. Core subsystems and their
APIs were not rewritten. Earlier uncommitted work is retained. Generated ELF,
ISO and objects change on build. Pre-redesign artifacts and relevant sources
are backed up in `/tmp/eduos-before-explorer/`; disk.img remains read-only.

## Build and validation

```sh
make -B
python3 tools/test_final.py
make run-graphics
```

The existing Q35/256 MiB/ISO/legacy IDE QEMU launch flags are unchanged (README).
Host checks cover process transitions/tree validity/cleanup/capacity, all four
policies/quantum/blocking metrics, frame exhaustion, mutable mappings and heap
invariants. Guarded UI rendering/input covers 640x400, 1024x768 and 1280x800.
Real QEMU checks use PS/2 controls and framebuffer glyphs/context, including
reset, each major action, source/deep views and Tools transitions. Screenshots
and log are in `/tmp/eduos-graphics/`. The previous course-specific section/badge
assertions are replaced because that interaction model is intentionally removed;
original terminal/live/process/application/persistence assertions remain.

Limits: bounded models, selected rather than arbitrary typed virtual addresses,
one manual blocked interval mechanism rather than a device I/O simulator,
nonpreemptive priority/SJF, no undo history for every operation, no persistent
learner state, and supporting walkthroughs rather than full laboratories for
architecture/boot/CPU/debugging. IRQs/syscalls/files/input demonstrate fixed
service/event types, not general device or instruction emulation. These models
never replace the working kernel implementations.

## Observed results

`make -B` produced the bootable ISO successfully; later focused renderer fixes
were rebuilt incrementally. The final `python3 tools/test_final.py` run reported
**FINAL PASS**: host model/renderer guards, direct PS/2/framebuffer experiments,
original graphics/terminal/navigation/help, live isolated processes, application
integration, all existing boot suites and both persistent-storage boots passed.
The disk SHA-256 remained
`8813b4e0d1544b31979aafd2174f9514d7ca02e120ea8eb66197b89fc4679d2b`.

The existing VirtualBox VM also booted at 1024x768. A smoke test created two
children, terminated/collected one, exited their parent, observed a living
adopted child separately from a zombie, and returned to Tools to run hello.
This is not a full VirtualBox suite pass: the existing preemption self-test
still reports failure and persistence tests skip without an attached data disk.
Prior ISO copies are preserved; new images/logs/screenshots are stored separately
under `C:/Users/Sujay Krishna R/VirtualBox VMs/Education OS/Explorer-validation-20261022/`.
