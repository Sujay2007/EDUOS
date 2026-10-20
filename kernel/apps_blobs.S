.intel_syntax noprefix
.section .rodata
.macro PRINT begin, end
    mov eax, 2
    mov edi, 1
    lea rsi, [rip + \begin]
    lea rdx, [rip + \end]
    sub rdx, rsi
    int 0x80
.endm
.macro EXIT status
    mov eax, 1
    mov edi, \status
    int 0x80
    ud2
.endm

.global app_hello_start, app_hello_end
app_hello_start:
    sub rsp, 64
    xor eax, eax
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne .Lhello_fail
    mov eax, 3
    int 0x80
    test rax, rax
    jle .Lhello_fail
    mov r12, rax
    PRINT .Lhello_prefix, .Lhello_prefix_end
    mov rax, r12
    lea rsi, [rsp + 63]
    mov byte ptr [rsi], 10
    mov ecx, 1
    mov ebx, 10
.Lhello_digit:
    xor edx, edx
    div rbx
    dec rsi
    add dl, '0'
    mov byte ptr [rsi], dl
    inc ecx
    test rax, rax
    jnz .Lhello_digit
    mov edx, ecx
    mov eax, 2
    mov edi, 1
    int 0x80
    PRINT .Lhello_message, .Lhello_message_end
    EXIT 0
.Lhello_fail:
    EXIT 1
.Lhello_prefix: .ascii "[hello] Ring 3 PID "
.Lhello_prefix_end:
.Lhello_message: .ascii "[hello] Isolated process and syscall output are working.\n"
.Lhello_message_end:
app_hello_end:

.global app_files_start, app_files_end
app_files_start:
    sub rsp, 64
    xor ebx, ebx
    PRINT .Lfiles_title, .Lfiles_title_end
.Lfiles_next:
    mov eax, 6
    mov rdi, rbx
    mov rsi, rsp
    int 0x80
    cmp rax, -4
    je .Lfiles_done
    test rax, rax
    js .Lfiles_fail
    xor ecx, ecx
.Lfiles_length:
    cmp ecx, 32
    jae .Lfiles_fail
    cmp byte ptr [rsp + rcx], 0
    je .Lfiles_name
    inc ecx
    jmp .Lfiles_length
.Lfiles_name:
    mov eax, 2
    mov edi, 1
    mov rsi, rsp
    mov edx, ecx
    int 0x80
    PRINT .Lfiles_space, .Lfiles_space_end
    mov rax, qword ptr [rsp + 32]
    lea rsi, [rsp + 63]
    mov byte ptr [rsi], 10
    mov ecx, 1
    mov r8d, 10
.Lfiles_digit:
    xor edx, edx
    div r8
    dec rsi
    add dl, '0'
    mov byte ptr [rsi], dl
    inc ecx
    test rax, rax
    jnz .Lfiles_digit
    mov eax, 2
    mov edi, 1
    mov edx, ecx
    int 0x80
    inc ebx
    jmp .Lfiles_next
.Lfiles_done:
    EXIT 0
.Lfiles_fail:
    PRINT .Lfiles_error, .Lfiles_error_end
    EXIT 2
.Lfiles_title: .ascii "[files] Persistent root directory (name / bytes):\n"
.Lfiles_title_end:
.Lfiles_space: .ascii "  "
.Lfiles_space_end:
.Lfiles_error: .ascii "[files] Directory unavailable.\n"
.Lfiles_error_end:
app_files_end:

.global app_view_start, app_view_end
app_view_start:
    mov r12, rdi
    sub rsp, 256
    xor r13d, r13d
    PRINT .Lview_title, .Lview_title_end
.Lview_next:
    cmp r13, 16384
    jae .Lview_limit
    mov eax, 5
    mov rdi, r12
    mov rsi, rsp
    mov edx, 256
    mov r10, r13
    int 0x80
    test rax, rax
    js .Lview_fail
    jz .Lview_done
    mov r14, rax
    mov edx, eax
    mov eax, 2
    mov edi, 1
    mov rsi, rsp
    int 0x80
    test rax, rax
    js .Lview_fail
    add r13, r14
    mov eax, 4
    int 0x80
    jmp .Lview_next
.Lview_done:
    PRINT .Lview_eof, .Lview_eof_end
    EXIT 0
.Lview_limit:
    PRINT .Lview_truncated, .Lview_truncated_end
    EXIT 0
.Lview_fail:
    PRINT .Lview_error, .Lview_error_end
    EXIT 2
