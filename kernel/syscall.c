#include "syscall.h"
#include "vmm.h"
#include "serial.h"
#include "thread.h"
#include <stddef.h>

extern void user_exit_to_kernel(int exit_code);

int user_buffer_validate(const void *ptr, size_t len, int require_write) {
    if (len == 0) {
        return 0;
    }

    if (ptr == NULL) {
        return -1;
    }

    uint64_t start = (uint64_t)ptr;
    uint64_t end = start + len;

    /* Check for integer wrap-around */
    if (end < start) {
        return -1;
    }

    /* Enforce that the entire buffer strictly resides in user space */
    if (start >= USER_SPACE_MAX_ADDR || end > USER_SPACE_MAX_ADDR) {
        return -1;
    }

    /* Verify each 4 KiB page spanning the buffer */
    uint64_t page_start = start & ~(PAGE_SIZE - 1);
    uint64_t page_end = (end - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t page = page_start; page <= page_end; page += PAGE_SIZE) {
        uint64_t pte_flags = 0;
        uint64_t phys = vmm_virt_to_phys(page, &pte_flags);

        if (phys == 0) {
            return -1; /* Page not mapped */
        }

        if (!(pte_flags & PTE_PRESENT)) {
            return -1; /* Page not present */
        }

        if (!(pte_flags & PTE_USER)) {
            return -1; /* Page not accessible to user mode (kernel memory) */
        }

        if (require_write && !(pte_flags & PTE_WRITABLE)) {
            return -1; /* Page is not writable */
        }
    }

    return 0;
}

void syscall_handler(struct interrupt_frame *frame) {
    uint64_t sys_num = frame->rax;

    switch (sys_num) {
        case SYS_EXIT: {
            int status = (int)frame->rdi;
            user_exit_to_kernel(status);
            /* Does not return */
            break;
        }

        case SYS_WRITE: {
            int fd = (int)frame->rdi;
            const char *buf = (const char *)frame->rsi;
            size_t count = (size_t)frame->rdx;

            /* Check valid file descriptor (1 = stdout, 2 = stderr) */
            if (fd != 1 && fd != 2) {
                frame->rax = SYSCALL_EINVAL;
                return;
            }

            if (count > SYS_WRITE_MAX_BYTES) {
                frame->rax = SYSCALL_EINVAL;
                return;
            }

            if (count == 0) {
                frame->rax = 0;
                return;
            }

            /* Validate user pointer and mapping permissions */
            if (user_buffer_validate(buf, count, 0) != 0) {
                frame->rax = SYSCALL_EFAULT;
                return;
            }

            /* Output string safely to serial terminal */
            for (size_t i = 0; i < count; i++) {
                serial_putchar(buf[i]);
            }

            frame->rax = (int64_t)count;
            return;
        }

        case SYS_GETPID: {
            struct thread *curr = thread_get_current();
            frame->rax = (curr != NULL) ? (int64_t)curr->tid : 1;
            return;
        }

        case SYS_YIELD: {
            thread_yield();
            frame->rax = 0;
            return;
        }

        default: {
            /* Unknown system call number: return defined error without corrupting state */
            frame->rax = SYSCALL_ENOSYS;
            return;
        }
    }
}

void syscall_init(void) {
    idt_register_handler(SYSCALL_VECTOR, syscall_handler);
}
