#include "pfa.h"
#include <limine.h>

extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;

/*
 * Bitmap:
 * Each bit corresponds to one 4 KiB physical page frame:
 *   Bit = 1: Allocated, reserved, or non-usable.
 *   Bit = 0: Free and usable.
 *
 * This design ensures that unmapped holes and reserved regions
 * start marked as 1 (allocated/reserved) and can never be mistakenly
 * allocated or freed.
 */
static uint8_t *bitmap = NULL;
static uint64_t total_page_count = 0;
static uint64_t usable_page_count = 0;
static uint64_t free_page_count = 0;
static uint64_t metadata_page_count = 0;
static uint64_t last_alloc_index = 0;

static inline void bitmap_set(uint64_t page_idx) {
    bitmap[page_idx / 8] |= (uint8_t)(1 << (page_idx % 8));
}

static inline void bitmap_clear(uint64_t page_idx) {
    bitmap[page_idx / 8] &= (uint8_t)~(1 << (page_idx % 8));
}

static inline int bitmap_test(uint64_t page_idx) {
    return (bitmap[page_idx / 8] & (uint8_t)(1 << (page_idx % 8))) != 0;
}

static void *phys_to_virt(uint64_t phys_addr) {
    if (hhdm_request.response == NULL) {
        return NULL;
    }
    return (void *)(phys_addr + hhdm_request.response->offset);
}

int pfa_init(void) {
    struct limine_memmap_response *mmap = memmap_request.response;
    if (mmap == NULL || mmap->entries == NULL || mmap->entry_count == 0) {
        return -1;
    }

    if (hhdm_request.response == NULL) {
        return -2;
    }

    uint64_t max_phys_addr = 0;

    /* Find the highest physical address among usable entries */
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];
        if (entry == NULL) {
            continue;
        }
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = entry->base + entry->length;
            if (top > max_phys_addr) {
                max_phys_addr = top;
            }
        }
    }

    if (max_phys_addr == 0) {
        return -3;
    }

    total_page_count = (max_phys_addr + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_size_bytes = (total_page_count + 7) / 8;
    metadata_page_count = (bitmap_size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t metadata_size_bytes = metadata_page_count * PAGE_SIZE;

    /*
     * Find a usable region with enough contiguous space to store the bitmap metadata.
     */
    uint64_t bitmap_phys_addr = 0;
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        /* Ensure page alignment */
        uint64_t aligned_base = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (aligned_base >= entry->base + entry->length) {
            continue;
        }

        uint64_t usable_len = (entry->base + entry->length) - aligned_base;
        if (usable_len >= metadata_size_bytes) {
            bitmap_phys_addr = aligned_base;
            break;
        }
    }

    if (bitmap_phys_addr == 0) {
        return -4;
    }

    bitmap = (uint8_t *)phys_to_virt(bitmap_phys_addr);
    if (bitmap == NULL) {
        return -5;
    }

    /*
     * Initialize entire bitmap to 1 (all pages marked reserved/allocated).
     */
    for (uint64_t i = 0; i < bitmap_size_bytes; i++) {
        bitmap[i] = 0xFF;
    }

    usable_page_count = 0;
    free_page_count = 0;

    /*
     * Mark only pages in LIMINE_MEMMAP_USABLE regions as free (0).
     */
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        /* Align start up to next page boundary */
        uint64_t start_phys = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        /* Align end down to page boundary */
        uint64_t end_phys = (entry->base + entry->length) & ~(PAGE_SIZE - 1);

        if (end_phys <= start_phys) {
            continue;
        }

        uint64_t start_page = start_phys / PAGE_SIZE;
        uint64_t end_page = end_phys / PAGE_SIZE;

        for (uint64_t p = start_page; p < end_page && p < total_page_count; p++) {
            bitmap_clear(p);
            usable_page_count++;
            free_page_count++;
        }
    }

    /*
     * Reserve the pages occupied by the bitmap itself!
     */
    uint64_t meta_start_page = bitmap_phys_addr / PAGE_SIZE;
    uint64_t meta_end_page = meta_start_page + metadata_page_count;
    for (uint64_t p = meta_start_page; p < meta_end_page && p < total_page_count; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_page_count--;
        }
    }

    last_alloc_index = 0;
    return 0;
}

uint64_t pfa_alloc_page(void) {
    if (bitmap == NULL || free_page_count == 0) {
        return 0;
    }

    /* Scan from last_alloc_index up to total_page_count */
    for (uint64_t i = last_alloc_index; i < total_page_count; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_page_count--;
            last_alloc_index = i + 1;
            return i * PAGE_SIZE;
        }
    }

    /* Wrap around to scan from 0 to last_alloc_index */
    for (uint64_t i = 0; i < last_alloc_index; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_page_count--;
            last_alloc_index = i + 1;
            return i * PAGE_SIZE;
        }
    }

    return 0;
}

int pfa_free_page(uint64_t phys_addr) {
    if (bitmap == NULL) {
        return -1;
    }

    /* Check page alignment */
    if ((phys_addr & (PAGE_SIZE - 1)) != 0) {
        return -2;
    }

    uint64_t page_idx = phys_addr / PAGE_SIZE;
    if (page_idx >= total_page_count) {
        return -3;
    }

    /* Verify if the page is currently marked allocated */
    if (!bitmap_test(page_idx)) {
        /* Already free (duplicate free) */
        return -4;
    }

    /* Check that this page falls within an original usable region and NOT metadata */
    struct limine_memmap_response *mmap = memmap_request.response;
    int is_usable = 0;
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];
        if (entry != NULL && entry->type == LIMINE_MEMMAP_USABLE) {
            if (phys_addr >= entry->base && (phys_addr + PAGE_SIZE) <= (entry->base + entry->length)) {
                is_usable = 1;
                break;
            }
        }
    }

    if (!is_usable) {
        /* Attempt to free reserved / non-usable hardware memory */
        return -5;
    }

    /* Protect bitmap metadata pages */
    uint64_t meta_phys_base = ((uint64_t)bitmap) - hhdm_request.response->offset;
    uint64_t meta_phys_end = meta_phys_base + (metadata_page_count * PAGE_SIZE);
    if (phys_addr >= meta_phys_base && phys_addr < meta_phys_end) {
        return -6;
    }

    bitmap_clear(page_idx);
    free_page_count++;

    if (page_idx < last_alloc_index) {
        last_alloc_index = page_idx;
    }

    return 0;
}

void pfa_get_stats(struct pfa_stats *stats) {
    if (stats == NULL) {
        return;
    }
    stats->total_pages = total_page_count;
    stats->usable_pages = usable_page_count;
    stats->free_pages = free_page_count;
    stats->allocated_pages = (usable_page_count > free_page_count) ? (usable_page_count - free_page_count) : 0;
    stats->metadata_pages = metadata_page_count;
}