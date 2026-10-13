#include "vmm.h"
#include "pfa.h"
#include <limine.h>

extern volatile struct limine_hhdm_request hhdm_request;

/* Root PML4 physical address loaded from CR3 */
static uint64_t kernel_pml4_phys = 0;

int vmm_is_canonical(uint64_t virt_addr) {
    /*
     * In 4-level paging on x86-64, bits 47 through 63 must all be 0s or all 1s.
     * Shift right arithmetic by 47 gives 0 or -1 (0xFFFFFFFFFFFFFFFF).
     */
    int64_t sign_extended = ((int64_t)virt_addr) >> 47;
    return (sign_extended == 0 || sign_extended == -1);
}

void vmm_invlpg(uint64_t virt_addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void *vmm_phys_to_virt(uint64_t phys_addr) {
    if (hhdm_request.response == NULL) {
        return NULL;
    }
    return (void *)(phys_addr + hhdm_request.response->offset);
}

int vmm_init(void) {
    if (hhdm_request.response == NULL) {
        return -1;
    }

    uint64_t cr3 = read_cr3();
    /* Mask out PCID or caching flags in low 12 bits */
    kernel_pml4_phys = cr3 & PTE_ADDR_MASK;

    if (kernel_pml4_phys == 0) {
        return -2;
    }

    return 0;
}

/*
 * Helper to get or create the next-level table.
 * If create is 1 and the entry is absent, allocates a new frame from PFA, zeroes it,
 * and populates the entry with (frame_phys | PTE_PRESENT | PTE_WRITABLE).
 */
static uint64_t *get_next_level(uint64_t *table, uint64_t index, int create) {
    uint64_t entry = table[index];

    if (entry & PTE_PRESENT) {
        /* Large/Huge pages (1 GiB or 2 MiB) at intermediate levels cannot be walked down to 4 KiB */
        if (entry & PTE_HUGE) {
            return NULL;
        }
        uint64_t next_table_phys = entry & PTE_ADDR_MASK;
        return (uint64_t *)vmm_phys_to_virt(next_table_phys);
    }

    if (!create) {
        return NULL;
    }

    uint64_t new_table_phys = pfa_alloc_page();
    if (new_table_phys == 0) {
        return NULL;
    }

    uint64_t *new_table_virt = (uint64_t *)vmm_phys_to_virt(new_table_phys);
    if (new_table_virt == NULL) {
        pfa_free_page(new_table_phys);
        return NULL;
    }

    /* Zero out the new page table entries */
    for (int i = 0; i < 512; i++) {
        new_table_virt[i] = 0;
    }

    /*
     * Set intermediate entry permissions to Present + Writable.
     * Leaf permissions (PTE) will govern the final permissions.
     */
    table[index] = new_table_phys | PTE_PRESENT | PTE_WRITABLE;

    return new_table_virt;
}

int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if ((virt_addr & (PAGE_SIZE - 1)) != 0 || (phys_addr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    if (!vmm_is_canonical(virt_addr)) {
        return -1;
    }

    if (kernel_pml4_phys == 0) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(kernel_pml4_phys);
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 1);
    if (pdpt == NULL) {
        return -3;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 1);
    if (pd == NULL) {
        return -3;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 1);
    if (pt == NULL) {
        return -3;
    }

    /* Check if leaf PTE is already mapped */
    if (pt[pt_i] & PTE_PRESENT) {
        return -2;
    }

    pt[pt_i] = (phys_addr & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    vmm_invlpg(virt_addr);
    return 0;
}

int vmm_unmap_page(uint64_t virt_addr) {
    if ((virt_addr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    if (!vmm_is_canonical(virt_addr)) {
        return -1;
    }

    if (kernel_pml4_phys == 0) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(kernel_pml4_phys);
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 0);
    if (pdpt == NULL) {
        return -2;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 0);
    if (pd == NULL) {
        return -2;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 0);
    if (pt == NULL) {
        return -2;
    }

    if (!(pt[pt_i] & PTE_PRESENT)) {
        return -2;
    }

    pt[pt_i] = 0;
    vmm_invlpg(virt_addr);

    return 0;
}

uint64_t vmm_virt_to_phys(uint64_t virt_addr, uint64_t *out_flags) {
    if (!vmm_is_canonical(virt_addr) || kernel_pml4_phys == 0) {
        return 0;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(kernel_pml4_phys);
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 0);
    if (pdpt == NULL) {
        return 0;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 0);
    if (pd == NULL) {
        return 0;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 0);
    if (pt == NULL) {
        return 0;
    }

    uint64_t pte = pt[pt_i];
    if (!(pte & PTE_PRESENT)) {
        return 0;
    }

    if (out_flags != NULL) {
        *out_flags = pte & ~PTE_ADDR_MASK;
    }

    return (pte & PTE_ADDR_MASK) | (virt_addr & (PAGE_SIZE - 1));
}