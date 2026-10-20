#include "shell.h"
#include "terminal.h"
#include "keyboard.h"
#include "pfa.h"
#include "vmm.h"
#include "heap.h"
#include "pit.h"
#include "io.h"
#include "idt.h"
#include "thread.h"
#include "process.h"
#include "process_test.h"
#include "usermode.h"
#include "fs.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_PROMPT "edus> "

static int streq(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 == *(const unsigned char *)s2;
}

static void cmd_help(void) {
    terminal_writeln("Educational OS Shell - Available Commands:");
    terminal_writeln("  help          - Display this help information");
    terminal_writeln("  ls            - List files in the read-only root directory");
    terminal_writeln("  cat <name>    - Read a file from persistent storage");
    terminal_writeln("  fsinfo        - Show filesystem and block-device information");
    terminal_writeln("  clear         - Clear the terminal screen");
    terminal_writeln("  mem           - Display physical memory allocator (PFA) statistics");
    terminal_writeln("  heap          - Display dynamic kernel heap statistics");
    terminal_writeln("  ticks         - Display timer tick count and interrupt frequency");
    terminal_writeln("  uptime        - Display system uptime since boot");
    terminal_writeln("  threads       - List active kernel threads and lifecycle states");
    terminal_writeln("  sched         - Display scheduler execution metrics and context switches");
    terminal_writeln("  yield         - Voluntarily yield the CPU to another thread");
    terminal_writeln("  sleep <ticks> - Block current thread for specified timer ticks");
    terminal_writeln("  ps            - List active processes and address space PML4 roots");
    terminal_writeln("  proc <pid>    - Display detailed process metrics and memory layout");
    terminal_writeln("  vmmap <pid>   - Walk 4-level page table and display memory mappings");
    terminal_writeln("  proctest      - Run process isolation and protection test suite");
    terminal_writeln("  usertest      - Execute Ring 3 user mode & system call test suite");
    terminal_writeln("  about         - Display educational OS system information");
    terminal_writeln("  reboot        - Reboot the computer");
}

static void cmd_clear(void) {
    terminal_clear();
}

static void cmd_mem(void) {
    struct pfa_stats stats;
    pfa_get_stats(&stats);

    terminal_writeln("Physical Memory Statistics (PFA):");
    terminal_write("  Total Addressable Pages : ");
    terminal_write_dec(stats.total_pages);
    terminal_write(" (");
    terminal_write_hex(stats.total_pages * PAGE_SIZE);
    terminal_writeln(")");

    terminal_write("  Usable RAM Pages        : ");
    terminal_write_dec(stats.usable_pages);
    terminal_write(" (");
    terminal_write_dec((stats.usable_pages * PAGE_SIZE) / 1024 / 1024);
    terminal_writeln(" MiB)");

    terminal_write("  Bitmap Metadata Pages   : ");
    terminal_write_dec(stats.metadata_pages);
    terminal_write(" (");
    terminal_write_dec(stats.metadata_pages * 4);
    terminal_writeln(" KiB)");

    terminal_write("  Allocated Pages         : ");
    terminal_write_dec(stats.allocated_pages);
    terminal_writeln("");

    terminal_write("  Free Pages              : ");
    terminal_write_dec(stats.free_pages);
    terminal_writeln("");
}

static void cmd_heap(void) {
    struct heap_stats hstats;
    heap_get_stats(&hstats);

    terminal_writeln("Dynamic Kernel Heap Statistics:");
    terminal_write("  Total Mapped Bytes : ");
    terminal_write_dec(hstats.total_heap_bytes);
    terminal_write(" (");
    terminal_write_dec(hstats.total_heap_bytes / 1024);
    terminal_writeln(" KiB)");

    terminal_write("  Allocated Bytes    : ");
    terminal_write_dec(hstats.allocated_bytes);
    terminal_writeln("");

    terminal_write("  Free Bytes         : ");
    terminal_write_dec(hstats.free_bytes);
    terminal_writeln("");

    terminal_write("  Active Allocations : ");
    terminal_write_dec(hstats.allocation_count);
    terminal_writeln("");
}

