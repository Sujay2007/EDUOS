# Task 8: framebuffer graphics and graphical shell

Limine's framebuffer request supplies the existing kernel mapping. No alternate
boot path or memory-manager changes are required. The kernel requests the
framebuffer alongside its existing memory-map and HHDM requests. The framebuffer
region remains reserved by the physical allocator. Process page tables continue
sharing the supervisor-only higher half, including this mapping.

The renderer validates the address, dimensions, pitch, span arithmetic, packed RGB
memory model, bit depth, nonzero channel masks and nonoverlapping channel fields.
It supports 24/32-bit pixels with 1–8 bits per RGB channel and converts 0xRRGGBB
colors into the provided mask layout. Unsupported responses leave serial output
available with a specific diagnostic. The graphical terminal requires at least
640x400. Limine selects the video mode; no hard-coded pitch or address is used.

Graphics primitives provide clipped pixel, rectangle and clear operations.
They preserve row padding and handle negative coordinates and extreme extents
without overflowing coordinate arithmetic. A built-in 5x7 printable-ASCII font
supports scaled rendering and a question-mark fallback.

The graphical terminal starts after the existing boot tests. It mirrors the
serial character output path so existing diagnostics and commands that directly
use serial output also appear graphically. It supports CR/LF, wrapping,
scrolling, tabs, destructive backspace, a steady cursor, and the existing clear
command's ANSI clear/home sequences. It is not a full ANSI/Unicode terminal.
Character updates are serialized against interrupts on the existing single CPU.
The terminal has a bounded grid (up to 200 columns by 100 rows), with no heap
allocation. Serial remains available when the QEMU display is hidden.

A minimal keyboard-operated desktop surrounds one permanently focused terminal
with a title bar and footer. The shell and keyboard driver are unchanged. There
is no mouse driver, movable windows, compositor, launcher, OS visualization or
application integration. Pointer input is deferred: the current driver supports
keyboard input only, and mouse support would require an additional PS/2 protocol
and event path. `clear` clears terminal contents while preserving desktop chrome.

## Build and run

```sh
make -B
make run-graphics
```

`make run` retains the existing headless serial configuration. `run-graphics`
uses GTK (requires a display server, such as WSLg) with exactly:

```sh
qemu-system-x86_64 -M q35 -m 256M \
  -cdrom educational-os.iso -boot d -serial stdio -display gtk \
  -device piix3-ide,id=eduide \
  -drive if=none,id=edudisk,file=disk.img,format=raw \
  -device ide-hd,drive=edudisk,bus=eduide.0
```

The persistent disk is preserved by the existing image creation policy.

## Verification

```sh
make test-graphics
# or, after building:
python3 tools/test_graphics.py
```

The test compiles the production primitive/font sources for the host and checks
pitch/padding, RGB masks, 24/32-bit formats, invalid metadata, clipping, extreme
coordinates, and glyph memory guards. It then boots QEMU once using the existing
storage test's command (`-display none`), adding a temporary QMP socket for
framebuffer screenshots and actual PS/2 keyboard injection. Hiding the host GUI
does not disable the emulated framebuffer.

The QEMU check requires the existing thread, user-mode, process and filesystem
suites to pass, verifies actual graphical glyph pixels and desktop colors,
exercises keyboard/backspace/file output, serial commands, clear, wrapping and
scrolling, and saves PNG screenshots and a serial log under
`/tmp/eduos-graphics/`. No host imaging libraries are required. The graphical
display is verified through screendumps, independently of serial success.
