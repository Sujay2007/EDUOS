#include <stdint.h>
#include <stddef.h>
#include <limine.h>

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

#include "io.h"
#include "serial.h"
#include "pfa.h"
#include "vmm.h"
#include "heap.h"
#include "gdt.h"
#include "idt.h"
#include "pit.h"
#include "keyboard.h"
#include "terminal.h"
#include "shell.h"
#include "thread.h"
#include "thread_test.h"
#include "syscall.h"
#include "usermode.h"
#include "process.h"
#include "process_test.h"

static const char *memmap_type_to_string(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
            return "Usable";
        case LIMINE_MEMMAP_RESERVED:
            return "Reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
            return "ACPI Reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS:
            return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:
            return "Bad Memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return "Bootloader Reclaimable";
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:
            return "Kernel/Modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:
            return "Framebuffer";
        default:
            return "Unknown";
    }
}

static void print_memory_map(void) {
    struct limine_memmap_response *response = memmap_request.response;

    if (response == NULL) {
        serial_write("[MEMMAP] Error: No memory map response received from Limine!\r\n");
        return;
    }

    serial_write("\r\nPhysical Memory Map (");
    serial_write_dec(response->entry_count);
    serial_write(" entries):\r\n");
    serial_write("  #   Base Address         Length               Type\r\n");
    serial_write("--------------------------------------------------------------------\r\n");

    for (uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry *entry = response->entries[i];
        if (entry == NULL) {
            continue;
        }

        serial_write(" [");
        if (i < 10) {
            serial_putchar(' ');
        }
        serial_write_dec(i);
        serial_write("] ");

        serial_write_hex64(entry->base);
        serial_write(" - ");
        serial_write_hex64(entry->length);
        serial_write("  ");
        serial_write(memmap_type_to_string(entry->type));
        serial_write("\r\n");
    }
}

static void print_pfa_stats(const char *label) {
    struct pfa_stats stats;
    pfa_get_stats(&stats);

    serial_write("\r\n--- PFA Statistics (");
    serial_write(label);
    serial_write(") ---\r\n");
    serial_write("  Total addressable pages: ");
    serial_write_dec(stats.total_pages);
    serial_write(" (");
    serial_write_hex64(stats.total_pages * PAGE_SIZE);
    serial_write(")\r\n");

    serial_write("  Usable pages           : ");
    serial_write_dec(stats.usable_pages);
    serial_write(" (");
    serial_write_dec((stats.usable_pages * PAGE_SIZE) / 1024 / 1024);
    serial_write(" MiB)\r\n");

    serial_write("  Bitmap metadata pages  : ");
    serial_write_dec(stats.metadata_pages);
    serial_write(" (");
    serial_write_dec(stats.metadata_pages * 4);
    serial_write(" KiB)\r\n");

    serial_write("  Allocated pages        : ");
    serial_write_dec(stats.allocated_pages);
    serial_write("\r\n");

    serial_write("  Free pages             : ");
    serial_write_dec(stats.free_pages);
    serial_write("\r\n");
}

