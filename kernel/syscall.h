#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"

/*
 * System Call Numbers (passed in %rax)
 */
#define SYS_EXIT  1
#define SYS_WRITE 2
#define SYS_GETPID 3
#define SYS_YIELD 4

/*
 * Standard System Call Error Codes (returned in %rax)
 */
#define SYSCALL_SUCCESS   0
#define SYSCALL_ENOSYS  (-1) /* Unknown / invalid system call number */
#define SYSCALL_EFAULT  (-2) /* Invalid address or bad user pointer */
#define SYSCALL_EINVAL  (-3) /* Invalid argument (e.g. invalid file descriptor or length) */

/*
 * Upper limit for user space addresses (lower canonical half: 0 to 0x00007FFFFFFFFFFF)
 */
#define USER_SPACE_MAX_ADDR 0x0000800000000000ULL

/*
 * Maximum bytes allowed in a single SYS_WRITE call
 */
#define SYS_WRITE_MAX_BYTES 4096

/*
 * Validate a user-provided buffer:
 * - Must not be NULL (if len > 0)
 * - Must lie strictly within user space (< 0x0000800000000000)
 * - Must not overflow
 * - Every 4 KiB page in the range must be present and user-accessible (PTE_PRESENT | PTE_USER)
 * - If require_write != 0, every page must also be writable (PTE_WRITABLE)
 *
 * Returns 0 if valid, non-zero if invalid.
 */
int user_buffer_validate(const void *ptr, size_t len, int require_write);

/*
 * Initialize the system call interface and register the handler for SYSCALL_VECTOR (128 / 0x80).
 */
void syscall_init(void);

/*
 * Dispatch system calls called from user mode (Vector 128 / 0x80).
 */
void syscall_handler(struct interrupt_frame *frame);

#endif /* SYSCALL_H */
