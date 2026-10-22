"""Focused real-framebuffer scrollback and pager checks, reused by final tests."""
import re
import time

from test_storage import ROOT, require


def check_terminal(command, rpc, screenshot, pixel, prompt, receive, key):
    font = (ROOT / "kernel/font.c").read_text().split("static const uint8_t font[95][5] = {", 1)[1].split("};", 1)[0]
    glyphs = {tuple(int(n.strip(), 0) for n in row.split(",")): chr(ch)
              for ch, row in enumerate(re.findall(r"\{([^{}]+)\}", font), 32)}

    def read(frame, x, y, count, scale=2):
        result = ""
        for cell in range(count):
            columns = []
            for col in range(5):
                mask = 0
                for row in range(7):
                    if pixel(frame, x + (cell * 6 + col) * scale, y + row * scale) in (
                            bytes.fromhex("dce8f5"), bytes.fromhex("67d8b4"), bytes.fromhex("a7bbcf")):
                        mask |= 1 << row
                columns.append(mask)
            result += glyphs.get(tuple(columns), "?")
        return result.rstrip()

    def geometry(frame):
        width = frame[0]
        sidebar = 320 if width >= 1200 else 288 if width >= 1000 else 256 if width >= 800 else 208
        cols = min((width - 64 - sidebar) // 12, 200)
        origin = (width - ((width - 64) // 12) * 12) // 2
        return origin, cols

    def top(frame):
        origin, cols = geometry(frame)
        return read(frame, origin, 100, cols)

    def status(frame):
        origin, cols = geometry(frame)
        area = min(cols * 12 - 132, 246)
        return read(frame, origin + cols * 12 - area, 69, area // 6, 1)

    command("clear")
    command("about")
    command("help all")
    command("help all")
    # Serial replies precede the bounded framebuffer worker. Wait for the
    # final prompt to reach its bottom row before sending navigation events.
    deadline = time.monotonic() + 5
    while True:
        live = screenshot("terminal-history-live")
        origin, cols = geometry(live)
        rows = min((live[1] - 144) // 18, 100)
        if read(live, origin, 100 + (rows - 1) * 18, cols) == "edus>":
            break
        require(time.monotonic() < deadline, "graphical output did not catch up to shell prompt")
    require("LIVE" in status(live) and "edus> about" not in top(live), "output did not automatically scroll")
    key("pgup")
    page = screenshot("terminal-page-up")
    require("HISTORY" in status(page) and top(page) != top(live), "Page Up did not reveal history")
    key("home")
    oldest = screenshot("terminal-oldest")
    require(top(oldest) == "edus> about", "old output was discarded instead of preserved")
    key("down")
    down = screenshot("terminal-arrow-down")
    require(top(down) != top(oldest), "Down did not move one history line")
    key("up")
    require(top(screenshot("terminal-arrow-up")) == top(oldest), "Up did not restore the prior line")
    key("pgdn")
    forward = screenshot("terminal-page-down")
    require(top(forward) != top(oldest), "Page Down did not advance through history")
    key("end")
    require("LIVE" in status(screenshot("terminal-end")), "End did not return to live output")
    key("pgup")
    require(b"System Timer Ticks:" in command("ticks", keyboard=True), "typing after history did not execute a command")
    require("LIVE" in status(screenshot("terminal-typing-live")), "typing did not restore live viewport")
    command("clear")
    key("pgup")
    cleared = screenshot("terminal-history-cleared")
    require(top(cleared) == "edus>" and "LIVE" in status(cleared), "clear retained stale history or lost prompt")

    token = command("help", wait=False)
    first = receive(token, b"-- More --")
    require(b"reboot" not in first, "help printed its last page before continue")
    page_frame = screenshot("help-first-page")
    origin, cols = geometry(page_frame)
    rows = min((page_frame[1] - 144) // 18, 100)
    visible = "\n".join(read(page_frame, origin, 100 + row * 18, cols) for row in range(rows))
    require("-- More --" in visible, "help continuation prompt was not displayed")
    key("tab")
    course = screenshot("help-course-toggle")
    require(pixel(course, 0, 60) == bytes.fromhex("f3f0e8"), "help pager prevented course navigation")
    key("tab")
    screenshot("help-tools-return")
    key("ret")
    receive(token, b"-- More --", count=2)
    key("q")
    quit_result = prompt(token)
    require(b"Help closed." in quit_result and b"reboot" not in quit_result, "help quit did not stop early")
    require(b"EDUFS v1: read-only" in command("fsinfo"), "pager left shell input unusable")
    token = command("help", wait=False)
    receive(token, b"-- More --")
    key("pgdn")
    complete = prompt(token)
    require(b"proctest" in complete and b"reboot" in complete, "paged help did not deliver the remaining commands")
    screenshot("help-complete")
    command("clear")
    print("TERMINAL PASS: preserved scrollback, PgUp/PgDn/Home/End/arrows, typing, clear,")
    print("paginated help, Enter single-line continuation, Page Down continuation and early quit")