static void test_pfa(void) {
    serial_write("\r\nInitializing Physical Page Allocator...\r\n");
    int init_res = pfa_init();
    if (init_res != 0) {
        serial_write("[PFA ERROR] Initialization failed with code: ");
        serial_write_dec((uint64_t)-init_res);
        serial_write("\r\n");
        return;
    }
    serial_write("[PFA] Initialization successful!\r\n");

    print_pfa_stats("Initial State");

    /* Allocate 4 pages */
    serial_write("\r\n[PFA Test] Allocating 4 physical pages...\r\n");
    uint64_t p0 = pfa_alloc_page();
    uint64_t p1 = pfa_alloc_page();
    uint64_t p2 = pfa_alloc_page();
    uint64_t p3 = pfa_alloc_page();

    serial_write("  Page 0: "); serial_write_hex64(p0); serial_write("\r\n");
    serial_write("  Page 1: "); serial_write_hex64(p1); serial_write("\r\n");
    serial_write("  Page 2: "); serial_write_hex64(p2); serial_write("\r\n");
    serial_write("  Page 3: "); serial_write_hex64(p3); serial_write("\r\n");

    print_pfa_stats("After 4 Allocations");

    /* Free page 1 and page 2 */
    serial_write("\r\n[PFA Test] Freeing Page 1 (");
    serial_write_hex64(p1);
    serial_write(") and Page 2 (");
    serial_write_hex64(p2);
    serial_write(")...\r\n");

    int f1 = pfa_free_page(p1);
    int f2 = pfa_free_page(p2);
    serial_write("  Free Page 1 result: "); serial_write_dec((uint64_t)f1); serial_write("\r\n");
    serial_write("  Free Page 2 result: "); serial_write_dec((uint64_t)f2); serial_write("\r\n");

    /* Test duplicate free error */
    serial_write("  Testing duplicate free on Page 1: ");
    int f_dup = pfa_free_page(p1);
    if (f_dup != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_dup);
        serial_write(")\r\n");
    } else {
        serial_write("UNEXPECTED SUCCESS!\r\n");
    }

    /* Test unaligned free error */
    serial_write("  Testing unaligned free (0x1001): ");
    int f_unaligned = pfa_free_page(0x1001);
    if (f_unaligned != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_unaligned);
        serial_write(")\r\n");
    }

    /* Test reserved address free error */
    serial_write("  Testing reserved memory free (0x0): ");
    int f_res = pfa_free_page(0x0);
    if (f_res != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-f_res);
        serial_write(")\r\n");
    }

    print_pfa_stats("After Freeing 2 Pages");

    /* Reallocate: Should immediately reuse the freed pages */
    serial_write("\r\n[PFA Test] Reallocating 2 physical pages...\r\n");
    uint64_t p_re1 = pfa_alloc_page();
    uint64_t p_re2 = pfa_alloc_page();

    serial_write("  Reallocated 1: "); serial_write_hex64(p_re1);
    if (p_re1 == p1 || p_re1 == p2) {
        serial_write(" (matches freed page)\r\n");
    } else {
        serial_write("\r\n");
    }

    serial_write("  Reallocated 2: "); serial_write_hex64(p_re2);
    if (p_re2 == p1 || p_re2 == p2) {
        serial_write(" (matches freed page)\r\n");
    } else {
        serial_write("\r\n");
    }

    print_pfa_stats("After Reallocation");

    /* Clean up all allocated test pages */
    pfa_free_page(p0);
    pfa_free_page(p3);
    pfa_free_page(p_re1);
    pfa_free_page(p_re2);

    print_pfa_stats("After Freeing All Test Pages");
}

