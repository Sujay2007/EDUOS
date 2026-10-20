# Task 10: applications and final integration

## Launch model

`apps` lists five fixed images. `run <name> [filename]` selects one image from the
kernel registry, creates an isolated process, copies its code/data to `0x400000`,
maps one stack page at `0x7ffff000`, and starts its existing user thread at Ring 3.
The filename is copied to the top of the process stack and passed in RDI; the
initial RSP leaves room below it. The shell waits with timed sleeps, reports the
real PID/exit code, destroys the process and allows deferred thread-stack cleanup.
Unknown names and extra arguments are rejected before creating a process.

The code is embedded assembly in `kernel/apps_blobs.S`, using RIP-relative data
references so copying is valid. Each image must fit the existing 4 KiB limit.
`view` defaults to `hello.txt`; no other app accepts an argument. This deliberately
does not implement arbitrary executable loading, linking, fork, or exec.

Successful apps return status 0. File unavailability/invalid filename returns 2;
`colors` returns 3 if the graphical canvas is unavailable. `check` returns the
failed contract-test index on failure. A 1000-tick launcher timeout reports a
still-live process and retains it rather than violating existing lifecycle
guards by freeing its active memory. Normal supplied apps are bounded; `view`
stops at 16 KiB. The foreground shell cannot launch another app until `run`
returns. Other kernel threads and live visualization keep running.

## User ABI

Entry uses `int 0x80`, syscall number in RAX, arguments in RDI/RSI/RDX/R10 and
signed result in RAX. Existing calls 1–4 (exit/write/getpid/yield) are unchanged.

| Number | Call | Arguments and result |
| --- | --- | --- |
| 5 | `SYS_READFILE` | RDI=NUL-terminated root name, RSI=writable destination, RDX=capacity <=1024, R10=byte offset; returns bytes, 0 at EOF |
| 6 | `SYS_DIRENT` | RDI=root index, RSI=writable 40-byte record; writes name[32] and uint64 size, returns 0, ENOENT at end |
| 7 | `SYS_CANVAS_RECT` | RDI=readable 20-byte record of five uint32 fields: x,y,width,height,RGB; returns 0 when queued |
| 8 | `SYS_SLEEP` | RDI=ticks, 0–200 inclusive; 0 yields, returns 0 |

File operations validate user buffers, page permissions, bounded filenames,
request lengths, indices and offsets, then reuse the production filesystem API.
Names crossing into an unmapped page are rejected without dereferencing it.
These are stateless root operations: there are no per-process open handles or
write operations. The mounted disk is the source of file contents; file data is
not embedded in the applications. On I/O error, READFILE returns EIO even if a
prefix was copied; callers must treat the buffer as incomplete.

Error codes: ENOSYS=-1, EFAULT=-2, EINVAL=-3, ENOENT=-4, ENODEV=-5, EIO=-6,
EAGAIN=-7. Zero-length buffers permit NULL; filenames are still validated. All
calls, including rejected requests, count toward the existing live syscall stats.

The canvas is a fixed 192x24 region at the right of the desktop's top bar. It
cannot overlap the shell or live panel. Rectangles must be nonempty and entirely
inside it, with RGB <=0xffffff. The syscall copies the validated request into a
16-slot bounded queue; the existing graphics worker renders it in normal thread
context. Queue exhaustion returns EAGAIN. There is no direct user MMIO mapping,
arbitrary desktop drawing, window manager, or interrupt-context rendering.

## Supplied programs

* `hello` verifies CPL=3, calls GETPID, formats its own numeric PID, writes a short
  message and exits.
* `files` loops over DIRENT, prints names and decimal sizes from the mounted disk.
* `view` repeatedly reads 256-byte chunks, writes them to the serial/graphical
  terminal, yields between chunks, and stops at EOF or its display limit.
* `colors` submits six RGB/CMY rectangles and sleeps for 150 ticks before exiting,
  making its process and mappings observable in the live panel. Colors remain
  until another canvas operation or reboot; no terminal text is overwritten.
* `check` exercises bad kernel pointers, a nonterminated/invalid filename,
  missing files, oversized reads, huge-offset EOF, directory bounds, invalid
  canvas/sleep requests and filename/buffer access across an unmapped boundary.

Application input is supplied through shell command arguments; the existing
keyboard is used to launch programs, not exposed as a new application input API.

## Integration checks

`apptest` launches all images, tests empty/missing/multi-sector files and invalid
names, verifies expected exit codes, physical-page reclamation, scheduler
invariants and real new-syscall counts. It warms the reusable kernel heap before
checking page totals, since the existing heap intentionally retains mapped pages.

```sh
make -B
python3 tools/test_final.py
make run-graphics
```

The final host test runs the existing graphics/live verification and boot suites,
types `run hello` using real emulated PS/2 keys, compares the launch/user PIDs,
checks graphical glyphs, persistent-file bytes and all six canvas colors, checks
live progress and cleanup, and runs `apptest`. It then executes the existing
two-boot storage test and checks the disk hash. Tests exercise QEMU's framebuffer
via QMP screendumps; they do not certify the host GTK window, VMware or hardware.

See [README.md](README.md) for exact launch flags, prerequisites, the complete
shell command list and the short presentation sequence.
