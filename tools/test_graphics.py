#!/usr/bin/env python3
"""Test framebuffer primitives, then real QEMU display and PS/2 shell input."""
import json
import pathlib
import selectors
import socket
import struct
import subprocess
import tempfile
import time
import zlib

from test_storage import COMMAND, ROOT, require


def png_from_ppm(ppm, png):
    header, dimensions, maximum, pixels = ppm.read_bytes().split(b"\n", 3)
    require(header == b"P6" and maximum == b"255", "unexpected QEMU screendump format")
    width, height = map(int, dimensions.split())
    require(len(pixels) == width * height * 3, "truncated screendump")

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    raw = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png.write_bytes(b"\x89PNG\r\n\x1a\n" +
                    chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    return width, height, pixels


def main(extra_checks=None):
    artifacts = pathlib.Path(tempfile.gettempdir()) / "eduos-graphics"
    artifacts.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eduos-gfx-") as temporary:
        temporary = pathlib.Path(temporary)
        executable = temporary / "graphics-test"
        subprocess.run([
            "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Ikernel", "-Itools/limine",
            "kernel/graphics.c", "kernel/font.c", "tests/graphics_test.c", "-o", str(executable)
        ], cwd=ROOT, check=True)
        subprocess.run([str(executable)], check=True)
        subprocess.run([
            "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Ikernel",
            "kernel/text_buffer.c", "tests/text_buffer_test.c", "-o", str(executable)
        ], cwd=ROOT, check=True)
        subprocess.run([str(executable)], check=True)
        subprocess.run([
            "clang", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Ikernel", "-Itools/limine",
            "kernel/education_model.c", "kernel/education.c", "kernel/lessons.c", "kernel/graphics.c",
            "kernel/font.c", "tests/education_test.c", "-o", str(executable)
        ], cwd=ROOT, check=True)
        subprocess.run([str(executable)], check=True)
        path = temporary / "qmp.sock"
        process = subprocess.Popen(COMMAND + ["-qmp", f"unix:{path},server=on,wait=off"],
                                   cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        output = bytearray()
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        qmp = socket.socket(socket.AF_UNIX)
        try:
            deadline = time.monotonic() + 10
            while True:
                require(process.poll() is None and time.monotonic() < deadline, "QMP startup failed")
                try:
                    qmp.connect(str(path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    # QEMU may create the Unix socket just before listen() is ready.
                    time.sleep(0.02)
            qmp.settimeout(10)
            stream = qmp.makefile("rb")
            require("QMP" in json.loads(stream.readline()), "QMP greeting missing")

            def rpc(execute, arguments=None):
                request = {"execute": execute}
                if arguments is not None:
                    request["arguments"] = arguments
                qmp.sendall(json.dumps(request).encode() + b"\n")
                while True:
                    response = json.loads(stream.readline())
                    if "event" in response:
                        continue
                    require("return" in response, f"QMP error: {response}")
                    return response["return"]

            rpc("qmp_capabilities")

            def receive(start, marker, count=1):
                deadline = time.monotonic() + 45
                while output[start:].count(marker) < count:
                    require(time.monotonic() < deadline, "timeout waiting for shell")
                    for key, _ in selector.select(0.2):
                        data = key.fileobj.read1(8192)
                        require(data, "QEMU exited before prompt")
                        output.extend(data)
                return bytes(output[start:]).replace(b"\r", b"")

            def prompt(start):
                return receive(start, b"edus> ")

            def command(text, wait=True, keyboard=False):
                start = len(output)
                if keyboard:
                    for ch in text:
                        key({" ": "spc", ".": "dot", "-": "minus", "/": "slash"}.get(ch, ch))
                    key("ret")
                else:
                    process.stdin.write(text.encode() + b"\n")
                    process.stdin.flush()
                return prompt(start) if wait else start

            def screenshot(name, settle=0.3):
                # Serial is synchronous; framebuffer output is drained by a worker.
                time.sleep(settle)
                ppm = artifacts / (name + ".ppm")
                png = artifacts / (name + ".png")
                rpc("screendump", {"filename": str(ppm)})
                frame = png_from_ppm(ppm, png)
                print(f"Screenshot: {png}")
                return frame

            def pixel(frame, x, y):
                width, _, pixels = frame
                return pixels[(y * width + x) * 3:(y * width + x + 1) * 3]

            startup = prompt(0)
            for marker in (
                b"All Kernel Multitasking & Scheduler Tests PASSED successfully!",
                b"All User Mode & System Call Tests PASSED successfully!",
                b"All Process Management & Address Space tests PASSED successfully!",
                b"Filesystem tests PASSED.", b"[GFX] Limine framebuffer validated:",
                b"[LIVE TEST] PASS:",
                b"[GFX] Graphical terminal ready; keyboard focus: shell.",
            ):
                require(marker in startup, f"missing result: {marker.decode()}")
            require(b"[FAIL]" not in startup and b"[SKIP]" not in startup,
                    "existing boot suite failure or skipped storage test")
            def key(code):
                rpc("send-key", {"keys": [{"type": "qcode", "data": code}], "hold-time": 40})
                time.sleep(0.06)
            from test_course import check_course
            check_course(command, screenshot, pixel, key)
            first = screenshot("desktop-boot")
            width, height, _ = first
            require(pixel(first, 0, 0) == bytes.fromhex("1e3047") and
                    pixel(first, 0, height // 2) == bytes.fromhex("0a1220"),
                    "desktop colors missing")
            command("clear")
            clean = screenshot("terminal-clear")
            cols = min((width - 64) // 12, 200)
            ox = (width - cols * 12) // 2
            # 'e' at the shell prompt: independent check of glyph pixels and background.
            for x, bits in enumerate((0x38, 0x54, 0x54, 0x54, 0x18)):
                for y in range(7):
                    expected = "67d8b4" if bits & (1 << y) else "101b2b"
                    require(pixel(clean, ox + x * 2, 100 + y * 2) == bytes.fromhex(expected),
                            "graphical prompt/font pixel mismatch")


            start = len(output)
            # Backspace exercises the existing keyboard driver and shell line editing.
            for ch in "cat hellx":
                key("spc" if ch == " " else ch)
            key("backspace")
            for ch in "o.txt":
                key("dot" if ch == "." else ch)
            key("ret")
            result = prompt(start)
            require(b"Hello from persistent virtual-disk storage!" in result,
                    "PS/2 keyboard input/backspace did not execute cat")
            typed = screenshot("desktop-keyboard")
            for x, bits in enumerate((0x7f, 8, 8, 8, 0x7f)):
                for y in range(7):
                    expected = "dce8f5" if bits & (1 << y) else "101b2b"
                    require(pixel(typed, ox + x * 2, 118 + y * 2) == bytes.fromhex(expected),
                            "PS/2 cat output missing from graphical terminal")
            require(b"EDUFS v1: read-only" in command("fsinfo"), "filesystem command regression")
            require(b"TID" in command("threads"), "direct-serial shell output regression")
            command("cat pattern.txt")  # wrap across multiple terminal rows
            command("help all")
            command("help all")  # force scrolling without touching desktop chrome
            scrolled = screenshot("desktop-scroll")
            require(pixel(scrolled, 0, 0) == pixel(first, 0, 0) and
                    pixel(scrolled, 0, height - 1) == pixel(first, 0, height - 1),
                    "scroll damaged desktop chrome")
            from test_terminal import check_terminal
            check_terminal(command, rpc, screenshot, pixel, prompt, receive, key)
            if extra_checks:
                extra_checks(command, rpc, screenshot, pixel, prompt, receive)
            print("QEMU PASS: framebuffer, desktop, graphical prompt, clear, PS/2 input/backspace,")
            print("serial mirroring, persistent-file output, wrapping/scrolling; existing boot suites PASS")
        finally:
            selector.close()
            qmp.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log = artifacts / "boot.log"
            log.write_bytes(output)
            print(f"Boot log: {log}")


if __name__ == "__main__":
    main()
