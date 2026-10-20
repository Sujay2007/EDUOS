#!/usr/bin/env python3
"""Create an EDUFS v1 raw disk once; never overwrite an existing disk."""
import pathlib
import re
import struct
import sys


def main():
    if len(sys.argv) != 3:
        raise ValueError("usage: mkfs.py OUTPUT SAMPLE_DIRECTORY")
    output, source = map(pathlib.Path, sys.argv[1:])
    if output.exists():
        print(f"Preserving existing persistent disk: {output}")
        return
    files = sorted(source.iterdir())
    if len(files) > 16:
        raise ValueError("EDUFS supports at most 16 root files")
    directory = bytearray(1024)
    payload = bytearray()
    for i, path in enumerate(files):
        name = path.name
        if (not path.is_file() or path.is_symlink() or len(name) > 31 or
                not re.fullmatch(r"[a-zA-Z0-9_.-]+", name) or name in (".", "..")):
            raise ValueError(f"invalid root file: {name}")
        data = path.read_bytes()
        start = 3 + len(payload) // 512 if data else 0
        struct.pack_into("<32sQQ16x", directory, i * 64, name.encode("ascii"), start, len(data))
        payload.extend(data)
        payload.extend(bytes((-len(data)) % 512))
    sectors = max(2048, 3 + len(payload) // 512)
    header = struct.pack("<8sIIQQ480x", b"EDUFS001", 1, len(files), sectors, 1)
    # Exclusive creation protects an existing disk even if another build races.
    with output.open("xb") as disk:
        disk.write(header)
        disk.write(directory)
        disk.write(payload)
        disk.truncate(sectors * 512)
    print(f"Created {output}: {len(files)} files, {sectors} sectors")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