static void cmd_ticks(void) {
    uint64_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();

    terminal_write("System Timer Ticks: ");
    terminal_write_dec(ticks);
    terminal_write(" (PIT Frequency: ");
    terminal_write_dec(freq);
    terminal_writeln(" Hz)");
}

static void cmd_uptime(void) {
    uint64_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();

    if (freq == 0) {
        freq = 100;
    }

    uint64_t seconds = ticks / freq;
    uint64_t centiseconds = (ticks % freq) * 100 / freq;

    terminal_write("Uptime: ");
    terminal_write_dec(seconds);
    terminal_putchar('.');
    if (centiseconds < 10) {
        terminal_putchar('0');
    }
    terminal_write_dec(centiseconds);
    terminal_write(" seconds (");
    terminal_write_dec(ticks);
    terminal_writeln(" ticks)");
}

static void cmd_threads(void) {
    terminal_writeln("Active Kernel Threads:");
    thread_print_table();
}

static void cmd_sched(void) {
    struct scheduler_stats stats;
    scheduler_get_stats(&stats);

    terminal_writeln("Scheduler Execution Metrics:");
    terminal_write("  Total Context Switches : ");
    terminal_write_dec(stats.total_switches);
    terminal_writeln("");

    terminal_write("  Timer Preemptions      : ");
    terminal_write_dec(stats.timer_preemptions);
    terminal_writeln("");

    terminal_write("  Voluntary Yields       : ");
    terminal_write_dec(stats.voluntary_yields);
    terminal_writeln("");

    terminal_write("  Active Threads Total   : ");
    terminal_write_dec(stats.active_threads);
    terminal_writeln("");

    terminal_write("    READY                : ");
    terminal_write_dec(stats.ready_threads);
    terminal_writeln("");

    terminal_write("    RUNNING              : ");
    terminal_write_dec(stats.running_threads);
    terminal_writeln("");

    terminal_write("    BLOCKED              : ");
    terminal_write_dec(stats.blocked_threads);
    terminal_writeln("");

    terminal_write("    TERMINATED           : ");
    terminal_write_dec(stats.terminated_threads);
    terminal_writeln("");

    terminal_write("  PIT Timer Frequency    : ");
    terminal_write_dec(timer_get_frequency());
    terminal_writeln(" Hz (10 ms per tick)");
}

static void cmd_yield(void) {
    terminal_writeln("Voluntarily yielding CPU to scheduler...");
    thread_yield();
    terminal_writeln("Resumed execution from voluntary yield.");
}

static void cmd_sleep(const char *arg) {
    /* Skip whitespace */
    while (*arg == ' ' || *arg == '\t') {
        arg++;
    }

    if (*arg == '\0') {
        terminal_writeln("Usage: sleep <ticks>");
        terminal_writeln("  Note: 1 tick = 10 ms at 100 Hz PIT timer (e.g. 'sleep 100' = 1 second).");
        return;
    }

    /* Validate and parse unsigned integer */
    uint64_t ticks = 0;
    const char *p = arg;
    while (*p >= '0' && *p <= '9') {
        ticks = (ticks * 10) + (*p - '0');
        p++;
    }

    if (*p != '\0' && *p != ' ' && *p != '\t') {
        terminal_writeln("Error: Invalid tick value. Expected positive integer.");
        return;
    }

    if (ticks == 0) {
        terminal_writeln("Sleeping for 0 ticks (immediate yield)...");
        thread_yield();
        terminal_writeln("Woke up from sleep.");
        return;
    }

    terminal_write("Blocking current thread for ");
    terminal_write_dec(ticks);
    terminal_write(" ticks (~");
    terminal_write_dec(ticks * 10);
    terminal_writeln(" ms)...");

    thread_sleep(ticks);

    terminal_writeln("Woke up from sleep (deadline reached)!");
}

