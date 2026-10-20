.intel_syntax noprefix
.section .rodata
.global live_probe_start, live_probe_end, live_probe_stop
/* Diagnostic-only isolated workload. The kernel sets the copied stop byte. */
live_probe_start:
1:
    cmp byte ptr [rip + live_probe_stop], 0
    jne 2f
    mov eax, 3
    int 0x80
    mov eax, 4
    int 0x80
    jmp 1b
2:
    mov eax, 1
    xor edi, edi
    int 0x80
    ud2
live_probe_stop:
    .byte 0
live_probe_end:
.section .note.GNU-stack,"",@progbits