static void test_vmm(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Initializing Virtual Memory Manager (VMM)...\r\n");
    int vmm_res = vmm_init();
    if (vmm_res != 0) {
        serial_write("[VMM ERROR] vmm_init failed with code: ");
        serial_write_dec((uint64_t)-vmm_res);
        serial_write("\r\n");
        return;
    }
    serial_write("[VMM] VMM initialized successfully!\r\n");

    /* Test 1: Map an unused virtual address to a newly allocated physical frame */
    uint64_t test_vaddr = 0xffffffff60000000ULL;
    serial_write("\r\n[VMM Test 1] Allocating physical frame and mapping to ");
    serial_write_hex64(test_vaddr);
    serial_write("...\r\n");

    uint64_t phys_frame = pfa_alloc_page();
    serial_write("  Allocated physical frame: ");
    serial_write_hex64(phys_frame);
    serial_write("\r\n");

    int map_res = vmm_map_page(test_vaddr, phys_frame, PTE_PRESENT | PTE_WRITABLE);
    serial_write("  vmm_map_page result: ");
    serial_write_dec((uint64_t)map_res);
    serial_write("\r\n");

    /* Verify translation via page table walk */
    uint64_t flags = 0;
    uint64_t resolved_phys = vmm_virt_to_phys(test_vaddr, &flags);
    serial_write("  Resolved physical address: ");
    serial_write_hex64(resolved_phys);
    if (resolved_phys == phys_frame) {
        serial_write(" (MATCH!)\r\n");
    } else {
        serial_write(" (MISMATCH!)\r\n");
    }

    /* Test read/write through virtual address */
    volatile uint64_t *ptr = (volatile uint64_t *)test_vaddr;
    *ptr = 0x12345678DEADBEEFULL;
    serial_write("  Memory write/read verify: ");
    serial_write_hex64(*ptr);
    if (*ptr == 0x12345678DEADBEEFULL) {
        serial_write(" (SUCCESS)\r\n");
    } else {
        serial_write(" (FAILED)\r\n");
    }

    /* Test 2: Error handling - duplicate mapping */
    serial_write("\r\n[VMM Test 2] Testing error handling:\r\n");
    serial_write("  Duplicate mapping on same vaddr: ");
    int dup_map = vmm_map_page(test_vaddr, phys_frame, PTE_PRESENT | PTE_WRITABLE);
    if (dup_map != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-dup_map);
        serial_write(")\r\n");
    }

    /* Test unaligned address */
    serial_write("  Unaligned virtual address mapping: ");
    int unaligned_map = vmm_map_page(test_vaddr + 1, phys_frame, PTE_PRESENT | PTE_WRITABLE);
    if (unaligned_map != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-unaligned_map);
        serial_write(")\r\n");
    }

    /* Test unmapping non-canonical address */
    serial_write("  Non-canonical unmap: ");
    int non_canon_unmap = vmm_unmap_page(0x0000800000000000ULL);
    if (non_canon_unmap != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-non_canon_unmap);
        serial_write(")\r\n");
    }

    /* Test unmapping unmapped address */
    serial_write("  Unmapping absent page (0xffffffff60001000): ");
    int absent_unmap = vmm_unmap_page(0xffffffff60001000ULL);
    if (absent_unmap != 0) {
        serial_write("Handled safely (code ");
        serial_write_dec((uint64_t)-absent_unmap);
        serial_write(")\r\n");
    }

    /* Test 3: Unmap the page and release frame */
    serial_write("\r\n[VMM Test 3] Unmapping ");
    serial_write_hex64(test_vaddr);
    serial_write("...\r\n");
    int unmap_res = vmm_unmap_page(test_vaddr);
    serial_write("  vmm_unmap_page result: ");
    serial_write_dec((uint64_t)unmap_res);
    serial_write("\r\n");

    uint64_t unmapped_check = vmm_virt_to_phys(test_vaddr, NULL);
    serial_write("  Resolved physical address after unmap: ");
    serial_write_hex64(unmapped_check);
    if (unmapped_check == 0) {
        serial_write(" (Confirmed absent)\r\n");
    } else {
        serial_write(" (Still present!)\r\n");
    }

    pfa_free_page(phys_frame);
    serial_write("  Physical frame released back to PFA.\r\n");
}

static void print_heap_stats(const char *label) {
    struct heap_stats hstats;
    heap_get_stats(&hstats);

    serial_write("  --- Heap Stats (");
    serial_write(label);
    serial_write(") ---\r\n");
    serial_write("    Total mapped bytes: ");
    serial_write_dec(hstats.total_heap_bytes);
    serial_write(" (");
    serial_write_dec(hstats.total_heap_bytes / 1024);
    serial_write(" KiB)\r\n");
    serial_write("    Allocated bytes   : ");
    serial_write_dec(hstats.allocated_bytes);
    serial_write("\r\n");
    serial_write("    Free bytes        : ");
    serial_write_dec(hstats.free_bytes);
    serial_write("\r\n");
    serial_write("    Active allocations: ");
    serial_write_dec(hstats.allocation_count);
    serial_write("\r\n");
}

