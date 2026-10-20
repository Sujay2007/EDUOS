.intel_syntax noprefix

# Macro for exceptions that push an error code automatically
.macro ISR_ERRCODE vec
.global isr_stub_\vec
.type isr_stub_\vec, @function
isr_stub_\vec:
    push \vec               # vector number
    jmp isr_common_stub
.endm

# Macro for exceptions / interrupts that do NOT push an error code
.macro ISR_NOERRCODE vec
.global isr_stub_\vec
.type isr_stub_\vec, @function
isr_stub_\vec:
    push 0                  # dummy error code
    push \vec               # vector number
    jmp isr_common_stub
.endm

# CPU Exceptions (0..31)
ISR_NOERRCODE 0   # Divide Error
ISR_NOERRCODE 1   # Debug
ISR_NOERRCODE 2   # NMI
ISR_NOERRCODE 3   # Breakpoint
ISR_NOERRCODE 4   # Overflow
ISR_NOERRCODE 5   # BOUND Range Exceeded
ISR_NOERRCODE 6   # Invalid Opcode
ISR_NOERRCODE 7   # Device Not Available (No Math Coprocessor)
ISR_ERRCODE   8   # Double Fault
ISR_NOERRCODE 9   # Coprocessor Segment Overrun
ISR_ERRCODE   10  # Invalid TSS
ISR_ERRCODE   11  # Segment Not Present
ISR_ERRCODE   12  # Stack-Segment Fault
ISR_ERRCODE   13  # General Protection Fault
ISR_ERRCODE   14  # Page Fault
ISR_NOERRCODE 15  # Reserved
ISR_NOERRCODE 16  # x87 FPU Floating-Point Error
ISR_ERRCODE   17  # Alignment Check
ISR_NOERRCODE 18  # Machine Check
ISR_NOERRCODE 19  # SIMD Floating-Point Exception
ISR_NOERRCODE 20  # Virtualization Exception
ISR_ERRCODE   21  # Control Protection Exception
ISR_NOERRCODE 22  # Reserved
ISR_NOERRCODE 23  # Reserved
ISR_NOERRCODE 24  # Reserved
ISR_NOERRCODE 25  # Reserved
ISR_NOERRCODE 26  # Reserved
ISR_NOERRCODE 27  # Reserved
ISR_NOERRCODE 28  # Hypervisor Injection Exception
ISR_ERRCODE   29  # VMM Communication Exception
ISR_ERRCODE   30  # Security Exception
ISR_NOERRCODE 31  # Reserved

# Hardware IRQs (32..47)
ISR_NOERRCODE 32  # IRQ0: Timer
ISR_NOERRCODE 33  # IRQ1: Keyboard
ISR_NOERRCODE 34  # IRQ2: Cascade
ISR_NOERRCODE 35  # IRQ3: COM2
ISR_NOERRCODE 36  # IRQ4: COM1
ISR_NOERRCODE 37  # IRQ5: LPT2
ISR_NOERRCODE 38  # IRQ6: Floppy
ISR_NOERRCODE 39  # IRQ7: Spurious / LPT1
ISR_NOERRCODE 40  # IRQ8: RTC
ISR_NOERRCODE 41  # IRQ9: Free
ISR_NOERRCODE 42  # IRQ10: Free
ISR_NOERRCODE 43  # IRQ11: Free
ISR_NOERRCODE 44  # IRQ12: Mouse
ISR_NOERRCODE 45  # IRQ13: FPU
ISR_NOERRCODE 46  # IRQ14: Primary ATA
ISR_NOERRCODE 47  # IRQ15: Secondary ATA
ISR_NOERRCODE 48  # Vector 48: Scheduler Yield / Software Interrupt
ISR_NOERRCODE 128 # Vector 128 (0x80): System Call Entry

# Common ISR stub
.extern isr_dispatch
isr_common_stub:
    # Save all general purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    # Pass pointer to interrupt_frame in RDI (SysV ABI 1st parameter)
    mov rdi, rsp
    cld                     # Direction flag cleared per SysV ABI
    call isr_dispatch
    mov rsp, rax            # Switch stack pointer if isr_dispatch returned new thread frame

    # Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    # Clean up vector and error code from stack
    add rsp, 16

    iretq