static void cmd_about(void) {
    terminal_writeln("==================================================");
    terminal_writeln("  Educational Operating System (x86-64)");
    terminal_writeln("==================================================");
    terminal_writeln("  Architecture : x86-64 Long Mode");
    terminal_writeln("  Bootloader   : Limine (Revision 3 Protocol)");
    terminal_writeln("  Subsystems   :");
    terminal_writeln("    - Physical Page Allocator (Bitmap)");
    terminal_writeln("    - Virtual Memory Manager (4-Level 64-bit Paging)");
    terminal_writeln("    - Dynamic Kernel Heap (kmalloc / kfree)");
    terminal_writeln("    - CPU Descriptors (GDT, TSS with IST)");
    terminal_writeln("    - Interrupt Handling (256-Entry IDT)");
    terminal_writeln("    - 8259 PIC & 8254 PIT (100 Hz Timer)");
    terminal_writeln("    - PS/2 Keyboard Driver & UART Serial Driver");
    terminal_writeln("    - Preemptive Multitasking & Round-Robin Scheduler");
    terminal_writeln("    - Interactive Terminal & Command Shell");
    terminal_writeln("    - User Mode (Ring 3) & System Call Interface");
}

static void cmd_usertest(void) {
    terminal_writeln("Running User Mode (Ring 3) and System Call verification suite...");
    int res = usermode_test_all();
    if (res == 0) {
        terminal_writeln("User mode tests PASSED successfully!");
    } else {
        terminal_writeln("User mode tests FAILED!");
    }
}

static void cmd_ps(void) {
    terminal_writeln("PID   Name             State        TID   Exit   PML4 CR3");
    terminal_writeln("-------------------------------------------------------------------");

    struct process *p = process_get_list();
    while (p != NULL) {
        terminal_write_dec(p->pid);
        if (p->pid < 10) terminal_write("     ");
        else if (p->pid < 100) terminal_write("    ");
        else terminal_write("   ");

        terminal_write(p->name);
        size_t nlen = 0;
        while (p->name[nlen]) nlen++;
        for (size_t sp = nlen; sp < 17; sp++) {
            terminal_write(" ");
        }

        const char *st_str = process_state_to_string(p->state);
        terminal_write(st_str);
        size_t slen = 0;
        while (st_str[slen]) slen++;
        for (size_t sp = slen; sp < 13; sp++) {
            terminal_write(" ");
        }

        if (p->main_thread != NULL) {
            terminal_write_dec(p->main_thread->tid);
            if (p->main_thread->tid < 10) terminal_write("     ");
            else terminal_write("    ");
        } else {
            terminal_write("-     ");
        }

        if (p->exit_code < 0) {
            terminal_write("-");
            terminal_write_dec((uint64_t)(-p->exit_code));
            terminal_write("   ");
        } else {
            terminal_write_dec((uint64_t)p->exit_code);
            terminal_write("      ");
        }

        terminal_write_hex(p->pml4_phys);
        terminal_writeln("");

        p = p->next;
    }
}

static void cmd_proc(const char *args) {
    while (*args == ' ' || *args == '\t') args++;
    if (*args == '\0') {
        terminal_writeln("Usage: proc <pid>");
        return;
    }

    uint32_t pid = 0;
    while (*args >= '0' && *args <= '9') {
        pid = pid * 10 + (*args - '0');
        args++;
    }

    struct process *p = process_get_by_pid((pid_t)pid);
    if (p == NULL) {
        terminal_write("Process with PID ");
        terminal_write_dec(pid);
        terminal_writeln(" not found.");
        return;
    }

    terminal_write("Process Information for PID ");
    terminal_write_dec(p->pid);
    terminal_write(" ('");
    terminal_write(p->name);
    terminal_writeln("'):");

    terminal_write("  State           : ");
    terminal_writeln(process_state_to_string(p->state));

    terminal_write("  Exit Code       : ");
    if (p->exit_code < 0) {
        terminal_write("-");
        terminal_write_dec((uint64_t)(-p->exit_code));
    } else {
        terminal_write_dec((uint64_t)p->exit_code);
    }
    terminal_writeln("");

    terminal_write("  PML4 Phys (CR3) : ");
    terminal_write_hex(p->pml4_phys);
    terminal_writeln("");

    terminal_write("  Main Thread TID : ");
    if (p->main_thread != NULL) {
        terminal_write_dec(p->main_thread->tid);
    } else {
        terminal_write("None");
    }
    terminal_writeln("");

    terminal_write("  User Code VAddr : ");
    terminal_write_hex(p->user_code_vaddr);
    terminal_write(" (Phys: ");
    terminal_write_hex(p->user_code_phys);
    terminal_write(", Pages: ");
    terminal_write_dec(p->user_code_pages);
    terminal_writeln(")");

    terminal_write("  User Stack Base : ");
    terminal_write_hex(p->user_stack_vaddr);
    terminal_write(" (Top: ");
    terminal_write_hex(p->user_stack_top);
    terminal_write(", Phys: ");
    terminal_write_hex(p->user_stack_phys);
    terminal_write(", Pages: ");
    terminal_write_dec(p->user_stack_pages);
    terminal_writeln(")");

    terminal_write("  Run Count       : ");
    terminal_write_dec(p->run_count);
    terminal_writeln("");

    terminal_write("  Total CPU Ticks : ");
    terminal_write_dec(p->total_ticks);
    terminal_writeln("");
}

