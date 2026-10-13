#ifndef PFA_H
#define PFA_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096ULL

struct pfa_stats {
    uint64_t total_pages;    /* Total addressable pages up to max physical address */
    uint64_t usable_pages;   /* Total pages in usable RAM regions */
    uint64_t allocated_pages;/* Currently allocated pages */
    uint64_t free_pages;     /* Currently free usable pages */
    uint64_t metadata_pages; /* Pages reserved specifically for bitmap metadata */
};

/*
 * Initialize the physical page allocator using Limine memory map and HHDM offset.
 * Returns 0 on success, non-zero on failure.
 */
int pfa_init(void);

/*
 * Allocate one 4 KiB physical page frame.
 * Returns 64-bit physical address of the page, or 0 on failure / exhaustion.
 */
uint64_t pfa_alloc_page(void);

/*
 * Free a previously allocated 4 KiB physical page frame.
 * Returns 0 on success, non-zero if the address is invalid, already free, or reserved.
 */
int pfa_free_page(uint64_t phys_addr);

/*
 * Retrieve allocator statistics.
 */
void pfa_get_stats(struct pfa_stats *stats);

#endif /* PFA_H */
