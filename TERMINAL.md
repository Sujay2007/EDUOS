# Terminal controls and validation

The graphical terminal retains the latest 512 physical text lines, including
wrapped lines, in a fixed-size ring (about 100 KiB). New output follows the
bottom normally. While browsing history, the viewport stays anchored as output
arrives, until its lines expire from the ring. The header shows LIVE or HISTORY
and the distance from current output. Rendering remains in the existing worker;
keyboard/serial interrupt handlers only enqueue input.

- Page Up / Page Down: move by a screen, retaining one overlapping line.
- Up / Down: move by one line.
- Home: oldest retained line. End: current output.
- Typing, Enter, or Backspace returns to current output before editing.
- `clear` clears both the visible terminal and retained history.
- `help`: wrapped, grouped help with at most 20 lines per page (fewer on small screens).
- At the help prompt: Space or Page Down continues a page; Enter continues one
  line; q quits. PS/2 Escape also quits. History navigation remains available.
- `help all`: prints all help without pausing, useful over serial or in scripts.

PS/2 extended navigation keys and common serial ANSI navigation sequences are
supported. There is no mouse driver, so mouse-wheel scrolling is unavailable.
Arrow keys browse output; they do not edit the command within a line.
The live panel is narrower, help descriptions wrap to terminal width, prompts
use the accent color, and header/footer controls use compact text.

Build and graphical launch (inside the project under Linux/WSL):

```sh
make -B
python3 tools/test_final.py
make run-graphics
```

The final test includes host buffer checks for retention, anchoring, eviction,
bounds, wrapping, backspace and clearing. QEMU framebuffer checks exercise
Page Up/Down, arrows, Home/End, typing after navigation, help pagination,
single-line continuation, early quit and subsequent shell/filesystem use.
They wait for the graphical prompt before navigating long output because
serial output completes before the bounded graphical worker drains its queue.
Existing graphics, process/live-state, application and two-boot persistent
storage assertions remain enabled.
