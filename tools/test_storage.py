#!/usr/bin/env python3
"""Boot the built kernel twice against the same disk; check suites and shell I/O."""
import hashlib
import pathlib
import selectors
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
COMMAND = [
    "qemu-system-x86_64", "-M", "q35", "-m", "256M",
    "-cdrom", "educational-os.iso", "-boot", "d", "-serial", "stdio",
    "-display", "none", "-monitor", "none",
    "-device", "piix3-ide,id=eduide",
    "-drive", "if=none,id=edudisk,file=disk.img,format=raw",
    "-device", "ide-hd,drive=edudisk,bus=eduide.0",
]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def boot(number):
    output = bytearray()
    process = subprocess.Popen(COMMAND, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    def prompt(start):
        deadline = time.monotonic() + 40
        while b"edus> " not in output[start:]:
            require(time.monotonic() < deadline, "timeout waiting for shell")
            for key, _ in selector.select(0.2):
                data = key.fileobj.read1(8192)
                require(data, "QEMU exited before shell prompt")
                output.extend(data)
        return bytes(output[start:]).replace(b"\r", b"")

    def command(text):
        start = len(output)
        process.stdin.write(text.encode() + b"\n")
        process.stdin.flush()
        return prompt(start)

    try:
        startup = prompt(0)
        for marker in (
            b"All Kernel Multitasking & Scheduler Tests PASSED successfully!",
            b"All User Mode & System Call Tests PASSED successfully!",
            b"All Process Management & Address Space tests PASSED successfully!",
            b"[FS] Mounted EDUFS v1 read-only",
            b"Filesystem tests PASSED.",
            b"[PASS] Read real hello.txt contents from persistent disk",
        ):
            require(marker in startup, f"missing result: {marker.decode()}")
        require(b"[FAIL]" not in startup and b"[SKIP]" not in startup,
                "kernel suite reported failure or skipped storage checks")
        require(b"EDUFS v1: read-only" in command("fsinfo"), "fsinfo failed")
        listing = command("ls")
        for name in (b"hello.txt", b"empty.txt", b"pattern.txt", b"readme.txt"):
            require(name in listing, "ls missing sample file")
        require(b"Hello from persistent virtual-disk storage!\n" in command("cat hello.txt"),
                "cat hello.txt failed")
        require(command("cat empty.txt") == b"cat empty.txt\nedus> ", "empty cat failed")
        pattern = (ROOT / "samples/pattern.txt").read_bytes()
        require(pattern in command("cat pattern.txt"), "multi-sector cat failed")
        require(b"This file lives on the attached raw virtual disk" in command("cat readme.txt"),
                "cat readme.txt failed")
        require(b"File not found." in command("cat missing.txt"), "missing file error failed")
        require(b"Invalid filename." in command("cat ../hello.txt"), "invalid filename accepted")
        require(b"proctest" in command("help all"), "existing shell command missing")
        print(f"Boot {number}: existing suites, filesystem suite, ls/cat/fsinfo PASS")
    finally:
        selector.close()
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        # Keep diagnostic output outside the repository, including on failures.
        import tempfile
        log = pathlib.Path(tempfile.gettempdir()) / f"eduos-storage-boot-{number}.log"
        log.write_bytes(output)
        print(f"Boot log: {log}")


if __name__ == "__main__":
    before = hashlib.sha256((ROOT / "disk.img").read_bytes()).digest()
    boot(1)
    boot(2)
    require(hashlib.sha256((ROOT / "disk.img").read_bytes()).digest() == before,
            "read-only boot modified disk contents")
    print("Persistent disk: contents survived both boots unchanged (SHA-256 PASS)")
