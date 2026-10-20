#include "process.h"
#include "thread.h"
#include "vmm.h"
#include "pfa.h"
#include "heap.h"
#include "serial.h"
#include "idt.h"

static struct process *process_list_head = NULL;
static struct process kernel_process;
static pid_t next_pid = 0;

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

static void kstrncpy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int process_init(void) {
    uint64_t rflags = interrupts_save_and_disable();

    kmemset(&kernel_process, 0, sizeof(struct process));
    kernel_process.pid = 0;
    kstrncpy(kernel_process.name, "kernel", PROCESS_NAME_MAX);
    kernel_process.state = PROCESS_STATE_RUNNING;
    kernel_process.exit_code = 0;
    kernel_process.pml4_phys = vmm_get_kernel_pml4();
    kernel_process.main_thread = thread_get_current();
    kernel_process.next = NULL;

    process_list_head = &kernel_process;
    next_pid = 1;

    interrupts_restore(rflags);
    return 0;
}

struct process *process_create(const char *name) {
    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    if (proc == NULL) {
        return NULL;
    }

    uint64_t pml4_phys = vmm_create_address_space();
    if (pml4_phys == 0) {
        kfree(proc);
        return NULL;
    }

    kmemset(proc, 0, sizeof(struct process));
    proc->pid = next_pid++;
    kstrncpy(proc->name, (name != NULL) ? name : "user_proc", PROCESS_NAME_MAX);
    proc->state = PROCESS_STATE_READY;
    proc->exit_code = 0;
    proc->pml4_phys = pml4_phys;
    proc->main_thread = NULL;

    /* Add to global process list protected from scheduler interrupts */
    uint64_t rflags = interrupts_save_and_disable();
    proc->next = process_list_head;
    process_list_head = proc;
    interrupts_restore(rflags);

    return proc;
}

