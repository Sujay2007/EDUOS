#include "heap.h"
#include "vmm.h"
#include "pfa.h"

/*
 * Kernel Heap Virtual Memory Window:
 * Placed high in canonical negative space, safely before the kernel text/data
 * (which starts at 0xffffffff80000000).
 */
#define HEAP_START_VADDR 0xffffffff70000000ULL
#define HEAP_INITIAL_PAGES 4ULL
#define HEAP_MAX_VADDR   0xffffffff78000000ULL

#define BLOCK_MAGIC_FREE  0x46524545ULL /* 'FREE' */
#define BLOCK_MAGIC_ALLOC 0x414c4c4fULL /* 'ALLO' */

/* 16-byte alignment requirement */
#define HEAP_ALIGNMENT 16ULL
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

struct heap_block {
    uint64_t magic;           /* BLOCK_MAGIC_FREE or BLOCK_MAGIC_ALLOC */
    size_t size;              /* Payload size in bytes (excluding this header) */
    struct heap_block *next;  /* Next block in the intrusive linked list */
    struct heap_block *prev;  /* Previous block in the intrusive linked list */
};

#define BLOCK_HEADER_SIZE ALIGN_UP(sizeof(struct heap_block), HEAP_ALIGNMENT)

static struct heap_block *heap_head = NULL;
static uint64_t heap_curr_break = HEAP_START_VADDR;

static int heap_expand(size_t pages) {
    for (size_t i = 0; i < pages; i++) {
        if (heap_curr_break >= HEAP_MAX_VADDR) {
            return -1; /* Out of virtual address space for heap */
        }

        uint64_t frame_phys = pfa_alloc_page();
        if (frame_phys == 0) {
            return -2; /* Physical memory exhausted */
        }

        int map_err = vmm_map_page(heap_curr_break, frame_phys, PTE_PRESENT | PTE_WRITABLE);
        if (map_err != 0) {
            pfa_free_page(frame_phys);
            return -3;
        }

        /* Zero out newly mapped page */
        uint64_t *page_words = (uint64_t *)heap_curr_break;
        for (size_t w = 0; w < (PAGE_SIZE / sizeof(uint64_t)); w++) {
            page_words[w] = 0;
        }

        heap_curr_break += PAGE_SIZE;
    }

    return 0;
}

