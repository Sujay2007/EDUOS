# Task 7: read-only filesystem and persistent storage

Build in the repository under Linux/WSL:

```sh
make -B
make run
```

`make -B` avoids stale tracked object files from earlier milestones. The build
creates `disk.img` from `samples/` only when the image does not already exist.
Even a forced build preserves an existing disk. `make clean` also preserves it.
To create a different disk without overwriting existing data:

```sh
python3 tools/mkfs.py new-disk.img samples
```

To refresh samples on the default disk, first move the existing disk to a backup
of your choice, then run `make`. Changes to sample files do not silently replace
persistent storage. The kernel has no disk-write interface.

Exact default boot command (the boot ISO remains on Q35's default controller):

```sh
qemu-system-x86_64 -M q35 -m 256M -cdrom educational-os.iso -boot d -serial stdio -display none \
  -device piix3-ide,id=eduide \
  -drive if=none,id=edudisk,file=disk.img,format=raw \
  -device ide-hd,drive=edudisk,bus=eduide.0
```

The explicit PIIX IDE controller supplies legacy primary-master ports. The driver
uses polled ATA IDENTIFY and LBA28 READ SECTORS with bounded polling, ATA error
checks, capacity checks and sector transactions serialized against preemption.
It supports 512-byte logical sectors, up to 128 GiB, and no DMA, AHCI, writes,
partitions, or other device/controller discovery. A missing or invalid disk
leaves the existing shell available and filesystem commands report unavailable.

## Interfaces

`block_device` describes capacity in 512-byte sectors and a read callback.
`block_read` validates ranges before invoking it. The EDUFS parser depends only
on this interface; the tests also supply an in-memory device.

`fs_mount`, `fs_lookup`, `fs_list`, and `fs_read` provide a validated directory
snapshot, indexed enumeration, lookup and bounded byte reads. Reads report the
actual byte count; offsets at or beyond EOF succeed with zero bytes. Errors are
distinct for invalid arguments, I/O, corrupt metadata, absent files and an
unmounted filesystem. Buffers and mount objects belong to callers; no heap
allocation is needed. If an I/O error follows a partial read, `actual` records
the bytes already copied. Mounted metadata is immutable; do not modify the image
from the host while it is mounted.

Shell commands: `ls`, `cat <name>`, `fsinfo`. Only the root directory exists.
Names are 1–31 ASCII letters, digits, underscores, hyphens or dots; `.` and `..`
are invalid. Slashes, spaces and unterminated/overlong names are rejected.

## EDUFS v1 disk format

EDUFS is a deliberately small educational format, not FAT or ISO9660. All
integers are unsigned little-endian, decoded explicitly rather than through
unaligned structure casts. There is no partition table.

| Location | Contents |
| --- | --- |
| Sector 0, bytes 0–7 | Magic `EDUFS001` |
| Bytes 8–11 | Version, 1 |
| Bytes 12–15 | Root file count, 0–16 |
| Bytes 16–23 | Filesystem size in sectors, at least 3 and at most device capacity |
| Bytes 24–31 | Directory start sector, exactly 1 |
| Bytes 32–511 | Reserved, zero |
| Sectors 1–2 | Up to sixteen 64-byte root entries |
| Sectors 3 onward | Sector-aligned contiguous file extents |

Each active directory entry contains a zero-padded 32-byte name, 64-bit start
sector, 64-bit byte length, and sixteen reserved zero bytes. Empty files have
start sector zero. Unused directory slots are ignored. Mount rejects duplicate
names, malformed names, bad padding/reserved fields, extents overlapping metadata
or other files, and out-of-device extents. Extent validation uses subtraction and
division to avoid overflow. Corruption detection is structural, not a checksum
or authentication guarantee. File count and flat directory structure are fixed
limits appropriate to this milestone.

## Tests

The boot-time filesystem suite tests lookup, complete directory enumeration,
empty directories/files, invalid/long names, bounded cross-sector reads with
canaries, short reads, EOF and huge offsets, corrupt superblocks, duplicate names,
overlapping/out-of-range extents, I/O failure and unavailable mounts. Persistent
disk checks read the sample files via the real ATA driver; these are explicitly
skipped if no disk mounts. The sample-specific checks expect the supplied image.

After building, run:

```sh
python3 tools/test_storage.py
```

This boots twice against the same persistent image, requires the existing
thread/user-mode/process suites and the new filesystem suite to report success,
checks the shell commands and sample contents, and verifies that the disk hash
is unchanged. It writes boot logs to the host temporary directory and fails on
a timeout, reported suite failure, or skipped persistent checks.