int process_map_user_code(struct process *proc, uint64_t vaddr, const void *code_bytes, size_t len) {
    /* Validate arguments and strictly reject code larger than one page (4 KiB) */
    if (proc == NULL || proc->pml4_phys == 0 || code_bytes == NULL || len == 0 || len > PAGE_SIZE) {
        return -1;
    }

    /* Verify lower canonical address bounds and page alignment */
    if (vaddr >= 0x0000800000000000ULL || (vaddr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    /* Validate arithmetic bounds to prevent overflow beyond user space */
    if ((vaddr + PAGE_SIZE > 0x0000800000000000ULL) || (vaddr + PAGE_SIZE < vaddr)) {
        return -1;
    }

    /* Reject duplicate mappings or overlapping virtual address in this address space */
    if (proc->user_code_phys != 0) {
        return -1;
    }
    uint64_t existing_flags = 0;
    if (vmm_virt_to_phys_pml4(proc->pml4_phys, vaddr, &existing_flags) != 0) {
        return -1;
    }

    uint64_t phys = pfa_alloc_page();
    if (phys == 0) {
        return -2;
    }

    void *virt_direct = vmm_phys_to_virt(phys);
    if (virt_direct == NULL) {
        pfa_free_page(phys);
        return -2;
    }

    kmemset(virt_direct, 0, PAGE_SIZE);
    kmemcpy(virt_direct, code_bytes, len);

    int map_res = vmm_map_page_pml4(proc->pml4_phys, vaddr, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
    if (map_res != 0) {
        pfa_free_page(phys);
        return map_res;
    }

    proc->user_code_vaddr = vaddr;
    proc->user_code_phys = phys;
    proc->user_code_pages = 1;
    return 0;
}

int process_map_user_stack(struct process *proc, uint64_t stack_top_vaddr, size_t pages) {
    /* Strictly enforce supported single-page stack allocations */
    if (proc == NULL || proc->pml4_phys == 0 || pages != 1) {
        return -1;
    }

    /* Verify address bounds and 16-byte alignment */
    if (stack_top_vaddr > 0x0000800000000000ULL || (stack_top_vaddr & 0xF) != 0) {
        return -1;
    }

    /* Validate stack address arithmetic to prevent underflow */
    if (stack_top_vaddr < PAGE_SIZE) {
        return -1;
    }

    uint64_t vaddr_base = stack_top_vaddr - PAGE_SIZE;
    if ((vaddr_base & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    /* Reject duplicate mappings or overlapping virtual address */
    if (proc->user_stack_phys != 0) {
        return -1;
    }
    uint64_t existing_flags = 0;
    if (vmm_virt_to_phys_pml4(proc->pml4_phys, vaddr_base, &existing_flags) != 0) {
        return -1;
    }

    uint64_t phys = pfa_alloc_page();
    if (phys == 0) {
        return -2;
    }

    void *virt_direct = vmm_phys_to_virt(phys);
    if (virt_direct == NULL) {
        pfa_free_page(phys);
        return -2;
    }

    kmemset(virt_direct, 0, PAGE_SIZE);

    int map_res = vmm_map_page_pml4(proc->pml4_phys, vaddr_base, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
    if (map_res != 0) {
        pfa_free_page(phys);
        return map_res;
    }

    proc->user_stack_vaddr = vaddr_base;
    proc->user_stack_phys = phys;
    proc->user_stack_pages = 1;
    proc->user_stack_top = stack_top_vaddr;
    return 0;
}

struct thread *process_spawn_thread(struct process *proc, uint64_t user_rip, uint64_t user_rsp) {
    if (proc == NULL) {
        return NULL;
    }

    struct thread *t = thread_create_user(proc, proc->name, user_rip, user_rsp);
    if (t != NULL) {
        proc->main_thread = t;
    }
    return t;
}

void process_exit(struct process *proc, int exit_code) {
    if (proc == NULL) {
        return;
    }

    proc->exit_code = exit_code;
    proc->state = PROCESS_STATE_TERMINATED;

    struct thread *curr = thread_get_current();
    if (curr != NULL && curr->process == proc) {
        thread_exit();
    }
}

void process_destroy(struct process *proc) {
    if (proc == NULL || proc->pid == 0) {
        return; /* Never destroy kernel process */
    }

    /* Prevent freeing a process while any live thread still references it */
    if (thread_process_has_live_threads(proc)) {
        return;
    }

    /* Unlink from global process list protected from interrupts */
    uint64_t rflags = interrupts_save_and_disable();
    if (process_list_head == proc) {
        process_list_head = proc->next;
    } else {
        struct process *prev = process_list_head;
        while (prev != NULL && prev->next != proc) {
            prev = prev->next;
        }
        if (prev != NULL) {
            prev->next = proc->next;
        }
    }
    interrupts_restore(rflags);

    /* Free user code physical frames if mapped */
    if (proc->user_code_phys != 0) {
        if (proc->pml4_phys != 0) {
            vmm_unmap_page_pml4(proc->pml4_phys, proc->user_code_vaddr);
        }
        pfa_free_page(proc->user_code_phys);
        proc->user_code_phys = 0;
    }

    /* Free user stack physical frames if mapped */
    if (proc->user_stack_phys != 0) {
        if (proc->pml4_phys != 0) {
            vmm_unmap_page_pml4(proc->pml4_phys, proc->user_stack_vaddr);
        }
        pfa_free_page(proc->user_stack_phys);
        proc->user_stack_phys = 0;
    }

    /* Destroy address space (frees intermediate page tables and root PML4) */
    if (proc->pml4_phys != 0) {
        vmm_destroy_address_space(proc->pml4_phys);
        proc->pml4_phys = 0;
    }

    kfree(proc);
}

struct process *process_get_by_pid(pid_t pid) {
    struct process *p = process_list_head;
    while (p != NULL) {
        if (p->pid == pid) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

struct process *process_get_current(void) {
    struct thread *curr = thread_get_current();
    if (curr != NULL) {
        return curr->process;
    }
    return NULL;
}

struct process *process_get_list(void) {
    return process_list_head;
}

const char *process_state_to_string(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_READY:
            return "READY";
        case PROCESS_STATE_RUNNING:
            return "RUNNING";
        case PROCESS_STATE_BLOCKED:
            return "BLOCKED";
        case PROCESS_STATE_TERMINATED:
            return "TERMINATED";
        default:
            return "UNKNOWN";
    }
}

static struct process_fault_info last_proc_fault = {0};

void process_record_fault(pid_t pid, uint64_t vector, uint64_t rip, uint64_t cr2, uint64_t error_code) {
    last_proc_fault.occurred = 1;
    last_proc_fault.pid = pid;
    last_proc_fault.vector = vector;
    last_proc_fault.rip = rip;
    last_proc_fault.cr2 = cr2;
    last_proc_fault.error_code = error_code;
}

void process_get_last_fault(struct process_fault_info *out_info) {
    if (out_info != NULL) {
        *out_info = last_proc_fault;
    }
}

void process_clear_last_fault(void) {
    kmemset(&last_proc_fault, 0, sizeof(last_proc_fault));
}

