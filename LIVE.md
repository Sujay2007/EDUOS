# Task 9: live kernel visualization

Build and launch using the existing graphical configuration:

```sh
make -B
make run-graphics
```

The desktop now reserves a right-hand panel for live kernel state. The existing
shell remains on the left, with the same PS/2 and serial input and filesystem
commands. `clear` preserves the panel. No window manager or application layer
has been added. `make run` still provides headless serial operation.

## Actual data sources

* Physical memory: `pfa_get_stats` usable/allocated/free page counts, displayed
  in KiB, plus an allocation proportion bar. Allocated includes allocator bitmap
  metadata, matching the allocator's existing accounting.
* Scheduling: existing `scheduler_get_stats` switch/preemption/yield counters,
  plus measured switch deltas and a 32-sample history. The history clips at 20
  switches per sample; the numeric total/delta remains exact. These are counts,
  not CPU utilization percentages.
* Timer: `timer_get_ticks`, the actual 100 Hz PIT tick counter.
* Syscalls: constant-time accounting at the real `int 0x80` dispatcher entry,
  including all calls, exit/write/getpid/yield/unknown counts and last caller.
  Rejected requests still count as activity.
* Processes and threads: copies from the existing live subsystem lists, with
  real IDs, names and states. A maximum of eight threads and six processes is
  copied per snapshot, with explicit truncation labels in the full-size panel.
* CPU context: the collector's current TID and active CR3 are labeled explicitly.
  A small scheduler-entry hook records the latest non-collector context and last
  user-process context. They are historical scalar observations, not necessarily
  still-live processes; their cached CR3 values are never dereferenced.
* Virtual memory: four bounded page-table queries for kernel text, collector
  heap data, user code and user stack. The root is selected from the first live
  user process in the bounded process list, otherwise PID 0's kernel root.
  Missing mappings are shown as absent. The queries handle 4 KiB, 2 MiB and
  1 GiB leaves and combine USER/RW/NX permissions across every paging level.
  This is a relevant mapping sample, not a complete scan of virtual memory.

The active CR3 normally belongs to the kernel because the visualizer is a kernel
thread. The selected process root and historical user context are separate
fields so the collector does not misrepresent itself as an executing user task.
Mappings are copied while process/page-table lifetimes are protected against
scheduling and destruction. No arbitrary addresses are probed and no list/table
pointers are retained for rendering.

## Collection and rendering

One `live_view` kernel thread drains up to 128 graphical terminal bytes every two
ticks and samples/redraws statistics every 25 ticks (about four times per second,
subject to scheduler/rendering delays). The existing shell and user processes
can run between updates. The observer itself contributes actual switches and
memory use; these are not subtracted or simulated.

Snapshots copy allocator, syscall, scheduler, bounded process/thread records and
four mappings under interrupt exclusion. Formatting and framebuffer drawing take
place afterward in normal thread context with interrupts enabled. No graphics
or new formatting is performed by timer, syscall or scheduler handlers.

The serial mirror now only appends bytes to a 32 KiB bounded queue. The worker
alone owns graphical terminal rendering, scrolling and panel updates, so syscall
output and fault diagnostics no longer draw in interrupt context. Scrolling
copies visible framebuffer rows instead of repainting all text glyphs, keeping
large shell responses from monopolizing the worker. Serial output
remains synchronous and unchanged. If the queue fills, graphical bytes are
dropped and counted; serial bytes remain available. The full panel and `live`
diagnostic expose this count. Unsupported framebuffers retain serial diagnostics.

The full panel is intended for the default 1280x800 mode. At smaller supported
modes a compact panel shows the core metrics, one mapping, two threads and one
process; the full bounded snapshot is still available through `live`. Terminal
width adapts to the panel, without changing shell parsing or keyboard controls.

## Diagnostics and tests

`live` prints the last completed/displayed snapshot for comparison, rather than
collecting unrelated values at command time. It includes raw page counts, ticks,
switches, syscall count, IDs, roots, sampled mappings and effective flags.

The boot-time `[LIVE TEST]` diagnostic checks allocator conservation and a real
page allocation/free, current TID/CR3, syscall category totals, mappings against
the existing VMM API, effective ancestor restrictions and 4K/2M/1G leaves. It
uses a temporary private page table and restores all physical pages afterward.

`livecheck` is an opt-in, focused runtime test. It holds one allocated page long
enough for a displayed refresh, then creates a temporary isolated process that
calls real getpid/yield syscalls for 75 ticks. It verifies the published snapshot
against the allocator, code/stack frames, process root and syscall/context data,
then stops and destroys the process using the existing lifecycle APIs. It does
not add an application or alter the normal boot workload.

```sh
python3 tools/test_live.py
# or, with the normal build dependency:
make test-live
```

The test runs the existing graphics primitives and QEMU graphics checks, including
PS/2 input/backspace and existing thread, user-mode, process and filesystem boot
suites. Additional checks decode actual panel glyphs from QEMU screenshots and
compare their numbers to the last displayed snapshot and `mem` output. They
verify idle refresh, real scheduling progress, the `livecheck` workload and
persistent-file commands afterward. Screenshots and logs remain under
`/tmp/eduos-graphics/`. Tests require the standard sample disk. The storage and
standalone graphics test commands remain available.

Exact QEMU launch command:

```sh
qemu-system-x86_64 -M q35 -m 256M \
  -cdrom educational-os.iso -boot d -serial stdio -display gtk \
  -device piix3-ide,id=eduide \
  -drive if=none,id=edudisk,file=disk.img,format=raw \
  -device ide-hd,drive=edudisk,bus=eduide.0
```

Automated display verification uses the same machine/disk configuration with
`-display none` and a temporary QMP socket for screenshots and key injection;
the emulated framebuffer remains active.
