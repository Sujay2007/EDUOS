#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096ULL

/* Page Table Entry Flags (x86-64) */
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_WRITETHROUGH (1ULL << 3)
#define PTE_NOCACHE   (1ULL << 4)
#define PTE_ACCESSED  (1ULL << 5)
#define PTE_DIRTY     (1ULL << 6)
#define PTE_HUGE      (1ULL << 7)
#define PTE_GLOBAL    (1ULL << 8)
#define PTE_NX        (1ULL << 63)

/* Helper macros for page table indices */
#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1FFULL)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1FFULL)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1FFULL)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1FFULL)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/*
 * Initialize the Virtual Memory Manager.
 * Detects the active PML4 from CR3 and ensures direct physical-to-virtual
 * mapping access is operational.
 * Returns 0 on success, non-zero on failure.
 */
int vmm_init(void);

/*
 * Map a single 4 KiB virtual page to a physical page frame.
 * Both virt_addr and phys_addr must be 4 KiB-aligned and virt_addr canonical.
 * Flags should include PTE_PRESENT and optional flags like PTE_WRITABLE.
 *
 * Returns:
 *   0 on success
 *  -1 for invalid/unaligned parameters
 *  -2 if page is already mapped
 *  -3 if physical frame allocation for intermediate tables fails
 */
int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/*
 * Unmap a single 4 KiB virtual page.
 * Invalidates the TLB for the unmapped virtual address.
 * Does NOT free the underlying physical page frame (caller manages frame lifetime).
 *
 * Returns:
 *   0 on success
 *  -1 for invalid/unaligned parameters
 *  -2 if page is not mapped (absent)
 */
int vmm_unmap_page(uint64_t virt_addr);

/*
 * Look up the physical address currently mapped to a virtual address.
 * If out_flags is non-NULL, stores the leaf PTE flags.
 *
 * Returns:
 *   Physical address (page-aligned) if mapped, or 0 if unmapped.
 */
uint64_t vmm_virt_to_phys(uint64_t virt_addr, uint64_t *out_flags);

/*
 * Check whether a 64-bit virtual address is canonical on x86-64 (48-bit sign-extended).
 */
int vmm_is_canonical(uint64_t virt_addr);

/*
 * Invalidate a single TLB entry for a given virtual address.
 */
void vmm_invlpg(uint64_t virt_addr);

/*
 * Convert physical address to virtual address using the Higher Half Direct Map (HHDM).
 */
void *vmm_phys_to_virt(uint64_t phys_addr);

#endif /* VMM_H */
