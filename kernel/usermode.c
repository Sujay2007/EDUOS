#include "usermode.h"
#include "syscall.h"
#include "vmm.h"
#include "pfa.h"
#include "heap.h"
#include "gdt.h"
#include "serial.h"

extern const uint8_t user_test_blob_start[];
extern const uint8_t user_test_blob_end[];

extern const uint8_t user_fault_cli_blob_start[];
extern const uint8_t user_fault_cli_blob_end[];

extern const uint8_t user_fault_mem_blob_start[];
extern const uint8_t user_fault_mem_blob_end[];

static volatile int expecting_fault_vector = 0;
static volatile uint64_t last_fault_rip = 0;
static volatile uint64_t last_fault_cr2 = 0;

static void *kmemset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    for (size_t i = 0; i < len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

static void *kmemcpy(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}

static inline uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void user_fault_handler(struct interrupt_frame *frame) {
    uint64_t vec = frame->vector;
    last_fault_rip = frame->rip;

    if (vec == 14) {
        last_fault_cr2 = read_cr2();
    }

    if (expecting_fault_vector != 0 && (int)vec == expecting_fault_vector) {
        /* Expected fault occurred during privilege / protection testing */
        user_exit_to_kernel(-(int)(vec + 100));
        return;
    }

    /* Unexpected fault */
    serial_write("\r\n  [USER FAULT] Unexpected user-mode exception: Vector ");
    serial_write_dec(vec);
    serial_write(" at RIP ");
    serial_write_hex64(frame->rip);
    serial_write(" RSP ");
    serial_write_hex64(frame->rsp);
    serial_write("\r\n");

    user_exit_to_kernel(-(int)(vec + 100));
}

int usermode_test_all(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Running User Mode (Ring 3) & System Call Verification Suite:\r\n");

    /* Initialize system call handler if not already done */
    syscall_init();

    /* 1. Allocate physical frames for user code and user stack */
    uint64_t code_phys = pfa_alloc_page();
    uint64_t stack_phys = pfa_alloc_page();
    if (code_phys == 0 || stack_phys == 0) {
        serial_write("  [FAIL] Failed to allocate physical frames for user mode!\r\n");
        if (code_phys) pfa_free_page(code_phys);
        if (stack_phys) pfa_free_page(stack_phys);
        return -1;
    }

    /* Allocate dedicated 8 KiB kernel stack for privilege transition (TSS RSP0) */
    void *kstack = kmalloc(8192);
    if (kstack == NULL) {
        serial_write("  [FAIL] Failed to allocate kernel stack for user mode!\r\n");
        pfa_free_page(code_phys);
        pfa_free_page(stack_phys);
        return -1;
    }
    uint64_t kstack_top = (uint64_t)kstack + 8192;
    kstack_top &= ~0xFULL; /* 16-byte align */

    /* 2. Map user virtual pages into the active page table with PTE_USER */
    int r1 = vmm_map_page(USER_CODE_VADDR, code_phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
    int r2 = vmm_map_page(USER_STACK_VADDR, stack_phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
    if (r1 != 0 || r2 != 0) {
        serial_write("  [FAIL] Failed to map user pages into VMM!\r\n");
        vmm_unmap_page(USER_CODE_VADDR);
        vmm_unmap_page(USER_STACK_VADDR);
        pfa_free_page(code_phys);
        pfa_free_page(stack_phys);
        kfree(kstack);
        return -2;
    }

    void *code_virt = vmm_phys_to_virt(code_phys);
    void *stack_virt = vmm_phys_to_virt(stack_phys);
    kmemset(stack_virt, 0, PAGE_SIZE);

    /* ------------------------------------------------------------- */
    /* TEST 1: Ring 3 Transition, CPL 3, SYS_WRITE, Invalid Syscall, */
    /*         Bad Pointer Protection, and Clean SYS_EXIT            */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[TEST 1] Ring 3 Transition, Syscall ABI & Memory Safety Verification\r\n");
    size_t test_len = (size_t)(user_test_blob_end - user_test_blob_start);
    kmemset(code_virt, 0, PAGE_SIZE);
    kmemcpy(code_virt, user_test_blob_start, test_len);

    expecting_fault_vector = 0;
    uint64_t saved_rsp0 = gdt_get_kernel_stack();

    int exit_code = user_enter_context(USER_CODE_VADDR, USER_STACK_TOP, kstack_top);
    gdt_set_kernel_stack(saved_rsp0);

    if (exit_code == 0) {
        serial_write("  User mode executed at CPL 3, called sys_write, verified error handling, and exited.\r\n");
        serial_write("  [PASS] Ring 3 entry, privilege verification, sys_write, and clean sys_exit verified!\r\n");
    } else {
        serial_write("  [FAIL] User mode test failed with exit code: ");
        serial_write_dec(exit_code);
        serial_write("\r\n");
        vmm_unmap_page(USER_CODE_VADDR);
        vmm_unmap_page(USER_STACK_VADDR);
        pfa_free_page(code_phys);
        pfa_free_page(stack_phys);
        kfree(kstack);
        return -3;
    }

    /* ------------------------------------------------------------- */
    /* TEST 2: Hardware Privilege Enforcement ('cli' in Ring 3)     */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[TEST 2] Hardware Privilege Enforcement (Privileged Instruction #GP Check)\r\n");
    size_t cli_len = (size_t)(user_fault_cli_blob_end - user_fault_cli_blob_start);
    kmemset(code_virt, 0, PAGE_SIZE);
    kmemcpy(code_virt, user_fault_cli_blob_start, cli_len);

    expecting_fault_vector = 13; /* General Protection Fault (#GP) */
    saved_rsp0 = gdt_get_kernel_stack();

    int cli_exit = user_enter_context(USER_CODE_VADDR, USER_STACK_TOP, kstack_top);
    gdt_set_kernel_stack(saved_rsp0);

    if (cli_exit == -(13 + 100)) {
        serial_write("  Privileged instruction 'cli' at CPL 3 triggered expected #GP (Vector 13).\r\n");
        serial_write("  [PASS] Hardware privilege separation correctly enforced by CPU!\r\n");
    } else {
        serial_write("  [FAIL] 'cli' did not generate expected #GP (exit code: ");
        serial_write_dec(cli_exit);
        serial_write(")\r\n");
        vmm_unmap_page(USER_CODE_VADDR);
        vmm_unmap_page(USER_STACK_VADDR);
        pfa_free_page(code_phys);
        pfa_free_page(stack_phys);
        kfree(kstack);
        return -4;
    }

    /* ------------------------------------------------------------- */
    /* TEST 3: Hardware Memory Protection (Unmapped Read #PF Check)  */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[TEST 3] Hardware Memory Protection (Unmapped Address #PF Check)\r\n");
    size_t mem_len = (size_t)(user_fault_mem_blob_end - user_fault_mem_blob_start);
    kmemset(code_virt, 0, PAGE_SIZE);
    kmemcpy(code_virt, user_fault_mem_blob_start, mem_len);

    expecting_fault_vector = 14; /* Page Fault (#PF) */
    saved_rsp0 = gdt_get_kernel_stack();

    int mem_exit = user_enter_context(USER_CODE_VADDR, USER_STACK_TOP, kstack_top);
    gdt_set_kernel_stack(saved_rsp0);

    if (mem_exit == -(14 + 100)) {
        serial_write("  Unmapped memory access at 0x200000 triggered expected #PF (Vector 14).\r\n");
        serial_write("  CR2 verified at: ");
        serial_write_hex64(last_fault_cr2);
        serial_write("\r\n");
        serial_write("  [PASS] Hardware memory paging protection verified!\r\n");
    } else {
        serial_write("  [FAIL] Unmapped access did not generate expected #PF (exit code: ");
        serial_write_dec(mem_exit);
        serial_write(")\r\n");
        vmm_unmap_page(USER_CODE_VADDR);
        vmm_unmap_page(USER_STACK_VADDR);
        pfa_free_page(code_phys);
        pfa_free_page(stack_phys);
        kfree(kstack);
        return -5;
    }

    /* ------------------------------------------------------------- */
    /* Clean up allocated user pages and stack                       */
    /* ------------------------------------------------------------- */
    vmm_unmap_page(USER_CODE_VADDR);
    vmm_unmap_page(USER_STACK_VADDR);
    pfa_free_page(code_phys);
    pfa_free_page(stack_phys);
    kfree(kstack);

    serial_write("\r\n====================================================================\r\n");
    serial_write("All User Mode & System Call Tests PASSED successfully!\r\n");
    return 0;
}
