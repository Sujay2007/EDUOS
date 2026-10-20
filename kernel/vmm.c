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

static inline void write_cr3(uint64_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

uint64_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}

int vmm_query_mapping(uint64_t root, uint64_t address, struct vmm_mapping *out) {
    if (!out || !root || (root & ~PTE_ADDR_MASK) || !vmm_is_canonical(address)) return -1;
    out->physical = out->page_size = out->flags = 0;
    uint64_t permissions = PTE_PRESENT | PTE_USER | PTE_WRITABLE;
    uint64_t table_phys = root;
    for (int level = 4; level >= 1; level--) {
        const uint64_t *table = vmm_phys_to_virt(table_phys);
        if (!table) return -1;
        unsigned shift = 12 + (level - 1) * 9;
        uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & PTE_PRESENT)) return -1;
        if (!(entry & PTE_USER)) permissions &= ~PTE_USER;
        if (!(entry & PTE_WRITABLE)) permissions &= ~PTE_WRITABLE;
        permissions |= entry & PTE_NX;
        if (level == 1 || ((level == 2 || level == 3) && (entry & PTE_HUGE))) {
            uint64_t size = 1ULL << shift;
            out->physical = (entry & PTE_ADDR_MASK & ~(size - 1)) | (address & (size - 1));
            out->page_size = size;
            out->flags = permissions;
            return 0;
        }
        if (level == 4 && (entry & PTE_HUGE)) return -1;
        table_phys = entry & PTE_ADDR_MASK;
        if (!table_phys) return -1;
    }
    return -1;
}

uint64_t vmm_get_current_pml4(void) {
    uint64_t cr3 = read_cr3();
    return cr3 & PTE_ADDR_MASK;
}

void vmm_set_current_pml4(uint64_t pml4_phys) {
    if (pml4_phys == 0) {
        pml4_phys = kernel_pml4_phys;
    }
    write_cr3(pml4_phys);
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

    /* Enforce supervisor-only permissions on all higher-half kernel entries */
    uint64_t *k_pml4 = (uint64_t *)vmm_phys_to_virt(kernel_pml4_phys);
    if (k_pml4 != NULL) {
        for (int i = 256; i < 512; i++) {
            k_pml4[i] &= ~PTE_USER;
        }
    }

    return 0;
}

/*
 * Helper to get or create the next-level table.
 * If create is 1 and the entry is absent, allocates a new frame from PFA, zeroes it,
 * and populates the entry with (frame_phys | PTE_PRESENT | PTE_WRITABLE [| PTE_USER]).
 */
