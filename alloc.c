// Memory allocation
#include <stdio.h>
#include <stdlib.h>

#define PAGE_SIZE 4096
#define HEADER_SIZE sizeof(struct mem_blk_header)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define ALIGN_DOWN(size) ((size / ALIGNMENT) * ALIGNMENT)
#define MIN_BLK_SIZE ALIGN(HEADER_SIZE + sizeof(struct free_blk_header))

// Malloc:
// Find space in free list
// Extend free list if necessary 
// Allocate and adjust free list

struct mem_blk_header
{
    size_t size; 
};

struct free_blk_header
{
    size_t size;
    struct free_blk_header* prev;
    struct free_blk_header* next;
};

struct free_blk_header* first_free = NULL;
void* heap_end = NULL;
void* heap_start = NULL;

// Return a pointer to a new empty 4k page
void* fetch_page(int pages) {
    void* ptr = malloc(PAGE_SIZE * pages);
    return ptr;
}

size_t ptr_distance(char* p1, char* p2) {
    return p2 - p1;
}

// If first_free = NULL, i.e. free list empty, last_free is assumed to be NULL and not touched anyway
struct free_blk_header* extend_heap(size_t blk_size, struct free_blk_header* last_free) {
    int num_pages = (blk_size + (PAGE_SIZE - 1)) / PAGE_SIZE;
    
    struct free_blk_header* new_free_node = fetch_page(num_pages);

    // If heap does not exist, assign heap start pointer
    if(heap_end == NULL) {
        heap_start = new_free_node;
    }

    heap_end = (char*)new_free_node + PAGE_SIZE * num_pages;
    new_free_node->size = PAGE_SIZE * num_pages;
    ((struct mem_blk_header *)(heap_end - HEADER_SIZE))->size = PAGE_SIZE * num_pages;

    if(!first_free) {
        // Our free list is empty, this is our first free node
        new_free_node->prev = NULL;
        new_free_node->next = NULL;
        first_free = new_free_node;
    } else {
        last_free->next = new_free_node;
        new_free_node->prev = last_free;
        new_free_node->next = NULL;
    }

    return new_free_node;
}

struct free_blk_header* alloc_block(struct free_blk_header* free_blk, size_t blk_size) {
    void* end_of_free_blk = (char*)free_blk + free_blk->size;
    size_t extra_size = ptr_distance((char*)free_blk + blk_size, end_of_free_blk); // Should already be aligned

    if (extra_size > MIN_BLK_SIZE) {
        // Split free block
        struct free_blk_header *new_free_blk = (char*)free_blk + blk_size;
        new_free_blk->size = extra_size;
        new_free_blk->next = free_blk->next;
        free_blk->next = new_free_blk;
        ((struct mem_blk_header *)((char*)new_free_blk + extra_size - HEADER_SIZE))->size = extra_size;
    } else {
        // Add padding to block size
        blk_size += extra_size;
    }

    free_blk->size = blk_size | 1; // Mark allocated
    ((struct mem_blk_header *)((char*)free_blk + blk_size - HEADER_SIZE))->size = blk_size | 1; // Mark allocated

    // If not the first element in free list
    if (free_blk->prev) {
        (free_blk->prev)->next = free_blk->next;
    }
    // If not the last element in free list
    if (free_blk->next) {
        (free_blk->next)->prev = free_blk->prev;
    }

    // If this block was the first in free list
    if(first_free == free_blk) {
        first_free = free_blk->next;
    }

    return free_blk;
}