int heap_init(void) {
    heap_curr_break = HEAP_START_VADDR;

    int exp_res = heap_expand(HEAP_INITIAL_PAGES);
    if (exp_res != 0) {
        return -1;
    }

    /* Initialize initial single large free block covering the mapped pages */
    heap_head = (struct heap_block *)HEAP_START_VADDR;
    heap_head->magic = BLOCK_MAGIC_FREE;
    heap_head->size = (HEAP_INITIAL_PAGES * PAGE_SIZE) - BLOCK_HEADER_SIZE;
    heap_head->next = NULL;
    heap_head->prev = NULL;

    return 0;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t aligned_size = ALIGN_UP(size, HEAP_ALIGNMENT);

    struct heap_block *curr = heap_head;
    while (curr != NULL) {
        if (curr->magic == BLOCK_MAGIC_FREE && curr->size >= aligned_size) {
            /* Check if we can split this block */
            if (curr->size >= aligned_size + BLOCK_HEADER_SIZE + HEAP_ALIGNMENT) {
                uint8_t *curr_raw = (uint8_t *)curr;
                struct heap_block *next_block = (struct heap_block *)(curr_raw + BLOCK_HEADER_SIZE + aligned_size);

                next_block->magic = BLOCK_MAGIC_FREE;
                next_block->size = curr->size - aligned_size - BLOCK_HEADER_SIZE;
                next_block->next = curr->next;
                next_block->prev = curr;

                if (curr->next != NULL) {
                    curr->next->prev = next_block;
                }
                curr->next = next_block;
                curr->size = aligned_size;
            }

            curr->magic = BLOCK_MAGIC_ALLOC;
            uint8_t *payload = ((uint8_t *)curr) + BLOCK_HEADER_SIZE;
            return (void *)payload;
        }
        curr = curr->next;
    }

    /* No free block large enough; expand heap */
    size_t needed_bytes = aligned_size + BLOCK_HEADER_SIZE;
    size_t needed_pages = (needed_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (needed_pages < 4) {
        needed_pages = 4; /* Expand by at least 4 pages (16 KiB) */
    }

    uint64_t old_break = heap_curr_break;
    if (heap_expand(needed_pages) != 0) {
        return NULL;
    }

    /* Append new free block covering the new pages */
    struct heap_block *new_free = (struct heap_block *)old_break;
    new_free->magic = BLOCK_MAGIC_FREE;
    new_free->size = (needed_pages * PAGE_SIZE) - BLOCK_HEADER_SIZE;
    new_free->next = NULL;

    /* Find tail of block list */
    struct heap_block *tail = heap_head;
    if (tail == NULL) {
        heap_head = new_free;
        new_free->prev = NULL;
    } else {
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = new_free;
        new_free->prev = tail;

        /* If previous tail was also free, coalesce with new free space */
        if (tail->magic == BLOCK_MAGIC_FREE) {
            tail->size += BLOCK_HEADER_SIZE + new_free->size;
            tail->next = NULL;
            new_free = tail;
        }
    }

    /* Now allocate from new_free */
    if (new_free->size >= aligned_size + BLOCK_HEADER_SIZE + HEAP_ALIGNMENT) {
        uint8_t *curr_raw = (uint8_t *)new_free;
        struct heap_block *next_block = (struct heap_block *)(curr_raw + BLOCK_HEADER_SIZE + aligned_size);

        next_block->magic = BLOCK_MAGIC_FREE;
        next_block->size = new_free->size - aligned_size - BLOCK_HEADER_SIZE;
        next_block->next = new_free->next;
        next_block->prev = new_free;

        if (new_free->next != NULL) {
            new_free->next->prev = next_block;
        }
        new_free->next = next_block;
        new_free->size = aligned_size;
    }

    new_free->magic = BLOCK_MAGIC_ALLOC;
    return (void *)(((uint8_t *)new_free) + BLOCK_HEADER_SIZE);
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    uint64_t ptr_val = (uint64_t)ptr;
    if (ptr_val < HEAP_START_VADDR + BLOCK_HEADER_SIZE || ptr_val >= heap_curr_break) {
        return; /* Outside heap boundary */
    }

    struct heap_block *block = (struct heap_block *)(ptr_val - BLOCK_HEADER_SIZE);

    /* Validate header magic: guard against double free or corrupted block */
    if (block->magic != BLOCK_MAGIC_ALLOC) {
        return;
    }

    block->magic = BLOCK_MAGIC_FREE;

    /* Coalesce with next block if free */
    if (block->next != NULL && block->next->magic == BLOCK_MAGIC_FREE) {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }

    /* Coalesce with prev block if free */
    if (block->prev != NULL && block->prev->magic == BLOCK_MAGIC_FREE) {
        block->prev->size += BLOCK_HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next != NULL) {
            block->next->prev = block->prev;
        }
    }
}

void heap_get_stats(struct heap_stats *stats) {
    if (stats == NULL) {
        return;
    }

    stats->total_heap_bytes = (size_t)(heap_curr_break - HEAP_START_VADDR);
    stats->allocated_bytes = 0;
    stats->free_bytes = 0;
    stats->allocation_count = 0;

    struct heap_block *curr = heap_head;
    while (curr != NULL) {
        if (curr->magic == BLOCK_MAGIC_ALLOC) {
            stats->allocated_bytes += curr->size;
            stats->allocation_count++;
        } else if (curr->magic == BLOCK_MAGIC_FREE) {
            stats->free_bytes += curr->size;
        }
        curr = curr->next;
    }
}