static uint64_t *get_next_level(uint64_t *table, uint64_t index, int create, uint64_t flags) {
    uint64_t entry = table[index];

    if (entry & PTE_PRESENT) {
        /* Large/Huge pages (1 GiB or 2 MiB) at intermediate levels cannot be walked down to 4 KiB */
        if (entry & PTE_HUGE) {
            return NULL;
        }
        /* If mapping a user page, ensure intermediate table allows user access */
        if (flags & PTE_USER) {
            table[index] |= PTE_USER;
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
    uint64_t entry_flags = PTE_PRESENT | PTE_WRITABLE;
    if (flags & PTE_USER) {
        entry_flags |= PTE_USER;
    }
    table[index] = new_table_phys | entry_flags;

    return new_table_virt;
}

uint64_t vmm_create_address_space(void) {
    if (kernel_pml4_phys == 0) {
        return 0;
    }

    uint64_t pml4_phys = pfa_alloc_page();
    if (pml4_phys == 0) {
        return 0;
    }

    uint64_t *pml4_virt = (uint64_t *)vmm_phys_to_virt(pml4_phys);
    uint64_t *k_pml4_virt = (uint64_t *)vmm_phys_to_virt(kernel_pml4_phys);
    if (pml4_virt == NULL || k_pml4_virt == NULL) {
        pfa_free_page(pml4_phys);
        return 0;
    }

    /* Zero out the lower half (indices 0..255) for user address space */
    for (int i = 0; i < 256; i++) {
        pml4_virt[i] = 0;
    }

    /* Share higher half (indices 256..511) supervisor-only kernel mappings */
    for (int i = 256; i < 512; i++) {
        pml4_virt[i] = k_pml4_virt[i] & ~PTE_USER;
    }

    return pml4_phys;
}

void vmm_destroy_address_space(uint64_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == kernel_pml4_phys) {
        return; /* Never destroy kernel PML4 */
    }

    /* If currently active, switch back to kernel PML4 first */
    if (vmm_get_current_pml4() == pml4_phys) {
        vmm_set_current_pml4(kernel_pml4_phys);
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(pml4_phys);
    if (pml4 == NULL) {
        return;
    }

    /* Only traverse and free lower half (0..255) user page tables */
    for (int i = 0; i < 256; i++) {
        uint64_t pml4_entry = pml4[i];
        if (!(pml4_entry & PTE_PRESENT)) {
            continue;
        }

        uint64_t pdpt_phys = pml4_entry & PTE_ADDR_MASK;
        uint64_t *pdpt = (uint64_t *)vmm_phys_to_virt(pdpt_phys);
        if (pdpt != NULL) {
            for (int j = 0; j < 512; j++) {
                uint64_t pdpt_entry = pdpt[j];
                if (!(pdpt_entry & PTE_PRESENT) || (pdpt_entry & PTE_HUGE)) {
                    continue;
                }

                uint64_t pd_phys = pdpt_entry & PTE_ADDR_MASK;
                uint64_t *pd = (uint64_t *)vmm_phys_to_virt(pd_phys);
                if (pd != NULL) {
                    for (int k = 0; k < 512; k++) {
                        uint64_t pd_entry = pd[k];
                        if (!(pd_entry & PTE_PRESENT) || (pd_entry & PTE_HUGE)) {
                            continue;
                        }

                        uint64_t pt_phys = pd_entry & PTE_ADDR_MASK;
                        pfa_free_page(pt_phys);
                    }
                    pfa_free_page(pd_phys);
                }
            }
            pfa_free_page(pdpt_phys);
        }
    }

    /* Free the root PML4 frame itself */
    pfa_free_page(pml4_phys);
}

int vmm_map_page_pml4(uint64_t pml4_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if ((virt_addr & (PAGE_SIZE - 1)) != 0 || (phys_addr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    if (!vmm_is_canonical(virt_addr)) {
        return -1;
    }

    if (flags & PTE_USER) {
        /* User mappings must strictly reside in lower canonical address space (< 0x0000800000000000) */
        if (virt_addr >= 0x0000800000000000ULL) {
            return -1;
        }
    }

    if (pml4_phys == 0) {
        pml4_phys = kernel_pml4_phys;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(pml4_phys);
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 1, flags);
    if (pdpt == NULL) {
        return -3;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 1, flags);
    if (pd == NULL) {
        return -3;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 1, flags);
    if (pt == NULL) {
        return -3;
    }

    /* Check if leaf PTE is already mapped */
    if (pt[pt_i] & PTE_PRESENT) {
        return -2;
    }

    pt[pt_i] = (phys_addr & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    /* Invalidate TLB if modifying currently active address space */
    if (vmm_get_current_pml4() == pml4_phys) {
        vmm_invlpg(virt_addr);
    }
    return 0;
}

int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    return vmm_map_page_pml4(vmm_get_current_pml4(), virt_addr, phys_addr, flags);
}

int vmm_unmap_page_pml4(uint64_t pml4_phys, uint64_t virt_addr) {
    if ((virt_addr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    if (!vmm_is_canonical(virt_addr)) {
        return -1;
    }

    if (pml4_phys == 0) {
        pml4_phys = kernel_pml4_phys;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(pml4_phys);
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 0, 0);
    if (pdpt == NULL) {
        return -2;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 0, 0);
    if (pd == NULL) {
        return -2;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 0, 0);
    if (pt == NULL) {
        return -2;
    }

    if (!(pt[pt_i] & PTE_PRESENT)) {
        return -2;
    }

    pt[pt_i] = 0;

    if (vmm_get_current_pml4() == pml4_phys) {
        vmm_invlpg(virt_addr);
    }

    return 0;
}

int vmm_unmap_page(uint64_t virt_addr) {
    return vmm_unmap_page_pml4(vmm_get_current_pml4(), virt_addr);
}

uint64_t vmm_virt_to_phys_pml4(uint64_t pml4_phys, uint64_t virt_addr, uint64_t *out_flags) {
    if (!vmm_is_canonical(virt_addr)) {
        return 0;
    }

    if (pml4_phys == 0) {
        pml4_phys = kernel_pml4_phys;
    }

    uint64_t *pml4 = (uint64_t *)vmm_phys_to_virt(pml4_phys);
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i   = PD_INDEX(virt_addr);
    uint64_t pt_i   = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_next_level(pml4, pml4_i, 0, 0);
    if (pdpt == NULL) {
        return 0;
    }

    uint64_t *pd = get_next_level(pdpt, pdpt_i, 0, 0);
    if (pd == NULL) {
        return 0;
    }

    uint64_t *pt = get_next_level(pd, pd_i, 0, 0);
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

uint64_t vmm_virt_to_phys(uint64_t virt_addr, uint64_t *out_flags) {
    return vmm_virt_to_phys_pml4(vmm_get_current_pml4(), virt_addr, out_flags);
}