void coalesce(struct free_blk_header* free_blk) {
    struct free_blk_header *left_blk = NULL;  // block to the right
    struct free_blk_header *right_blk = NULL; // block to the left

    // If a block exists to the left, check if it's free
    if (free_blk != heap_start)
    {
        struct mem_blk_header *left_tail = (char *)free_blk - HEADER_SIZE;
        if ((left_tail->size & 1) == 0)
        {
            left_blk = (char *)free_blk - left_tail->size;
        }
    }
    // If block exists to the right, check if it's free
    if (((char *)free_blk + free_blk->size) != heap_end)
    {
        struct mem_blk_header *right_head = (char *)free_blk + free_blk->size;
        if ((right_head->size & 1) == 0)
        {
            right_blk = right_head; // Right is unallocated so we can include in coalesce
        }
    }

    size_t new_blk_size = free_blk->size;

    // If block to the left is free
    // Change blk_to_free pointer to point to left block so we change header of block on left if exists
    // Change guard at end of right header
    // Change size first so we can jump to end of new coalesced block
    if (left_blk)
    {
        left_blk->next = free_blk->next;
        free_blk = left_blk;
        new_blk_size += left_blk->size;
    }
    // If block to the right is free
    if (right_blk)
    {
        free_blk->next = right_blk->next;
        if (right_blk->next)
        {
            right_blk->next->prev = free_blk;
        }
        new_blk_size += right_blk->size;
    }

    free_blk->size = new_blk_size;
    ((struct mem_blk_header *)((char *)free_blk + new_blk_size - HEADER_SIZE))->size = new_blk_size;
}

// Allocate 
void* smalloc(size_t size) {
    void* return_mem = NULL;
    size_t blk_size = ALIGN(size + 2 * sizeof(struct mem_blk_header));

    struct free_blk_header* next_free = first_free;
    struct free_blk_header* last_free = first_free;

    // TRAVERSE FREE LIST TO ALLOCATE FROM
    while (next_free) {
        if (next_free->size > blk_size) {
            return_mem = alloc_block(next_free, blk_size);
            break;
        }

        last_free = next_free;
        next_free = next_free->next;
    }

    // Either free list is empty or no big enough free blocks, in both cases
    // we extend heap and allocate from the new free block created
    if(!return_mem) {
        return_mem = alloc_block(extend_heap(blk_size, last_free), blk_size);
    }

    // Return pointer to the start of payload
    return ((void*)(((char *)return_mem) + HEADER_SIZE));
}

void sfree(void* payload_start) {
    struct free_blk_header* blk_to_free = ((char*)payload_start) - HEADER_SIZE; // Find start of block
    // Mark unallocated
    blk_to_free->size = blk_to_free->size & ~1L; // write macro to make clearer
    ((struct mem_blk_header *)((char *)blk_to_free + blk_to_free->size - HEADER_SIZE))->size = blk_to_free->size & ~1L; // another macro
    blk_to_free->prev = NULL;
    blk_to_free->next = NULL;

    // For iterating over free list
    struct free_blk_header* list_iter = first_free;
    struct free_blk_header* last_free = NULL;

    // Search free list for place our block should go
    while(list_iter != NULL) {
        // If we have found the free block which should come after blk_to_free
        if(list_iter > blk_to_free) {
            // If blk_to_free isn't being placed at start of free list
            if(last_free != NULL) {
                last_free->next = blk_to_free;
            } else {
                first_free = blk_to_free; // Start of free list
            }
            blk_to_free->next = list_iter;
            blk_to_free->prev = list_iter->prev;
            list_iter->prev = blk_to_free;
            break;
        }

        last_free = list_iter;
        list_iter = list_iter->next;
    }

    // If this is the case, previous while loop won't have run
    if (first_free == NULL) {
        first_free = blk_to_free; // prev and next are already set to NULL
    }
    else if(list_iter == NULL) {
        // In this case we reached end of free list without finding a block that should be ahead of blk_to_free
        // This means blk_to_free goes at the end of the list, i.e. after last_free
        last_free->next = blk_to_free;
    }

    coalesce(blk_to_free);
}

int main() {
    // Verify nothing is overwritten by writing increasing integers & checking all there
    printf("Hello World!");
    void* seg1 = smalloc(2048);
    void* seg2 = smalloc(73);
    void* seg3 = smalloc(173);
    void* seg4 = smalloc(2000);
    sfree(seg1);
    size_t foo = first_free->size;
    void* seg5 = smalloc(2000);
    sfree(seg3);
    sfree(seg2);
    sfree(seg4);
    sfree(seg5);
}