.Lview_title: .ascii "[view] Reading persistent storage through SYS_READFILE:\n"
.Lview_title_end:
.Lview_eof: .ascii "\n[view] EOF\n"
.Lview_eof_end:
.Lview_truncated: .ascii "\n[view] Stopped at the 16 KiB display limit.\n"
.Lview_truncated_end:
.Lview_error: .ascii "[view] File unavailable or invalid name.\n"
.Lview_error_end:
app_view_end:

.global app_colors_start, app_colors_end
app_colors_start:
    lea r12, [rip + .Lcolors_rects]
    mov r13d, 6
.Lcolors_next:
    mov eax, 7
    mov rdi, r12
    int 0x80
    cmp rax, -7
    jne .Lcolors_result
    mov eax, 8
    mov edi, 1
    int 0x80
    jmp .Lcolors_next
.Lcolors_result:
    test rax, rax
    js .Lcolors_fail
    add r12, 20
    dec r13d
    jnz .Lcolors_next
    PRINT .Lcolors_active, .Lcolors_active_end
    mov eax, 8
    mov edi, 150
    int 0x80
    PRINT .Lcolors_done, .Lcolors_done_end
    EXIT 0
.Lcolors_fail:
    PRINT .Lcolors_error, .Lcolors_error_end
    EXIT 3
.Lcolors_rects:
    .long 0, 0, 32, 24, 0xff0000
    .long 32, 0, 32, 24, 0x00ff00
    .long 64, 0, 32, 24, 0x0000ff
    .long 96, 0, 32, 24, 0x00ffff
    .long 128, 0, 32, 24, 0xff00ff
    .long 160, 0, 32, 24, 0xffff00
.Lcolors_active: .ascii "[colors] canvas active (RGB/CMY)\n"
.Lcolors_active_end:
.Lcolors_done: .ascii "[colors] done\n"
.Lcolors_done_end:
.Lcolors_error: .ascii "[colors] Graphical canvas unavailable.\n"
.Lcolors_error_end:
app_colors_end:

.global app_check_start, app_check_end
app_check_start:
    sub rsp, 128
    mov r12d, 1
    mov eax, 5
    mov rdi, 0xffffffff80000000
    mov rsi, rsp
    mov edx, 1
    xor r10d, r10d
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_hello]
    mov rsi, 0xffffffff80000000
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_long]
    mov rsi, rsp
    int 0x80
    cmp rax, -3
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_badname]
    int 0x80
    cmp rax, -3
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_missing]
    int 0x80
    cmp rax, -4
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_hello]
    mov edx, 1025
    int 0x80
    cmp rax, -3
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    mov edx, 1
    mov r10, -1
    int 0x80
    test rax, rax
    jne .Lcheck_fail
    inc r12d
    mov eax, 6
    mov rdi, -1
    mov rsi, rsp
    int 0x80
    cmp rax, -4
    jne .Lcheck_fail
    inc r12d
    mov eax, 6
    xor edi, edi
    mov rsi, 0xffffffff80000000
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    inc r12d
    mov eax, 7
    mov rdi, 0xffffffff80000000
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    inc r12d
    mov eax, 7
    lea rdi, [rip + .Lcheck_rect]
    int 0x80
    cmp rax, -3
    jne .Lcheck_fail
    inc r12d
    mov eax, 8
    mov edi, 201
    int 0x80
    cmp rax, -3
    jne .Lcheck_fail
    inc r12d
    mov eax, 5
    lea rdi, [rip + .Lcheck_hello]
    mov rsi, 0x7fffffff
    mov edx, 2
    xor r10d, r10d
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    inc r12d
    mov rax, 0x7fffffff
    mov byte ptr [rax], 'a'
    mov rdi, rax
    mov eax, 5
    mov rsi, rsp
    mov edx, 1
    int 0x80
    cmp rax, -2
    jne .Lcheck_fail
    PRINT .Lcheck_ok, .Lcheck_ok_end
    EXIT 0
.Lcheck_fail:
    PRINT .Lcheck_error, .Lcheck_error_end
    mov eax, 1
    mov rdi, r12
    int 0x80
    ud2
.Lcheck_hello: .asciz "hello.txt"
.Lcheck_missing: .asciz "missing.txt"
.Lcheck_badname: .asciz "../hello.txt"
.Lcheck_long: .fill 32, 1, 97
    .byte 0
.Lcheck_rect: .long 192, 0, 1, 1, 0
.Lcheck_ok: .ascii "[check] user ABI checks passed\n"
.Lcheck_ok_end:
.Lcheck_error: .ascii "[check] syscall contract failure (exit status identifies check)\n"
.Lcheck_error_end:
app_check_end:
.section .note.GNU-stack,"",@progbits
