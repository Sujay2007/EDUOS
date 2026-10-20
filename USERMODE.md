# User Mode and System Call Interface Architecture

This document describes the design, implementation, and verification of 64-bit user mode (Ring 3) and the system call interface in the Educational x86-64 Operating System.

---

## 1. Privilege-Level Infrastructure (GDT, TSS, and Assembly Transitions)

### Global Descriptor Table (GDT)
The x86-64 GDT contains dedicated descriptors for kernel mode (Ring 0) and user mode (Ring 3):

| Index | Selector | Description | Access Byte | Granularity | Usage |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | `0x00` | Null Descriptor | `0x00` | `0x00` | Reserved |
| 1 | `0x08` | Kernel Code 64-bit | `0x9A` (Ring 0, Executable, Readable) | `0xA0` (Long mode) | Ring 0 Kernel CS |
| 2 | `0x10` | Kernel Data 64-bit | `0x92` (Ring 0, Writable) | `0xC0` | Ring 0 Kernel DS/SS |
| 3 | `0x18` | User Data 64-bit | `0xF2` (Ring 3, Writable) | `0xC0` | User DS/SS (`0x1B` with RPL 3) |
| 4 | `0x20` | User Code 64-bit | `0xFA` (Ring 3, Executable, Readable) | `0xA0` (Long mode) | User CS (`0x23` with RPL 3) |
| 5 | `0x28` | Task State Segment | `0x89` (64-bit TSS Available) | 16-byte desc | TSS for stack switching |

### Task State Segment (TSS) Stack Switching
When an interrupt, exception, or system call occurs while the CPU is executing in Ring 3 (CPL = 3):
1. The CPU hardware looks up `tss.rsp0`.
2. The hardware automatically switches the stack pointer (`%rsp`) to `tss.rsp0`.
3. The hardware pushes the privilege-transition stack frame:
   - `SS` (User Data Selector: `0x1B`)
   - `RSP` (User Stack Pointer)
   - `RFLAGS` (User Flags)
   - `CS` (User Code Selector: `0x23`)
   - `RIP` (User Instruction Pointer)
4. The kernel function `gdt_set_kernel_stack(uint64_t stack_top)` updates `tss.rsp0` whenever scheduling or entering user mode.

### Assembly Transition to Ring 3 (`user_enter_context`)
The transition from Ring 0 to Ring 3 is performed via `iretq` in `kernel/usermode_entry.S`:
1. Saves caller kernel callee-saved registers (`rbp`, `rbx`, `r12`..`r15`) and kernel stack pointer in `kernel_saved_rsp`.
2. Loads user data segment selectors (`0x1B`) into `ds`, `es`, `fs`, `gs`.
3. Pushes the 64-bit `iretq` frame onto the kernel stack:
   - User SS (`0x1B`)
   - User RSP
   - User RFLAGS (`0x202` with IF=1)
   - User CS (`0x23`)
   - User RIP
4. Clears scratch registers (`rax`..`r15`) to provide a clean execution environment.
5. Executes `iretq`, which switches CPL to 3 and resumes at `user_rip` on `user_rsp`.

---

## 2. User Space Memory & Protection

### Virtual Address Layout
- **User Code**: Mapped at `0x0000000000400000` (4 MiB mark).
- **User Stack**: Mapped at `0x00000000007FF000` (1 page), stack top at `0x0000000000800000` (8 MiB mark, 16-byte aligned).
- **User Boundary**: All user addresses must strictly reside in the lower canonical half: `virt_addr < 0x0000800000000000`.

### VMM User-Page Mapping (`PTE_USER`)
In x86-64 4-level paging, user access (`CPL == 3`) requires that the `PTE_USER` (bit 2) flag be set on **all** levels of the translation hierarchy:
- When `vmm_map_page()` maps a page with `flags & PTE_USER`:
  1. The destination virtual address is asserted to be `< 0x0000800000000000` (preventing user access to higher-half kernel memory).
  2. The intermediate table entries (PML4E, PDPTE, PDE) are tagged with `PTE_USER`.
  3. The leaf PTE is tagged with `flags | PTE_USER | PTE_PRESENT`.