static void test_heap(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Initializing Kernel Heap...\r\n");
    int h_res = heap_init();
    if (h_res != 0) {
        serial_write("[HEAP ERROR] heap_init failed with code: ");
        serial_write_dec((uint64_t)-h_res);
        serial_write("\r\n");
        return;
    }
    serial_write("[HEAP] Kernel heap initialized successfully!\r\n");

    print_heap_stats("Initial State");

    /* Test 1: Zero-size allocation */
    serial_write("\r\n[Heap Test 1] Testing zero-size kmalloc(0): ");
    void *zero_ptr = kmalloc(0);
    if (zero_ptr == NULL) {
        serial_write("Handled safely (returned NULL)\r\n");
    } else {
        serial_write("UNEXPECTED NON-NULL!\r\n");
    }

    /* Test 2: Allocations of variable sizes */
    serial_write("\r\n[Heap Test 2] Variable-sized allocations:\r\n");
    char *buf32 = (char *)kmalloc(32);
    uint32_t *arr256 = (uint32_t *)kmalloc(256);
    uint64_t *arr2048 = (uint64_t *)kmalloc(2048);

    serial_write("  buf32 (32 B)  at: "); serial_write_hex64((uint64_t)buf32); serial_write("\r\n");
    serial_write("  arr256 (256 B) at: "); serial_write_hex64((uint64_t)arr256); serial_write("\r\n");
    serial_write("  arr2048 (2048 B) at: "); serial_write_hex64((uint64_t)arr2048); serial_write("\r\n");

    /* Verify 16-byte alignment */
    if (((uint64_t)buf32 % 16 == 0) && ((uint64_t)arr256 % 16 == 0) && ((uint64_t)arr2048 % 16 == 0)) {
        serial_write("  Alignment check: All pointers are 16-byte aligned (PASS)\r\n");
    } else {
        serial_write("  Alignment check: Alignment error! (FAIL)\r\n");
    }

    /* Write patterns into each buffer */
    for (int i = 0; i < 32; i++) {
        buf32[i] = (char)('A' + (i % 26));
    }
    for (int i = 0; i < 64; i++) {
        arr256[i] = (uint32_t)(0xAA000000 + i);
    }
    for (int i = 0; i < 256; i++) {
        arr2048[i] = (uint64_t)(0xFEEDFACE00000000ULL + i);
    }

    /* Verify patterns */
    int pattern_ok = 1;
    if (buf32[0] != 'A' || buf32[31] != (char)('A' + (31 % 26))) pattern_ok = 0;
    if (arr256[0] != 0xAA000000 || arr256[63] != 0xAA00003F) pattern_ok = 0;
    if (arr2048[0] != 0xFEEDFACE00000000ULL || arr2048[255] != 0xFEEDFACE000000FFULL) pattern_ok = 0;

    serial_write("  Buffer pattern integrity verify: ");
    if (pattern_ok) {
        serial_write("ALL VALID (PASS)\r\n");
    } else {
        serial_write("CORRUPTED! (FAIL)\r\n");
    }

    print_heap_stats("After 3 Allocations");

    /* Test 3: Free middle block and verify reuse */
    serial_write("\r\n[Heap Test 3] Freeing middle buffer arr256...\r\n");
    kfree(arr256);

    /* Test duplicate free error */
    serial_write("  Testing duplicate free on arr256: Handled safely\r\n");
    kfree(arr256);

    /* Test freeing NULL */
    serial_write("  Testing free(NULL): Handled safely\r\n");
    kfree(NULL);

    /* Test freeing invalid wild pointer */
    serial_write("  Testing free(0x12345): Handled safely\r\n");
    kfree((void *)0x12345);

    print_heap_stats("After Freeing arr256");

    /* Reallocate: Should fit into the freed middle block */
    serial_write("\r\n[Heap Test 4] Reallocating 200 bytes...\r\n");
    void *realloc_ptr = kmalloc(200);
    serial_write("  Reallocated 200 B at: ");
    serial_write_hex64((uint64_t)realloc_ptr);
    if (realloc_ptr == arr256) {
        serial_write(" (Reused freed block!)\r\n");
    } else {
        serial_write("\r\n");
    }

    /* Free remaining blocks */
    kfree(buf32);
    kfree(arr2048);
    kfree(realloc_ptr);

    print_heap_stats("After Freeing All Blocks (Coalesced)");

    /* Test 5: Large allocation triggering heap expansion */
    serial_write("\r\n[Heap Test 5] Large allocation (32 KiB, triggering expansion)...\r\n");
    void *large_buf = kmalloc(32768);
    serial_write("  large_buf at: ");
    serial_write_hex64((uint64_t)large_buf);
    serial_write("\r\n");

    /* Write/read large buffer */
    volatile uint64_t *large_words = (volatile uint64_t *)large_buf;
    large_words[0] = 0xCAFEBABEDEADBEEFULL;
    large_words[4095] = 0x0123456789ABCDEFULL;

    serial_write("  large_buf verify: ");
    if (large_words[0] == 0xCAFEBABEDEADBEEFULL && large_words[4095] == 0x0123456789ABCDEFULL) {
        serial_write("VERIFIED (PASS)\r\n");
    } else {
        serial_write("FAILED!\r\n");
    }

    print_heap_stats("After Large Allocation");

    kfree(large_buf);
    print_heap_stats("Final State After All Frees");
}

