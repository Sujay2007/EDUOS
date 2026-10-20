#!/usr/bin/env python3
"""Final integration: existing graphics/live checks, apps, disk reboot checks."""
import hashlib
import re
import subprocess
import sys
import time

from test_graphics import main
from test_live import check_live, fields
from test_storage import ROOT, require


def check_applications(command, rpc, screenshot, pixel, prompt, receive):
    check_live(command, rpc, screenshot, pixel, prompt, receive)
    listing = command("apps")
    for name in ("hello", "files", "view", "colors", "check"):
        require(("  " + name + " - ").encode() in listing, f"missing application {name}")
    require(b"Application launch failed" in command("run unknown"), "unknown application accepted")
    require(b"Application launch failed" in command("run hello extra"), "unexpected application argument accepted")
    require(b"Usage: run" in command("run view hello.txt extra"), "multiple arguments accepted")
    command("clear")
    hello = command("run hello", keyboard=True)
    launch = re.search(rb"\[APP\] launch hello PID (\d+)", hello)
    identity = re.search(rb"\[hello\] Ring 3 PID (\d+)", hello)
    require(launch and identity and launch.group(1) == identity.group(1) and
            re.search(rb"\[APP\] exit hello PID \d+ status 0", hello),
            "PS/2 application launch/getpid/exit failed")
    frame = screenshot("apps-keyboard-hello")
    width = frame[0]
    origin = (width - ((width - 64) // 12) * 12) // 2
    # 'R' in '[hello] Ring 3 PID ...', row 2 after launch; actual guest pixels.
    for x, bits in enumerate((0x7f, 9, 0x19, 0x29, 0x46)):
        for y in range(7):
            expected = "dce8f5" if bits & (1 << y) else "101b2b"
            require(pixel(frame, origin + 8 * 12 + x * 2, 136 + y * 2) == bytes.fromhex(expected),
                    "user application output missing from graphical terminal")
    files = command("run files")
    for name in (b"hello.txt", b"empty.txt", b"pattern.txt", b"readme.txt"):
        require(name in files, "user directory application missed a persistent file")
    view = command("run view hello.txt")
    require(b"Hello from persistent virtual-disk storage!" in view and b"[view] EOF" in view,
            "user application did not read persistent file")
    pattern = command("run view pattern.txt")
    require((ROOT / "samples/pattern.txt").read_bytes() in pattern, "user multi-sector reads failed")
    require(b"[view] EOF" in command("run view empty.txt"), "empty-file application failed")
    require(re.search(rb"\[APP\] exit view PID \d+ status 2", command("run view missing.txt")),
            "missing-file application error status missing")
    require(b"[check] user ABI checks passed" in command("run check"), "user ABI safety checks failed")
    before = fields(command("live"))
    token = command("run colors", wait=False)
    receive(token, b"[colors] canvas active (RGB/CMY)")
    canvas = screenshot("apps-colors-active")
    left = width - 192 - 24
    for i, color in enumerate(("ff0000", "00ff00", "0000ff", "00ffff", "ff00ff", "ffff00")):
        require(pixel(canvas, left + i * 32 + 16, 20) == bytes.fromhex(color),
                "queued user canvas drawing did not reach framebuffer")
    require(pixel(canvas, left - 1, 20) == bytes.fromhex("1e3047"), "application drew outside canvas")
    result = prompt(token)
    require(re.search(rb"\[APP\] exit colors PID \d+ status 0", result), "graphics application failed to exit")
    time.sleep(0.4)
    after = fields(command("live"))
    require(after["syscalls"] > before["syscalls"] and after["sample"] > before["sample"] and
            after["last_user_pid"] > before["last_user_pid"] and after["drops"] == 0,
            "applications stalled live state or lost graphical output")
    tests = command("apptest")
    require(b"Application tests PASSED." in tests and b"[APP TEST] FAIL" not in tests and
            b"PASS page reclamation and scheduler invariants." in tests and
            b"PASS real filesystem/canvas/sleep syscall accounting." in tests,
            "application integration suite failed")
    require(b"EDUFS v1: read-only" in command("fsinfo"), "filesystem unavailable after application suite")
    processes = command("ps")
    require(b"hello" not in processes and b"colors" not in processes and b"view" not in processes,
            "terminated application retained in process list")
    command("clear")
    command("apps")
    command("run hello")
    command("run view hello.txt")
    # Let the bounded graphical queue catch up before the final presentation capture.
    time.sleep(0.6)
    screenshot("final-desktop")
    print("APPLICATIONS PASS: PS/2 launch, Ring 3 PID/exit, persistent file listing/reads,")
    print("empty/missing files, user-pointer/EOF/bounds checks, queued canvas pixels,")
    print("live activity, resource cleanup and post-application shell/filesystem use")


if __name__ == "__main__":
    disk = ROOT / "disk.img"
    original = hashlib.sha256(disk.read_bytes()).digest()
    main(check_applications)
    # Preserve and execute the existing two-boot persistent-storage regression.
    subprocess.run([sys.executable, "tools/test_storage.py"], cwd=ROOT, check=True)
    require(hashlib.sha256(disk.read_bytes()).digest() == original,
            "final workflow modified the read-only persistent disk")
    print("FINAL PASS: integrated graphical boot/applications plus existing two-boot storage suite")
