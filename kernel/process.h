#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t pid_t;

typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

#define PROCESS_NAME_MAX 32

/* Forward declaration */
struct thread;

struct process {
    pid_t pid;                           /* Monotonic process ID */
    char name[PROCESS_NAME_MAX];         /* Process name */
    process_state_t state;               /* Lifecycle state */
    int exit_code;                       /* Exit status when terminated */

    uint64_t pml4_phys;                  /* Root PML4 physical address for CR3 */

    struct thread *main_thread;          /* Associated main schedulable thread */

    /* Memory layout tracking */
    uint64_t user_code_vaddr;            /* Virtual address of user code */
    uint64_t user_code_phys;             /* Physical frame allocated for user code */
    size_t user_code_pages;              /* Number of code pages */

    uint64_t user_stack_vaddr;           /* Virtual base address of user stack */
    uint64_t user_stack_phys;            /* Physical frame allocated for user stack */
    size_t user_stack_pages;             /* Number of stack pages */
    uint64_t user_stack_top;             /* Initial user RSP (16-byte aligned) */

    /* Process-level statistics */
    uint64_t total_ticks;                /* CPU ticks accumulated */
    uint64_t run_count;                  /* Number of times scheduled */

    struct process *next;                /* Linked list of processes */
};

/*
 * Initialize the process management subsystem.
 * Creates the kernel process (PID 0) wrapping the kernel environment.
 */
int process_init(void);

/*
 * Create a new user process with an isolated address space.
 * Returns pointer to process struct, or NULL on failure.
 */
struct process *process_create(const char *name);

/*
 * Map user code into the process's address space.
 * Allocates physical memory, copies code_bytes, and maps with PTE_PRESENT | PTE_USER | PTE_WRITABLE.
 */
int process_map_user_code(struct process *proc, uint64_t vaddr, const void *code_bytes, size_t len);

/*
 * Map user stack into the process's address space.
 * Allocates physical memory, zeroes it, and maps with PTE_PRESENT | PTE_USER | PTE_WRITABLE.
 */
int process_map_user_stack(struct process *proc, uint64_t stack_top_vaddr, size_t pages);

/*
 * Spawn a schedulable thread associated with this process.
 * The thread's interrupt frame is prepared for Ring 3 execution at user_rip and user_rsp.
 */
struct thread *process_spawn_thread(struct process *proc, uint64_t user_rip, uint64_t user_rsp);

/*
 * Terminate a process with an exit status.
 * Transitions process to PROCESS_STATE_TERMINATED and exits the calling thread.
 */
void process_exit(struct process *proc, int exit_code);

/*
 * Destroy a process: frees its user physical pages, page tables (via vmm_destroy_address_space),
 * and process metadata.
 */
void process_destroy(struct process *proc);

/*
 * Lookup process by PID.
 */
struct process *process_get_by_pid(pid_t pid);

/*
 * Get the currently executing process.
 */
struct process *process_get_current(void);

/*
 * Get head of global process list.
 */
struct process *process_get_list(void);

/*
 * Convert process state to human-readable string.
 */
const char *process_state_to_string(process_state_t state);

/* Process fault recording for exception diagnosis and verification */
struct process_fault_info {
    int occurred;
    pid_t pid;
    uint64_t vector;
    uint64_t rip;
    uint64_t cr2;
    uint64_t error_code;
};

void process_record_fault(pid_t pid, uint64_t vector, uint64_t rip, uint64_t cr2, uint64_t error_code);
void process_get_last_fault(struct process_fault_info *out_info);
void process_clear_last_fault(void);

#endif /* PROCESS_H */
