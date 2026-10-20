#!/usr/bin/env python3
"""Run the existing graphics test plus live-state and screenshot comparisons."""
import re
import time

from test_graphics import main
from test_storage import ROOT, require


def fields(output):
    match = re.search(rb"LIVE sample=([^\n]+)", output)
    require(match is not None, "displayed snapshot diagnostic missing")
    return {key.decode(): int(value) for key, value in
            re.findall(rb"([a-z_]+)=(\d+)", b"sample=" + match.group(1))}


def check_live(command, rpc, screenshot, pixel, prompt, receive):
    # Decode actual framebuffer glyphs on the panel, not a host-side mock display.
    font = (ROOT / "kernel/font.c").read_text().split("static const uint8_t font[95][5] = {", 1)[1].split("};", 1)[0]
    glyphs = {}
    for ch, entry in enumerate(re.findall(r"\{([^{}]+)\}", font), 32):
        values = tuple(int(n.strip(), 0) for n in entry.split(","))
        glyphs[values] = chr(ch)

    def decode(frame):
        width, height, _ = frame
        full_cols = (width - 64) // 12
        origin = (width - full_cols * 12) // 2
        sidebar = 384 if width >= 1000 else 320 if width >= 800 else 256
        cols = min((width - 64 - sidebar) // 12, 200)
        left = origin + cols * 12 + 24
        panel_width = width - left - 24
        lines = []
        for line in range((height - 128) // 11):
            text = ""
            for cell in range((panel_width - 19) // 6):
                bits = []
                for x in range(5):
                    mask = 0
                    for y in range(7):
                        color = pixel(frame, left + 12 + cell * 6 + x, 86 + line * 11 + y)
                        if color in (bytes.fromhex("dce8f5"), bytes.fromhex("67d8b4")):
                            mask |= 1 << y
                    bits.append(mask)
                text += glyphs.get(tuple(bits), "?")
            lines.append(text.rstrip())
        return "\n".join(lines)

    time.sleep(0.5)
    first_dump = command("live")
    first = fields(first_dump)
    require(first["usable_pages"] == first["allocated_pages"] + first["free_pages"],
            "allocator snapshot violates conservation")
    memory = command("mem")
    for name, field in (("Usable RAM Pages", "usable_pages"), ("Allocated Pages", "allocated_pages"),
                        ("Free Pages", "free_pages")):
        match = re.search(name.encode() + rb"\s*:\s*(\d+)", memory)
        require(match and int(match.group(1)) == first[field], "live memory differs from allocator shell API")
    # Freeze QEMU after retrieving the last completed frame's snapshot. Retry if
    # the collector started another frame between the serial response and stop.
    matched = False
    for _ in range(8):
        dump = command("live")
        snapshot = fields(dump)
        rpc("stop")
        try:
            lines = decode(screenshot("live-verified"))
        finally:
            rpc("cont")
        sample = re.search(r"Sample #(\d+) @tick (\d+)", lines)
        if sample and int(sample.group(1)) == snapshot["sample"] and int(sample.group(2)) == snapshot["tick"]:
            matched = True
            for label, field in (("Usable", "usable_pages"), ("Allocated", "allocated_pages"), ("Free", "free_pages")):
                require(re.search(label + r"\s+" + str(snapshot[field] * 4) + r"\b", lines),
                        "framebuffer memory value differs from displayed snapshot")
            require(f"Switches {snapshot['switches']} " in lines and
                    f"Syscalls {snapshot['syscalls']} " in lines,
                    "framebuffer activity counters differ from snapshot")
            require("THREADS: TID PID STATE NAME" in lines and "live_view" in lines and
                    "PROCESSES: PID STATE NAME" in lines, "process/thread panel missing")
            break
        time.sleep(0.05)
    require(matched, "could not match framebuffer to published snapshot")
    time.sleep(0.6)
    second = fields(command("live"))
    require(second["sample"] > first["sample"] and second["tick"] > first["tick"] and
            second["switches"] > first["switches"], "idle desktop does not periodically refresh real activity")
    require(second["drops"] == 0, "terminal output queue overflowed")
    # The diagnostic sleeps while an actual process runs, so the worker samples
    # a live, privately mapped address space instead of dereferencing history.
    token = command("livecheck", wait=False)
    receive(token, b"[LIVE CHECK] Isolated probe active for 75 ticks.")
    # The panel is cleared/redrawn with interrupts enabled, not atomically.
    # A single delayed screendump can catch its blank/partial redraw, and the
    # old 0.2s + screenshot's 0.3s delay consumed most of the 75-tick probe.
    # Observe immediately and poll for the same required rendered evidence.
    deadline = time.monotonic() + 5
    probe_screen = ""
    while time.monotonic() < deadline:
        probe_screen = decode(screenshot("live-process-probe", settle=0))
        if "live_probe" in probe_screen and re.search(r"MAPPINGS PID [1-9]\d*", probe_screen):
            break
        time.sleep(0.02)
    require("live_probe" in probe_screen and re.search(r"MAPPINGS PID [1-9]\d*", probe_screen),
            "live isolated process was not displayed")
    result = prompt(token)
    require(b"[LIVE CHECK] PASS displayed allocator change." in result and
            b"[LIVE CHECK] PASS process mappings, user context, syscalls and displayed memory." in result and
            b"[LIVE CHECK] FAIL" not in result, "live allocation/process/syscall diagnostic failed")
    time.sleep(0.5)
    after = fields(command("live"))
    require(after["syscalls"] > second["syscalls"] and after["last_user_pid"] > second["last_user_pid"] and
            after["drops"] == 0, "real user syscalls/context not reflected after workload")
    require(b"hello.txt" in command("ls") and
            b"Hello from persistent virtual-disk storage!" in command("cat hello.txt"),
            "filesystem/shell regression after live workload")
    screenshot("live-desktop-final")
    print("LIVE PASS: framebuffer numbers match snapshots and allocator API; periodic ticks/switches;")
    print("real allocation refresh, isolated process mappings/IDs, effective permissions and syscall activity")


if __name__ == "__main__":
    main(check_live)