static void cmd_vmmap(const char *args) {
    while (*args == ' ' || *args == '\t') args++;
    if (*args == '\0') {
        terminal_writeln("Usage: vmmap <pid>");
        return;
    }

    uint32_t pid = 0;
    while (*args >= '0' && *args <= '9') {
        pid = pid * 10 + (*args - '0');
        args++;
    }

    struct process *p = process_get_by_pid((pid_t)pid);
    if (p == NULL) {
        terminal_write("Process with PID ");
        terminal_write_dec(pid);
        terminal_writeln(" not found.");
        return;
    }

    terminal_write("Virtual Memory Map for PID ");
    terminal_write_dec(p->pid);
    terminal_write(" ('");
    terminal_write(p->name);
    terminal_write("', PML4=");
    terminal_write_hex(p->pml4_phys);
    terminal_writeln("):");
    terminal_writeln("Virtual Address Range             Physical Frame       Permissions");
    terminal_writeln("-------------------------------------------------------------------------");

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(p->pml4_phys);
    if (pml4 == NULL) {
        terminal_writeln("  [Error: Unable to translate PML4 to virtual address]");
        return;
    }

    int user_mappings_found = 0;
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(pml4[pml4_i] & PTE_PRESENT)) continue;
        uint64_t *pdpt = (uint64_t *)vmm_phys_to_virt(pml4[pml4_i] & PTE_ADDR_MASK);
        if (pdpt == NULL) continue;

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt[pdpt_i] & PTE_PRESENT)) continue;
            if (pdpt[pdpt_i] & PTE_HUGE) {
                uint64_t vstart = ((uint64_t)pml4_i << 39) | ((uint64_t)pdpt_i << 30);
                terminal_write_hex(vstart);
                terminal_write(" - ");
                terminal_write_hex(vstart + 0x40000000ULL - 1);
                terminal_write("  ");
                terminal_write_hex(pdpt[pdpt_i] & PTE_ADDR_MASK);
                terminal_writeln("  [1GiB, User]");
                user_mappings_found++;
                continue;
            }
            uint64_t *pd = (uint64_t *)vmm_phys_to_virt(pdpt[pdpt_i] & PTE_ADDR_MASK);
            if (pd == NULL) continue;

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd[pd_i] & PTE_PRESENT)) continue;
                if (pd[pd_i] & PTE_HUGE) {
                    uint64_t vstart = ((uint64_t)pml4_i << 39) | ((uint64_t)pdpt_i << 30) | ((uint64_t)pd_i << 21);
                    terminal_write_hex(vstart);
                    terminal_write(" - ");
                    terminal_write_hex(vstart + 0x200000ULL - 1);
                    terminal_write("  ");
                    terminal_write_hex(pd[pd_i] & PTE_ADDR_MASK);
                    terminal_writeln("  [2MiB, User]");
                    user_mappings_found++;
                    continue;
                }
                uint64_t *pt = (uint64_t *)vmm_phys_to_virt(pd[pd_i] & PTE_ADDR_MASK);
                if (pt == NULL) continue;

                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    if (!(pt[pt_i] & PTE_PRESENT)) continue;
                    uint64_t vstart = ((uint64_t)pml4_i << 39) | ((uint64_t)pdpt_i << 30) |
                                     ((uint64_t)pd_i << 21) | ((uint64_t)pt_i << 12);
                    terminal_write_hex(vstart);
                    terminal_write(" - ");
                    terminal_write_hex(vstart + 0x1000ULL - 1);
                    terminal_write("  ");
                    terminal_write_hex(pt[pt_i] & PTE_ADDR_MASK);
                    terminal_write("  [4KiB, ");
                    if (pt[pt_i] & PTE_USER) terminal_write("User, ");
                    else terminal_write("Supervisor, ");
                    if (pt[pt_i] & PTE_WRITABLE) terminal_write("R/W]");
                    else terminal_write("R/O]");
                    terminal_writeln("");
                    user_mappings_found++;
                }
            }
        }
    }

    if (!user_mappings_found) {
        terminal_writeln("  (No lower-half user mappings present)");
    }

    terminal_writeln("0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF  Shared Kernel Space  [Supervisor-Only]");
}

