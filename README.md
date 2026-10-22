# Educational x86-64 OS

A real freestanding x86-64 kernel, booted by Limine and demonstrated in QEMU.
The default framebuffer interface is an **Interactive OS Explorer**: open a
mechanism directly, manipulate its bounded teaching model, and observe the
consequences. Processes, scheduling, memory, interrupts, syscalls, files and
concurrency have direct controls. Explanations and source references are optional;
there are no course-completion gates. Press **Tab** for the preserved shell,
real live inspector and application canvas. Serial shell access remains active.
See [LEARNING.md](LEARNING.md) for controls, model assumptions and source mapping.

## Architecture

* Limine loads a higher-half kernel and supplies the memory map, HHDM and RGB
  framebuffer. A bitmap physical allocator, four-level paging and kernel heap
  provide memory management.
* GDT/TSS, IDT, legacy PIC and a 100 Hz PIT support exceptions, keyboard/serial
  input and a preemptive round-robin thread scheduler.
* Each user process has a private lower-half page table, one code/data page,
  one user stack page and a kernel stack. Higher-half kernel mappings are shared
  supervisor-only. User faults terminate the affected process.
* Ring 3 applications call `int 0x80`. The kernel provides bounded output,
  process identity, scheduling, read-only file access and a restricted graphics
  canvas. Applications never receive a framebuffer or kernel pointer.
* EDUFS v1 provides a validated read-only root filesystem on a persistent ATA
  PIO virtual disk. The boot ISO and data disk are separate artifacts.
* A normal `live_view` thread drains graphical output and periodically renders
  real allocator, mapping, process, scheduling, tick and syscall statistics.
  Interrupt/syscall paths enqueue graphical work instead of drawing.

## Build and launch (Linux/WSL)

Required tools: Clang, LLD, Make, xorriso, Python 3, QEMU x86-64 and the bundled
`tools/limine` v8.7.0 release. Confirm that directory contains `limine`,
`limine.h` and boot binaries. The repository currently records it as a Git
gitlink without `.gitmodules`; a fresh checkout may need that release populated
manually. No additional Python packages or runtime libraries are required.

```sh
make -B
make run-graphics
```

`make -B` rebuilds all tracked object files to avoid stale binaries from earlier
milestones. Output: `educational-os.iso`, with `kernel/kernel.elf` inside. The
first build creates `disk.img` from `samples/`; subsequent builds and `make clean`
preserve the existing persistent disk. See [STORAGE.md](STORAGE.md) before
intentionally replacing a disk or refreshing its sample files.

`make run-graphics` uses GTK and needs a display server (for example WSLg):

```sh
qemu-system-x86_64 -M q35 -m 256M \
  -cdrom educational-os.iso -boot d -serial stdio -display gtk \
  -device piix3-ide,id=eduide \
  -drive if=none,id=edudisk,file=disk.img,format=raw \
  -device ide-hd,drive=edudisk,bus=eduide.0
```

`make run` uses the same machine/disk configuration with `-display none`, keeping
serial interaction. The emulated framebuffer still exists in headless mode.

## Applications

```text
apps
run hello
run files
run view hello.txt
run colors
run check
```

| Application | Demonstration |
| --- | --- |
| `hello` | Verifies Ring 3 and prints its actual PID through syscalls |
| `files` | Enumerates persistent root files from user mode, including byte sizes |
| `view [name]` | Reads text through a file syscall, default `hello.txt`, up to 16 KiB |
| `colors` | Queues an RGB/CMY strip in the top bar, sleeps, then exits |
| `check` | Checks user-pointer validation, file errors/EOF, canvas and sleep bounds |

These are fixed embedded position-independent images, launched using existing
process facilities. **There is no general ELF loader or disk executable loading.**
Each `run` is foreground: the shell sleeps until exit, reports status and reaps
the process; the graphics/live worker continues. Details: [APPLICATIONS.md](APPLICATIONS.md).

## Shell and demonstration

Existing commands remain available: `help`, `clear`, `mem`, `heap`, `ticks`,
`uptime`, `threads`, `sched`, `yield`, `sleep <ticks>`, `ps`, `proc <pid>`,
`vmmap <pid>`, `ls`, `cat <name>`, `fsinfo`, `live`, `livecheck`, `usertest`
(`ring3`), `proctest`, `about`, and `reboot`. New commands: `apps`,
`run <app> [filename]`, `apptest`. Input supports typing, Enter and Backspace
through PS/2 or serial; mouse/window management is not implemented.

Short reproducible presentation, using the standard sample disk:

1. Run `make -B`, then `make run-graphics`. Enter Processes directly. C creates
   children; select slots with 1..8, X exits, W waits/collects. R resets; Esc returns
   to the explorer grid. E opens an explanation; I opens source references.
   Press Tab for Tools and its `edus>` prompt.
2. Type `clear`, `fsinfo`, then `apps`.
3. Type `run hello`; observe the launch PID matching the application's PID.
4. Type `run files`, then `run view hello.txt` to demonstrate persistent data.
5. Type `run colors`; observe the color strip and the application's state in the
   live panel while it sleeps. The strip remains after the application exits.
6. Type `run check`, then `apptest` for the application and ABI checks.
7. Type `ps`, `live`, and `cat hello.txt` to show cleanup and continued shell use.

## Validation

After building, execute the final integration test once:

```sh
python3 tools/test_final.py
# Equivalent with build dependencies:
make test-final
```

This first tests explorer models, direct operations and real experiment-screen pixels,
then selects Tools and runs the existing primitive/graphical/live checks and boot-time thread,
user-mode, process and filesystem suites, then application launch/file/error/
canvas/cleanup checks. It checks real screenshots and PS/2 input, not just serial
success. It then runs the existing two-boot persistence regression and verifies
the raw disk remains byte-for-byte unchanged. Screenshots and the integrated
serial log: `/tmp/eduos-graphics/`; storage logs:
`/tmp/eduos-storage-boot-{1,2}.log`.

Individual checks remain available: `python3 tools/test_storage.py`,
`make test-graphics`, `make test-live`; inside the OS use `usertest`, `proctest`,
`livecheck` and `apptest`. The standard sample disk is required by sample-specific
tests; real missing/corrupt disks are reported as unavailable rather than rebuilt.

## Limits

The supported/test target is QEMU Q35 with the explicit legacy IDE controller.
BIOS boot is exercised by the automated suite. UEFI and physical hardware are
not certified. VMware is **not tested**: this development environment has no
VMware runtime or meaningful guest-test facility. The hybrid ISO's UEFI boot
image alone is not evidence of VMware compatibility.

The OS is single-CPU, uses PIC/PIT and ATA PIO/LBA28 (512-byte sectors, up to
128 GiB), and has no writable filesystem, general executable loader, POSIX ABI,
application keyboard-read syscall, or general user framebuffer access. The
filesystem is flat with at most 16 files. Application code/data is writable and
stacks currently executable, matching the existing process implementation.
Graphical output has a bounded queue; its drop count is visible through `live`.
Live views are sampled and bounded, so short-lived tasks may fall between frames.

Subsystem details: [GRAPHICS.md](GRAPHICS.md), [LIVE.md](LIVE.md),
[STORAGE.md](STORAGE.md), [APPLICATIONS.md](APPLICATIONS.md).
