.intel_syntax noprefix
.section .text

# --- Process Test Blobs ---

# 1. Process Alpha Payload (PID 1)
.global proc_alpha_blob_start
.global proc_alpha_blob_end

proc_alpha_blob_start:
    # Print start message via SYS_WRITE (rax=2, rdi=1, rsi=msg, rdx=len)
    lea rsi, [rip + .msg_alpha_1]
    mov rdi, 1
    mov rdx, 58
    mov rax, 2              # SYS_WRITE
    int 0x80

    # Get PID via SYS_GETPID (rax=3)
    mov rax, 3              # SYS_GETPID
    int 0x80

    # Voluntarily yield CPU via SYS_YIELD (rax=4)
    mov rax, 4              # SYS_YIELD
    int 0x80

    # Print resumed message via SYS_WRITE
    lea rsi, [rip + .msg_alpha_2]
    mov rdi, 1
    mov rdx, 65
    mov rax, 2              # SYS_WRITE
    int 0x80

    # Terminate process with status 42 via SYS_EXIT (rax=1, rdi=42)
    mov rdi, 42
    mov rax, 1              # SYS_EXIT
    int 0x80
    ud2

.msg_alpha_1:
    .ascii "  [Process Alpha (PID 1)] Running in user address space!\r\n"
.msg_alpha_2:
    .ascii "  [Process Alpha (PID 1)] Resumed after yield, exiting status 42.\r\n"

.align 16
proc_alpha_blob_end:

# 2. Process Beta Payload (PID 2)
.global proc_beta_blob_start
.global proc_beta_blob_end

proc_beta_blob_start:
    # Print start message via SYS_WRITE (rax=2, rdi=1, rsi=msg, rdx=len)
    lea rsi, [rip + .msg_beta_1]
    mov rdi, 1
    mov rdx, 57
    mov rax, 2              # SYS_WRITE
    int 0x80

    # Get PID via SYS_GETPID (rax=3)
    mov rax, 3              # SYS_GETPID
    int 0x80

    # Voluntarily yield CPU via SYS_YIELD (rax=4)
    mov rax, 4              # SYS_YIELD
    int 0x80

    # Print resumed message via SYS_WRITE
    lea rsi, [rip + .msg_beta_2]
    mov rdi, 1
    mov rdx, 64
    mov rax, 2              # SYS_WRITE
    int 0x80

    # Terminate process with status 84 via SYS_EXIT (rax=1, rdi=84)
    mov rdi, 84
    mov rax, 1              # SYS_EXIT
    int 0x80
    ud2

.msg_beta_1:
    .ascii "  [Process Beta (PID 2)] Running in user address space!\r\n"
.msg_beta_2:
    .ascii "  [Process Beta (PID 2)] Resumed after yield, exiting status 84.\r\n"

.align 16
proc_beta_blob_end:

# 3. Supervisor Memory Access Violation Payload (Attempts read from 0xFFFFFFFF80000000)
.global proc_sup_violation_blob_start
.global proc_sup_violation_blob_end

proc_sup_violation_blob_start:
    mov rax, 0xffffffff80000000
    mov rbx, [rax]          # Access present supervisor page -> triggers #PF (Vector 14)
    # If protection failed to trigger exception, exit with error 99
    mov rdi, 99
    mov rax, 1
    int 0x80
    ud2
proc_sup_violation_blob_end:

# 4. System Call Bad User Pointer Payload (Passes kernel pointer to SYS_WRITE)
.global proc_syscall_badptr_blob_start
.global proc_syscall_badptr_blob_end

proc_syscall_badptr_blob_start:
    mov rdi, 1
    mov rsi, 0xffffffff80000000
    mov rdx, 16
    mov rax, 2              # SYS_WRITE
    int 0x80
    # RAX must return SYSCALL_EFAULT (-2)
    cmp rax, -2
    jne .badptr_fail
    mov rdi, 0              # Success
    mov rax, 1              # SYS_EXIT
    int 0x80
    ud2
.badptr_fail:
    mov rdi, 98             # Failure
    mov rax, 1
    int 0x80
    ud2
proc_syscall_badptr_blob_end:

# 5. Fault Containment Faulting Process (Executes privileged instruction cli -> triggers #GP)
.global proc_fault_cli_blob_start
.global proc_fault_cli_blob_end

proc_fault_cli_blob_start:
    cli                     # Privileged instruction -> triggers #GP (Vector 13)
    mov rdi, 97
    mov rax, 1
    int 0x80
    ud2
proc_fault_cli_blob_end:

# 6. Fault Containment Safe Companion Process (Runs safely and exits with status 77)
.global proc_safe_companion_blob_start
.global proc_safe_companion_blob_end

proc_safe_companion_blob_start:
    lea rsi, [rip + .msg_safe]
    mov rdi, 1
    mov rdx, 58
    mov rax, 2              # SYS_WRITE
    int 0x80
    mov rdi, 77             # Clean exit status 77
    mov rax, 1              # SYS_EXIT
    int 0x80
    ud2
.msg_safe:
    .ascii "  [Process Companion] Safe process ran and exited cleanly!\r\n"
.align 16
proc_safe_companion_blob_end:
