#include "process_test.h"
#include "process.h"
#include "thread.h"
#include "vmm.h"
#include "pfa.h"
#include "pit.h"
#include "serial.h"
#include "syscall.h"
#include <stddef.h>

/* External test blob symbols declared in process_test_blobs.S */
extern uint8_t proc_alpha_blob_start[];
extern uint8_t proc_alpha_blob_end[];

extern uint8_t proc_beta_blob_start[];
extern uint8_t proc_beta_blob_end[];

extern uint8_t proc_sup_violation_blob_start[];
extern uint8_t proc_sup_violation_blob_end[];

extern uint8_t proc_syscall_badptr_blob_start[];
extern uint8_t proc_syscall_badptr_blob_end[];

extern uint8_t proc_fault_cli_blob_start[];
extern uint8_t proc_fault_cli_blob_end[];

extern uint8_t proc_safe_companion_blob_start[];
extern uint8_t proc_safe_companion_blob_end[];

static int wait_for_process(struct process *proc, uint64_t max_ticks) {
    uint64_t start = timer_get_ticks();
    while (proc->state != PROCESS_STATE_TERMINATED) {
        thread_yield();
        if (timer_get_ticks() - start > max_ticks) {
            serial_write("  [ERROR] Timed out waiting for process to terminate!\r\n");
            return -1;
        }
    }
    return 0;
}

