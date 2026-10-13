#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

struct heap_stats {
    size_t total_heap_bytes;    /* Total virtual memory mapped for heap */
    size_t allocated_bytes;     /* Currently allocated payload bytes */
    size_t free_bytes;          /* Currently free bytes in heap blocks */
    size_t allocation_count;    /* Number of active allocations */
};

/*
 * Initialize the kernel heap.
 * Maps initial virtual heap pages starting at HEAP_START_VADDR.
 * Returns 0 on success, non-zero on failure.
 */
int heap_init(void);

/*
 * Allocate dynamic memory of requested size from the kernel heap.
 * The returned pointer is guaranteed to be 16-byte aligned.
 * Returns non-NULL pointer on success, NULL on failure or if size == 0.
 */
void *kmalloc(size_t size);

/*
 * Free dynamically allocated memory previously returned by kmalloc().
 * Safely handles NULL pointers, invalid pointers, and duplicate frees.
 */
void kfree(void *ptr);

/*
 * Query current heap usage statistics.
 */
void heap_get_stats(struct heap_stats *stats);

#endif /* HEAP_H */