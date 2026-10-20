#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"

/*
 * User Space Memory Layout
 */
#define USER_CODE_VADDR   0x0000000000400000ULL /* 4 MiB base for user text */
#define USER_STACK_VADDR  0x00000000007FF000ULL /* 1 page below 8 MiB */
#define USER_STACK_TOP    0x0000000000800000ULL /* 8 MiB mark (16-byte aligned) */

/*
 * Low-level assembly context switch into user mode.
 * Returns when the user program invokes SYS_EXIT or encounters a handled fault.
 */
int user_enter_context(uint64_t user_rip, uint64_t user_rsp, uint64_t kernel_rsp0);

/*
 * Clean exit from user mode back to the kernel test harness caller.
 */
void user_exit_to_kernel(int exit_code);

/*
 * Handle an exception that occurred while executing in user mode (CPL == 3).
 */
void user_fault_handler(struct interrupt_frame *frame);

/*
 * Run the comprehensive user mode and system call verification suite:
 * 1. Test Ring 3 entry, CS verification, sys_write, invalid syscall, bad pointer rejection, and clean sys_exit.
 * 2. Test hardware privilege enforcement (privileged instruction 'cli' triggers #GP).
 * 3. Test hardware memory protection (unmapped read triggers #PF).
 *
 * Returns 0 on complete success, negative error code on failure.
 */
int usermode_test_all(void);

#endif /* USERMODE_H */