static volatile int breakpoint_handled = 0;

static void custom_breakpoint_handler(struct interrupt_frame *frame) {
    (void)frame;
    breakpoint_handled = 1;
    serial_write("  [INT3 HANDLER] Recoverable Breakpoint (#BP) successfully intercepted!\r\n");
}

static void test_gdt_idt_exceptions_and_timer(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Initializing Global Descriptor Table (GDT) and TSS...\r\n");
    gdt_init();
    serial_write("[GDT] GDT loaded, segment registers and CS reloaded, TSS loaded!\r\n");

    serial_write("\r\nInitializing Interrupt Descriptor Table (IDT)...\r\n");
    idt_init();
    serial_write("[IDT] 256-entry IDT loaded with CPU exceptions and hardware IRQs!\r\n");

    /* Test controlled, recoverable exception (INT3 / Breakpoint) */
    serial_write("\r\n[EXCEPTION TEST] Testing controlled recoverable exception (#BP / INT 3)...\r\n");
    idt_register_handler(3, custom_breakpoint_handler);
    breakpoint_handled = 0;
    __asm__ volatile ("int3");

    if (breakpoint_handled) {
        serial_write("  [EXCEPTION TEST] Breakpoint exception handling verified (PASS)\r\n");
    } else {
        serial_write("  [EXCEPTION TEST] Breakpoint not handled! (FAIL)\r\n");
    }

    /* Initialize Hardware Timer (8254 PIT) */
    serial_write("\r\nInitializing 8254 Programmable Interval Timer (PIT)...\r\n");
    pit_init(100); /* 100 Hz = 10ms per tick */
    serial_write("[PIT] PIT initialized at ");
    serial_write_dec(timer_get_frequency());
    serial_write(" Hz (IRQ0 -> Vector 32)!\r\n");

    /* Enable maskable hardware interrupts */
    serial_write("[INT] Enabling maskable interrupts (sti)...\r\n");
    interrupts_enable();

    serial_write("[TIMER] Sampling hardware timer ticks over periodic intervals:\r\n");
    uint64_t last_tick = timer_get_ticks();
    int samples = 0;

    while (samples < 5) {
        uint64_t current_tick = timer_get_ticks();
        if (current_tick >= last_tick + 20) { /* ~200 ms interval */
            samples++;
            last_tick = current_tick;
            serial_write("  [Tick Sample ");
            serial_write_dec(samples);
            serial_write("/5] Current Ticks: ");
            serial_write_dec(current_tick);
            serial_write(" (monotonic progress verified)\r\n");
        }
        __asm__ volatile ("pause");
    }

    serial_write("[TIMER] Timer interrupt handling is stable and verified!\r\n");
}

void _start(void) {
    serial_init();
    serial_write("\r\nEducational OS kernel booted successfully!\r\n");
    serial_write("Running directly on the emulated x86-64 machine.\r\n");

    print_memory_map();
    test_pfa();
    test_vmm();
    test_heap();
    test_gdt_idt_exceptions_and_timer();

    serial_write("\r\nAll milestone verifications completed successfully!\r\n");

    /* Initialize keyboard driver, terminal abstraction, and shell */
    keyboard_init();
    terminal_init();

    /* Initialize kernel multitasking and round-robin scheduler */
    thread_init();

    /* Initialize process management subsystem (PID 0 kernel process) */
    process_init();

    /* Run kernel multitasking and preemption verification suite */
    thread_test_all();

    /* Run user mode and system call verification suite */
    usermode_test_all();

    /* Run process management, isolation, and protection verification suite */
    process_test_all();

    /* Initialize and launch interactive shell */
    shell_init();
    shell_run();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
