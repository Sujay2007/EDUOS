.intel_syntax noprefix

.section .text

.global user_enter_context
.type user_enter_context, @function

user_enter_context:
    # Parameters (SysV ABI):
    #   RDI = user_rip
    #   RSI = user_rsp
    #   RDX = kernel_rsp0

    # Save kernel caller callee-saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    # Save kernel stack pointer
    mov [rip + kernel_saved_rsp], rsp

    # Preserve user entry coordinates across gdt_set_kernel_stack
    mov r12, rdi            # user_rip
    mov r13, rsi            # user_rsp

    # Set TSS RSP0 for user -> kernel interrupt / syscall transitions
    mov rdi, rdx            # kernel_rsp0
    call gdt_set_kernel_stack

    # Load user data segment selectors (GDT_USER_DATA_RPL3 = 0x18 | 3 = 0x1B)
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    # Construct x86-64 iretq frame on kernel stack:
    # [RSP + 32] User SS (0x1B)
    # [RSP + 24] User RSP
    # [RSP + 16] User RFLAGS (0x202: IF enabled, bit 1 set)
    # [RSP +  8] User CS (0x20 | 3 = 0x23)
    # [RSP +  0] User RIP
    push 0x1B               # User SS
    push r13                # User RSP
    push 0x202              # User RFLAGS
    push 0x23               # User CS
    push r12                # User RIP

    # Clear general-purpose registers so user mode starts in a clean environment
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    # Execute transition to 64-bit Ring 3!
    iretq

.global user_exit_to_kernel
.type user_exit_to_kernel, @function

user_exit_to_kernel:
    # Parameter:
    #   RDI = exit_code

    # Restore saved kernel stack
    mov rsp, [rip + kernel_saved_rsp]

    # Reload kernel data segment selectors (GDT_KERNEL_DATA_SEL = 0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    # Set return value in RAX
    mov rax, rdi

    # Restore kernel callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    # Return to caller of user_enter_context
    ret

# --- Self-Contained User-Mode Test Payloads ---

.global user_test_blob_start
.global user_test_blob_end

user_test_blob_start:
    # 1. Privilege level verification: verify CS RPL is 3
    mov ax, cs
    and ax, 3
    cmp ax, 3
    jne .fail_privilege

    # 2. SYS_WRITE(fd=1, buf=msg, count=20)
    lea rsi, [rip + .msg_hello]
    mov rdi, 1
    mov rdx, 20
    mov rax, 2              # SYS_WRITE
    int 0x80

    cmp rax, 20
    jne .fail_write

    # 3. Invalid system call (rax=999) -> must return -1
    mov rax, 999
    mov rdi, 0
    int 0x80
    cmp rax, -1
    jne .fail_invalid_syscall

    # 4. User pointer validation test: pass kernel address 0xffffffff80000000
    mov rdi, 1
    mov rsi, 0xffffffff80000000
    mov rdx, 16
    mov rax, 2              # SYS_WRITE
    int 0x80
    cmp rax, -2             # Expected: SYSCALL_EFAULT (-2)
    jne .fail_bad_pointer

    # 5. Clean exit: SYS_EXIT(status=0)
    mov rdi, 0              # status = 0 (SUCCESS)
    mov rax, 1              # SYS_EXIT
    int 0x80

    # Safety trap in case sys_exit returned
    ud2

.fail_privilege:
    mov rdi, 101            # Error 101: privilege level check failed
    mov rax, 1
    int 0x80
    ud2

.fail_write:
    mov rdi, 102            # Error 102: sys_write failed
    mov rax, 1
    int 0x80
    ud2

.fail_invalid_syscall:
    mov rdi, 103            # Error 103: invalid syscall returned unexpected value
    mov rax, 1
    int 0x80
    ud2

.fail_bad_pointer:
    mov rdi, 104            # Error 104: bad pointer was not rejected with EFAULT
    mov rax, 1
    int 0x80
    ud2

.msg_hello:
    .ascii "Hello from ring 3!\r\n"

.align 16
user_test_blob_end:

# Privileged instruction test payload (attempts 'cli' in Ring 3 -> triggers #GP)
.global user_fault_cli_blob_start
.global user_fault_cli_blob_end

user_fault_cli_blob_start:
    cli                     # Privileged instruction: must fault with #GP (Vector 13)
    mov rdi, 105            # Error 105: cli did not fault!
    mov rax, 1
    int 0x80
    ud2
user_fault_cli_blob_end:

# Unmapped user memory access test payload (attempts read from 0x200000 -> triggers #PF)
.global user_fault_mem_blob_start
.global user_fault_mem_blob_end

user_fault_mem_blob_start:
    mov rax, 0x0000000000200000
    mov rbx, [rax]          # Access unmapped address: must fault with #PF (Vector 14)
    mov rdi, 106            # Error 106: unmapped read did not fault!
    mov rax, 1
    int 0x80
    ud2
user_fault_mem_blob_end:

.section .bss
.align 8
.global kernel_saved_rsp
kernel_saved_rsp:
    .quad 0
