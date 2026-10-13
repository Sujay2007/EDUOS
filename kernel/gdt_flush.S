.intel_syntax noprefix
.global gdt_flush
.type gdt_flush, @function

# void gdt_flush(uint64_t gdt_ptr, uint16_t cs, uint16_t ds, uint16_t tss_sel);
# RDI = gdt_ptr pointer
# RSI = kernel CS (0x08)
# RDX = kernel DS (0x10)
# RCX = TSS selector (0x28)

gdt_flush:
    lgdt [rdi]

    # Reload data segment registers
    mov ds, dx
    mov es, dx
    mov ss, dx
    mov fs, dx
    mov gs, dx

    # Far return to reload CS
    push rsi                # Push new CS (0x08)
    lea rax, [rip + .reload_cs]
    push rax                # Push target RIP
    retfq                   # Far return reloads CS:RIP

.reload_cs:
    # Load Task Register
    ltr cx
    ret