static void cmd_proctest(void) {
    terminal_writeln("Running Process Management & Address Space verification suite...");
    int res = process_test_all();
    if (res == 0) {
        terminal_writeln("Process tests PASSED successfully!");
    } else {
        terminal_writeln("Process tests FAILED!");
    }
}

static void cmd_reboot(void) {
    terminal_writeln("Rebooting machine...");

    /* 1. Attempt 8042 PS/2 controller pulse reset */
    uint8_t status;
    do {
        status = inb(0x64);
        if (status & 0x01) {
            inb(0x60);
        }
    } while (status & 0x02);

    outb(0x64, 0xFE);

    /* 2. Fallback: Force triple fault by loading an empty IDT and invoking interrupt */
    struct idt_ptr null_idt = { .limit = 0, .base = 0 };
    __asm__ volatile ("lidt %0" : : "m"(null_idt));
    __asm__ volatile ("int3");

    /* Infinite halt if reboot fails */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void cmd_ls(void) {
    struct filesystem *fs = fs_root();
    if (!fs->mounted) { terminal_writeln("Filesystem unavailable."); return; }
    struct fs_entry entry;
    for (uint32_t i = 0; fs_list(fs, i, &entry) == FS_OK; i++) {
        terminal_write(entry.name); terminal_write("  ");
        terminal_write_dec(entry.size); terminal_writeln(" bytes");
    }
}

static void cmd_fsinfo(void) {
    struct filesystem *fs = fs_root();
    if (!fs->mounted) { terminal_writeln("Filesystem unavailable (missing disk or invalid EDUFS)."); return; }
    terminal_writeln("EDUFS v1: read-only, flat root directory");
    terminal_write("Device: "); terminal_writeln(fs->device->name);
    terminal_write("Block size: "); terminal_write_dec(BLOCK_SIZE); terminal_writeln(" bytes");
    terminal_write("Device sectors: "); terminal_write_dec(fs->device->sectors); terminal_putchar('\n');
    terminal_write("Filesystem sectors: "); terminal_write_dec(fs->sectors); terminal_putchar('\n');
    terminal_write("Files: "); terminal_write_dec(fs->count); terminal_putchar('\n');
}

static void cmd_cat(const char *args) {
    while (*args == ' ' || *args == '\t') args++;
    char name[FS_NAME_BYTES];
    size_t n = 0;
    while (*args && *args != ' ' && *args != '\t') {
        if (n + 1 >= sizeof(name)) { terminal_writeln("Invalid filename (maximum 31 bytes)."); return; }
        name[n++] = *args++;
    }
    name[n] = 0;
    while (*args == ' ' || *args == '\t') args++;
    if (!n || *args) { terminal_writeln("Usage: cat <name>"); return; }
    struct filesystem *fs = fs_root();
    uint32_t index;
    int result = fs_lookup(fs, name, &index);
    if (result != FS_OK) {
        terminal_writeln(result == FS_INVALID ? "Invalid filename." :
                         result == FS_UNMOUNTED ? "Filesystem unavailable." : "File not found.");
        return;
    }
    char buf[128]; size_t got; uint64_t offset = 0;
    char last = '\n';
    for (;;) {
        if (fs_read(fs, index, offset, buf, sizeof(buf), &got) != FS_OK) {
            terminal_writeln("File read failed."); return;
        }
        if (!got) break;
        for (size_t i = 0; i < got; i++) terminal_putchar(buf[i]);
        last = buf[got - 1];
        offset += got;
    }
    if (last != '\n') terminal_putchar('\n');
}

static void execute_command(char *cmd_line) {
    /* Skip leading spaces */
    while (*cmd_line == ' ' || *cmd_line == '\t') {
        cmd_line++;
    }

    if (*cmd_line == '\0') {
        return;
    }

    /* Extract command name */
    char cmd[64];
    int i = 0;
    while (cmd_line[i] && cmd_line[i] != ' ' && cmd_line[i] != '\t' && i < 63) {
        cmd[i] = cmd_line[i];
        i++;
    }
    cmd[i] = '\0';

    const char *args = &cmd_line[i];

    if (streq(cmd, "ls")) {
        cmd_ls();
    } else if (streq(cmd, "cat")) {
        cmd_cat(args);
    } else if (streq(cmd, "fsinfo")) {
        cmd_fsinfo();
    } else if (streq(cmd, "help")) {
        cmd_help();
    } else if (streq(cmd, "clear")) {
        cmd_clear();
    } else if (streq(cmd, "mem")) {
        cmd_mem();
    } else if (streq(cmd, "heap")) {
        cmd_heap();
    } else if (streq(cmd, "ticks")) {
        cmd_ticks();
    } else if (streq(cmd, "uptime")) {
        cmd_uptime();
    } else if (streq(cmd, "threads")) {
        cmd_threads();
    } else if (streq(cmd, "sched")) {
        cmd_sched();
    } else if (streq(cmd, "yield")) {
        cmd_yield();
    } else if (streq(cmd, "sleep")) {
        cmd_sleep(args);
    } else if (streq(cmd, "ps")) {
        cmd_ps();
    } else if (streq(cmd, "proc")) {
        cmd_proc(args);
    } else if (streq(cmd, "vmmap")) {
        cmd_vmmap(args);
    } else if (streq(cmd, "proctest")) {
        cmd_proctest();
    } else if (streq(cmd, "usertest") || streq(cmd, "ring3")) {
        cmd_usertest();
    } else if (streq(cmd, "about")) {
        cmd_about();
    } else if (streq(cmd, "reboot")) {
        cmd_reboot();
    } else {
        terminal_write("Unknown command: '");
        terminal_write(cmd);
        terminal_writeln("'. Type 'help' for available commands.");
    }
}

void shell_init(void) {
    terminal_writeln("\r\n==================================================");
    terminal_writeln("  Welcome to Educational OS Interactive Shell!");
    terminal_writeln("  Type 'help' to see available commands.");
    terminal_writeln("==================================================");
}

void shell_run(void) {
    char line_buf[SHELL_BUFFER_SIZE];
    uint32_t line_len = 0;

    terminal_write(SHELL_PROMPT);

    for (;;) {
        char ch = keyboard_getchar();

        if (ch == '\n') {
            terminal_putchar('\n');
            line_buf[line_len] = '\0';
            execute_command(line_buf);
            line_len = 0;
            terminal_write(SHELL_PROMPT);
        } else if (ch == '\b') {
            if (line_len > 0) {
                line_len--;
                terminal_backspace();
            }
        } else if (ch >= 32 && ch <= 126) {
            if (line_len < SHELL_BUFFER_SIZE - 1) {
                line_buf[line_len++] = ch;
                terminal_putchar(ch);
            }
        }
    }
}