### Strict Pointer Validation (`user_buffer_validate`)
Before dereferencing any user pointer passed to a system call:
1. `ptr != NULL` (if length > 0).
2. Integer overflow check: `start + len >= start`.
3. Upper boundary check: `start < 0x0000800000000000` and `start + len <= 0x0000800000000000`.
4. Page table walkthrough: For every 4 KiB page spanning the buffer, verifies `PTE_PRESENT` and `PTE_USER`. If writing, verifies `PTE_WRITABLE`.
If validation fails, the kernel returns `-2` (`SYSCALL_EFAULT`) without touching invalid or kernel memory.

---

## 3. System Call Interface & ABI

System call entry uses an IDT interrupt gate on Vector `128` (`0x80`) configured with `IDT_ATTR_USER_GATE` (`0xEE`: Present, DPL 3, 64-bit Interrupt Gate). Because DPL is 3, ring 3 code can invoke `int $0x80` without triggering a General Protection Fault.

### Register Calling Convention
- **Syscall Number**: `%rax`
- **Argument 1**: `%rdi`
- **Argument 2**: `%rsi`
- **Argument 3**: `%rdx`
- **Argument 4**: `%r10`
- **Return Value**: `%rax` (`>= 0` on success, negative error code on failure).
- **Preserved Registers**: All other registers (`rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`..`r15`, `rsp`, `rflags`) are preserved across the call.

### Defined System Calls

| Number | Name | Arguments | Description |
| :--- | :--- | :--- | :--- |
| `1` | `SYS_EXIT` | `rdi`: `int status` | Terminates the user task cleanly and returns control to the kernel. |
| `2` | `SYS_WRITE` | `rdi`: `int fd`, `rsi`: `const char *buf`, `rdx`: `size_t count` | Validates buffer and outputs string to serial terminal (stdout=1, stderr=2). |
| `3` | `SYS_GETPID`| None | Returns current task/thread TID. |
| `4` | `SYS_YIELD` | None | Voluntarily yields CPU slice to scheduler. |

### Return Error Codes
- `0`: Success.
- `-1` (`SYSCALL_ENOSYS`): Invalid or unrecognized system call number.
- `-2` (`SYSCALL_EFAULT`): Bad address, kernel-space pointer, or unmapped memory.
- `-3` (`SYSCALL_EINVAL`): Invalid argument (e.g. invalid file descriptor or excessive count).

---

## 4. Verification Suite

The kernel includes an automated 3-phase verification suite executed during boot and callable via the interactive shell command `usertest`:

1. **Test 1: Transition, Privilege Proof, Syscall Execution & Exit**:
   - Drops CPU into Ring 3 with `user_enter_context`.
   - In user code: Reads `%cs` and asserts `(cs & 3) == 3`.
   - In user code: Calls `SYS_WRITE(1, "Hello from ring 3!\r\n", 20)`.
   - In kernel: Verifies `(frame->cs & 3) == 3`, validates user buffer, prints message to COM1 serial console.
   - In user code: Invokes invalid syscall `999` and verifies return value `-1`.
   - In user code: Attempts to pass kernel address `0xFFFFFFFF80000000` to `SYS_WRITE` and verifies rejection with `-2` (`SYSCALL_EFAULT`).
   - In user code: Invokes `SYS_EXIT(0)` and returns to kernel test harness.
   - Kernel verifies exit status 0 and prints `[PASS]`.

2. **Test 2: Hardware Privilege Enforcement**:
   - User program executes `cli` (Clear Interrupts, privileged instruction) at CPL 3.
   - CPU hardware triggers General Protection Fault (`#GP`, Vector 13).
   - Kernel intercepts user fault, logs user RIP, and safely returns to test harness without halting the system.
   - Kernel verifies `#GP` and prints `[PASS]`.

3. **Test 3: Hardware Memory Protection**:
   - User program attempts to read unmapped memory `0x0000000000200000` at CPL 3.
   - CPU hardware triggers Page Fault (`#PF`, Vector 14).
   - Kernel intercepts user fault, logs CR2 (`0x200000`), and returns to test harness.
   - Kernel verifies `#PF` and CR2, and prints `[PASS]`.

---

## 5. Limitations & Future Milestones

- **Single Shared PML4**: Currently, user pages are mapped into the lower half of the active PML4 table and cleaned up after execution. Future milestones will introduce isolated, per-process PML4 page tables and an ELF loader.
- **Interrupt Gate vs SYSCALL/SYSRET**: System calls currently use `int $0x80` with an interrupt gate. Future optimization can add MSR-based `SYSCALL`/`SYSRETQ` fast paths.