int process_test_all(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Running Process Management & Address Space Isolation Test Suite:\r\n");

    struct pfa_stats baseline_pfa;
    pfa_get_stats(&baseline_pfa);
    uint64_t baseline_free_pages = baseline_pfa.free_pages;

    int total_failures = 0;

    /* -----------------------------------------------------------------
     * TEST 1: Address Space Lifecycle & Page Table Isolation
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 1] Address Space Lifecycle & Higher-Half Sharing:\r\n");
    {
        uint64_t pml4_phys = vmm_create_address_space();
        if (pml4_phys == 0 || pml4_phys == vmm_get_kernel_pml4()) {
            serial_write("  [FAIL] Failed to create new PML4 address space!\r\n");
            total_failures++;
        } else {
            uint64_t *pml4_virt = (uint64_t *)vmm_phys_to_virt(pml4_phys);
            uint64_t *k_pml4_virt = (uint64_t *)vmm_phys_to_virt(vmm_get_kernel_pml4());

            int shared_ok = 1;
            /* Verify indices 256..511 are identical to kernel PML4 and supervisor-only */
            for (int i = 256; i < 512; i++) {
                if (pml4_virt[i] != k_pml4_virt[i]) {
                    shared_ok = 0;
                    break;
                }
                if (pml4_virt[i] & PTE_USER) {
                    shared_ok = 0;
                    break;
                }
            }

            /* Verify indices 0..255 are strictly zeroed (no user mappings initially) */
            int isolated_ok = 1;
            for (int i = 0; i < 256; i++) {
                if (pml4_virt[i] != 0) {
                    isolated_ok = 0;
                    break;
                }
            }

            /* Map a user page into this address space at 0x400000 */
            uint64_t user_frame = pfa_alloc_page();
            int map_ok = (vmm_map_page_pml4(pml4_phys, 0x400000, user_frame, PTE_PRESENT | PTE_USER | PTE_WRITABLE) == 0);

            uint64_t lookup_flags = 0;
            uint64_t phys_found = vmm_virt_to_phys_pml4(pml4_phys, 0x400000, &lookup_flags);
            int lookup_ok = (phys_found == user_frame) && ((lookup_flags & PTE_USER) != 0);

            /* Clean up mapped page and destroy address space */
            vmm_unmap_page_pml4(pml4_phys, 0x400000);
            pfa_free_page(user_frame);
            vmm_destroy_address_space(pml4_phys);

            struct pfa_stats pfa_check;
            pfa_get_stats(&pfa_check);
            int leak_ok = (pfa_check.free_pages == baseline_free_pages);

            if (shared_ok && isolated_ok && map_ok && lookup_ok && leak_ok) {
                serial_write("  [PASS] PML4 lifecycle, higher-half sharing, and memory reclamation verified.\r\n");
            } else {
                serial_write("  [FAIL] Address space lifecycle verification failed!\r\n");
                total_failures++;
            }
        }
    }

    /* -----------------------------------------------------------------
     * TEST 2: Strict Hardware Supervisor-Only Protection Proof
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 2] Hardware Supervisor Protection Proof (#PF Protection Violation):\r\n");
    {
        struct process *proc = process_create("sup_probe");
        if (proc == NULL) {
            serial_write("  [FAIL] Could not create sup_probe process!\r\n");
            total_failures++;
        } else {
            size_t code_len = (size_t)(proc_sup_violation_blob_end - proc_sup_violation_blob_start);
            int r_c = process_map_user_code(proc, 0x400000, proc_sup_violation_blob_start, code_len);
            int r_s = process_map_user_stack(proc, 0x80000000, 1);
            if (r_c != 0 || r_s != 0) {
                serial_write("  [FAIL] Failed to map code/stack for sup_probe!\r\n");
                total_failures++;
                process_destroy(proc);
            } else {
                process_clear_last_fault();
                struct thread *t = process_spawn_thread(proc, 0x400000, 0x80000000);
                if (t == NULL) {
                    serial_write("  [FAIL] Failed to spawn thread for sup_probe!\r\n");
                    total_failures++;
                    process_destroy(proc);
                } else {
                    int w_res = wait_for_process(proc, 200);
                    if (w_res != 0) {
                        serial_write("  [FAIL] Timeout waiting for sup_probe!\r\n");
                        total_failures++;
                        process_destroy(proc);
                    } else {
                        struct process_fault_info fault;
                        process_get_last_fault(&fault);

                        int pf_vector_ok = (fault.occurred && fault.vector == 14 && fault.pid == proc->pid);
                        int cr2_ok = (fault.cr2 == 0xFFFFFFFF80000000ULL);

                        /*
                         * Hardware assertion: Page Fault Error Code:
                         * Bit 0 (P): 1 = Protection violation (page was present in PML4/PDPT/PD/PT!)
                         * Bit 1 (W): 0 = Read access
                         * Bit 2 (U): 1 = Access caused by user-mode execution (CPL 3)
                         * (error_code & 0x05) == 0x05 proves that the kernel mapping exists,
                         * but the CPU strictly denied access because it lacks PTE_USER!
                         */
                        int prot_viol_ok = ((fault.error_code & 0x05) == 0x05);
                        int exit_ok = (proc->exit_code == -(14 + 100)); /* -114 */

                        if (pf_vector_ok && cr2_ok && prot_viol_ok && exit_ok) {
                            serial_write("  [PASS] Supervisor protection proven: #PF with P=1 (protection violation) & U=1 (Ring 3) on 0xFFFFFFFF80000000!\r\n");
                        } else {
                            serial_write("  [FAIL] Supervisor protection test failed: ");
                            serial_write("Vector=");
                            serial_write_dec(fault.vector);
                            serial_write(" CR2=");
                            serial_write_hex64(fault.cr2);
                            serial_write(" ErrorCode=");
                            serial_write_hex64(fault.error_code);
                            serial_write(" ExitCode=");
                            serial_write_dec((uint64_t)proc->exit_code);
                            serial_write("\r\n");
                            total_failures++;
                        }

                        process_destroy(proc);
                    }
                }
            }
        }
    }

    /* -----------------------------------------------------------------
     * TEST 3: Dual Isolated Processes at Identical Virtual Addresses
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 3] Dual Processes at Identical Virtual Addresses (0x400000):\r\n");
    {
        struct process *p1 = process_create("proc_alpha");
        struct process *p2 = process_create("proc_beta");

        if (p1 == NULL || p2 == NULL) {
            serial_write("  [FAIL] Failed to allocate dual processes!\r\n");
            total_failures++;
            if (p1) process_destroy(p1);
            if (p2) process_destroy(p2);
        } else {
            /* Verify private physical address spaces */
            int cr3_diff = (p1->pml4_phys != p2->pml4_phys);

            size_t len1 = (size_t)(proc_alpha_blob_end - proc_alpha_blob_start);
            size_t len2 = (size_t)(proc_beta_blob_end - proc_beta_blob_start);

            /* Both processes mapped at EXACTLY the same virtual address 0x400000 */
            int r1_c = process_map_user_code(p1, 0x400000, proc_alpha_blob_start, len1);
            int r1_s = process_map_user_stack(p1, 0x80000000, 1);

            int r2_c = process_map_user_code(p2, 0x400000, proc_beta_blob_start, len2);
            int r2_s = process_map_user_stack(p2, 0x80000000, 1);

            if (r1_c != 0 || r1_s != 0 || r2_c != 0 || r2_s != 0) {
                serial_write("  [FAIL] Failed to map memory for dual processes!\r\n");
                total_failures++;
                process_destroy(p1);
                process_destroy(p2);
            } else {
                int phys_diff = (p1->user_code_phys != p2->user_code_phys) &&
                                (p1->user_stack_phys != p2->user_stack_phys);

                struct thread *t1 = process_spawn_thread(p1, 0x400000, 0x80000000);
                struct thread *t2 = process_spawn_thread(p2, 0x400000, 0x80000000);

                if (t1 == NULL || t2 == NULL) {
                    serial_write("  [FAIL] Failed to spawn threads for dual processes!\r\n");
                    total_failures++;
                    process_destroy(p1);
                    process_destroy(p2);
                } else {
                    int w1 = wait_for_process(p1, 300);
                    int w2 = wait_for_process(p2, 300);

                    if (w1 != 0 || w2 != 0) {
                        serial_write("  [FAIL] Timeout waiting for dual processes!\r\n");
                        total_failures++;
                        process_destroy(p1);
                        process_destroy(p2);
                    } else {
                        int p1_ok = (p1->exit_code == 42);
                        int p2_ok = (p2->exit_code == 84);

                        if (cr3_diff && phys_diff && p1_ok && p2_ok) {
                            serial_write("  [PASS] Both processes ran concurrently at 0x400000 with private memory, exiting with 42 & 84!\r\n");
                        } else {
                            serial_write("  [FAIL] Dual process isolation failed!\r\n");
                            total_failures++;
                        }

                        process_destroy(p1);
                        process_destroy(p2);
                    }
                }
            }
        }
    }

    /* -----------------------------------------------------------------
     * TEST 4: System Call User Pointer Validation (EFAULT)
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 4] System Call Kernel Pointer Validation (SYS_WRITE rejection):\r\n");
    {
        struct process *proc = process_create("bad_ptr_proc");
        if (proc == NULL) {
            serial_write("  [FAIL] Failed to create bad_ptr_proc!\r\n");
            total_failures++;
        } else {
            size_t code_len = (size_t)(proc_syscall_badptr_blob_end - proc_syscall_badptr_blob_start);
            int r_c = process_map_user_code(proc, 0x400000, proc_syscall_badptr_blob_start, code_len);
            int r_s = process_map_user_stack(proc, 0x80000000, 1);

            if (r_c != 0 || r_s != 0) {
                serial_write("  [FAIL] Failed to map memory for bad_ptr_proc!\r\n");
                total_failures++;
                process_destroy(proc);
            } else {
                struct thread *t = process_spawn_thread(proc, 0x400000, 0x80000000);
                if (t == NULL) {
                    serial_write("  [FAIL] Failed to spawn thread for bad_ptr_proc!\r\n");
                    total_failures++;
                    process_destroy(proc);
                } else {
                    int w_res = wait_for_process(proc, 200);
                    if (w_res != 0) {
                        serial_write("  [FAIL] Timeout waiting for bad_ptr_proc!\r\n");
                        total_failures++;
                        process_destroy(proc);
                    } else {
                        if (proc->exit_code == 0) {
                            serial_write("  [PASS] Kernel pointer 0xFFFFFFFF80000000 passed to SYS_WRITE rejected with EFAULT (-2).\r\n");
                        } else {
                            serial_write("  [FAIL] System call pointer validation failed (exit code ");
                            serial_write_dec((uint64_t)proc->exit_code);
                            serial_write(")!\r\n");
                            total_failures++;
                        }

                        process_destroy(proc);
                    }
                }
            }
        }
    }

    /* -----------------------------------------------------------------
     * TEST 5: Fault Containment (Crashing Process Does Not Affect Others)
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 5] Process Fault Containment & System Resilience:\r\n");
    {
        struct process *p_fault = process_create("fault_proc");
        struct process *p_safe  = process_create("safe_proc");

        if (p_fault == NULL || p_safe == NULL) {
            serial_write("  [FAIL] Failed to create containment test processes!\r\n");
            total_failures++;
            if (p_fault) process_destroy(p_fault);
            if (p_safe) process_destroy(p_safe);
        } else {
            size_t f_len = (size_t)(proc_fault_cli_blob_end - proc_fault_cli_blob_start);
            size_t s_len = (size_t)(proc_safe_companion_blob_end - proc_safe_companion_blob_start);

            int rf_c = process_map_user_code(p_fault, 0x400000, proc_fault_cli_blob_start, f_len);
            int rf_s = process_map_user_stack(p_fault, 0x80000000, 1);

            int rs_c = process_map_user_code(p_safe, 0x400000, proc_safe_companion_blob_start, s_len);
            int rs_s = process_map_user_stack(p_safe, 0x80000000, 1);

            if (rf_c != 0 || rf_s != 0 || rs_c != 0 || rs_s != 0) {
                serial_write("  [FAIL] Failed to map memory for containment test!\r\n");
                total_failures++;
                process_destroy(p_fault);
                process_destroy(p_safe);
            } else {
                struct thread *tf = process_spawn_thread(p_fault, 0x400000, 0x80000000);
                struct thread *ts = process_spawn_thread(p_safe, 0x400000, 0x80000000);

                if (tf == NULL || ts == NULL) {
                    serial_write("  [FAIL] Failed to spawn threads for containment test!\r\n");
                    total_failures++;
                    process_destroy(p_fault);
                    process_destroy(p_safe);
                } else {
                    int wf = wait_for_process(p_fault, 200);
                    int ws = wait_for_process(p_safe, 200);

                    if (wf != 0 || ws != 0) {
                        serial_write("  [FAIL] Timeout waiting for containment test processes!\r\n");
                        total_failures++;
                        process_destroy(p_fault);
                        process_destroy(p_safe);
                    } else {
                        int fault_exit_ok = (p_fault->exit_code == -(13 + 100)); /* -113 for #GP */
                        int safe_exit_ok  = (p_safe->exit_code == 77);

                        if (fault_exit_ok && safe_exit_ok) {
                            serial_write("  [PASS] Faulting process terminated cleanly (-113 #GP) while safe companion completed (77)!\r\n");
                        } else {
                            serial_write("  [FAIL] Fault containment failed: fault_proc=");
                            serial_write_dec((uint64_t)p_fault->exit_code);
                            serial_write(" safe_proc=");
                            serial_write_dec((uint64_t)p_safe->exit_code);
                            serial_write("\r\n");
                            total_failures++;
                        }

                        process_destroy(p_fault);
                        process_destroy(p_safe);
                    }
                }
            }
        }
    }

    /* -----------------------------------------------------------------
     * TEST 6: Zero PFA Page Leak Verification
     * ----------------------------------------------------------------- */
    serial_write("\r\n[TEST 6] Zero PFA Page Leak Verification:\r\n");
    {
        /* Yield to allow thread garbage collection to reclaim thread stacks */
        for (int i = 0; i < 5; i++) {
            thread_yield();
        }

        struct pfa_stats current_pfa;
        pfa_get_stats(&current_pfa);

        if (current_pfa.free_pages == baseline_free_pages) {
            serial_write("  [PASS] Zero physical pages leaked (Free pages: ");
            serial_write_dec(current_pfa.free_pages);
            serial_write(" == Baseline: ");
            serial_write_dec(baseline_free_pages);
            serial_write(").\r\n");
        } else {
            serial_write("  [FAIL] Page leak detected! Current free: ");
            serial_write_dec(current_pfa.free_pages);
            serial_write(" Baseline free: ");
            serial_write_dec(baseline_free_pages);
            serial_write("\r\n");
            total_failures++;
        }
    }

    serial_write("\r\n--------------------------------------------------------------------\r\n");
    if (total_failures == 0) {
        serial_write("All Process Management & Address Space tests PASSED successfully!\r\n");
    } else {
        serial_write("Process test suite finished with FAILURES!\r\n");
    }
    serial_write("====================================================================\r\n\r\n");

    return total_failures;
}
