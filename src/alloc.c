#include "alloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ALIGNMENT 16 /**< The alignment of the memory blocks */

static free_block *HEAD = NULL; /**< Pointer to the first element of the free list */

/**
 * Split a free block into two blocks
 *
 * @param block The block to split
 * @param size The size of the first new split block
 * @return A pointer to the first block or NULL if the block cannot be split
 */
void *split(free_block *block, int size) {
    if((block->size < size + sizeof(free_block))) {
        return NULL;
    }

    void *split_pnt = (char *)block + size + sizeof(free_block);
    free_block *new_block = (free_block *) split_pnt;

    new_block->size = block->size - size - sizeof(free_block);
    new_block->next = block->next;

    block->size = size;

    return block;
}


/* for the sake of tracking the last allocated free block for next fit */
static free_block *last_alloc = NULL; 


/**
 * Find the previous neighbor of a block
 *
 * @param block The block to find the previous neighbor of
 * @return A pointer to the previous neighbor or NULL if there is none
 */
free_block *find_prev(free_block *block) {
    free_block *curr = HEAD;
    while(curr != NULL) {
        char *next = (char *)curr + curr->size + sizeof(free_block);
        if(next == (char *)block)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Find the next neighbor of a block
 *
 * @param block The block to find the next neighbor of
 * @return A pointer to the next neighbor or NULL if there is none
 */
free_block *find_next(free_block *block) {
    char *block_end = (char*)block + block->size + sizeof(free_block);
    free_block *curr = HEAD;

    while(curr != NULL) {
        if((char *)curr == block_end)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Remove a block from the free list
 *
 * @param block The block to remove
 */
void remove_free_block(free_block *block) {
    free_block *curr = HEAD;
    if(curr == block) {
        HEAD = block->next;
        return;
    }
    while(curr != NULL) {
        if(curr->next == block) {
            curr->next = block->next;
            return;
        }
        curr = curr->next;
    }
}

/**
 * Coalesce neighboring free blocks
 *
 * @param block The block to coalesce
 * @return A pointer to the first block of the coalesced blocks
 */
void *coalesce(free_block *block) {
    if (block == NULL) {
        return NULL;
    }

    free_block *prev = find_prev(block);
    free_block *next = find_next(block);

    // Coalesce with previous block if it is contiguous.
    if (prev != NULL) {
        char *end_of_prev = (char *)prev + prev->size + sizeof(free_block);
        if (end_of_prev == (char *)block) {
            prev->size += block->size + sizeof(free_block);

            // Ensure prev->next is updated to skip over 'block', only if 'block' is directly next to 'prev'.
            if (prev->next == block) {
                prev->next = block->next;
            }
            block = prev; // Update block to point to the new coalesced block.
        }
    }

    // Coalesce with next block if it is contiguous.
    if (next != NULL) {
        char *end_of_block = (char *)block + block->size + sizeof(free_block);
        if (end_of_block == (char *)next) {
            block->size += next->size + sizeof(free_block);

            // Ensure block->next is updated to skip over 'next'.
            block->next = next->next;
        }
    }

    return block;
}

/**
 * Call sbrk to get memory from the OS
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the allocated memory
 */
void *do_alloc(size_t size) {
    
    void *p = sbrk(0); 
    unsigned long addr = (unsigned long)p;  // pointer to integer
    
    size_t curr_alignment = addr & (ALIGNMENT - 1);
    if (curr_alignment != 0) {
        size_t adjustment = ALIGNMENT - curr_alignment;
        if (sbrk(adjustment) == (void *)-1) return NULL;  // aligns memory
    }

    // get memory from OS
    void *new_block = sbrk(size + sizeof(header));
    if (new_block == (void *)-1) return NULL;  // sbrk failed

    header *block = (header *)new_block;
    return (void *)((char *)block + sizeof(header));
}

/**
 * Allocates memory for the end user
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the requested block of memory
 */
void *tumalloc(size_t size) {

    free_block *block = HEAD;
    header block_header;

    if (!HEAD) {
        void *ptr = do_alloc(size); // sort of following pseudocode, is this right?
        return ptr;
    }

    else {
        while (block != NULL) {
            if (size <= block->size) { // should it be block.size or block->size?
                void *allocated = split(block, size+sizeof(block_header));
                if (allocated) {
                    remove_free_block(block);
                    return allocated;
                }

                block_header.size = size;
                block_header.magic = 0x01234567;
                block = block->next;
                return block + sizeof(header); // i don't think it's supposed to be block here
            }
            else{
                void *ptr = do_alloc(size);
                return ptr;
            }
        }
    }
    }


/**
 * Allocates and initializes a list of elements for the end user
 *
 * @param num How many elements to allocate
 * @param size The size of each element
 * @return A pointer to the requested block of initialized memory
 */
void *tucalloc(size_t num, size_t size) {

    size_t total_size = num * size;
    
    void *ptr = tumalloc(total_size); //using tumalloc to allocate mem
    
    if (!ptr) { 
        return NULL;
    }

    memset(ptr, 0, total_size); // set the memory to 0
    
    return ptr;
}

/**
 * Reallocates a chunk of memory with a bigger size
 *
 * @param ptr A pointer to an already allocated piece of memory
 * @param new_size The new requested size to allocate
 * @return A new pointer containing the contents of ptr, but with the new_size
 */
void *turealloc(void *ptr, size_t new_size) {

    if (!ptr) {
        return tumalloc(new_size);
    }

    // free mem if size is 0
    if (new_size == 0) {
        tufree(ptr);
        return NULL;
    }

    // gets the header of the existing block
    header *old_header = (header *)((char *)ptr - sizeof(header));
    size_t old_size = old_header->size;

    if (old_size >= new_size) {
        return ptr;
    }

    // allocate new block
    void *new_ptr = tumalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    tufree(ptr);

    return new_ptr;
}

/**
 * Removes used chunk of memory and returns it to the free list
 *
 * @param ptr Pointer to the allocated piece of memory
 */
void tufree(void *ptr) {

    if (!ptr) return;

    header *block_header = (header *)ptr - sizeof(header);

    if (block_header->magic != 0x01234567) { // a bit diff from pseudocode, but still same test case.
        printf("MEMORY CORRUPTION DETECTED at %p!\n", ptr);
        abort(); 
    }

    else {
        // TODO: stuff here
        free_block *block = (free_block *)block_header;
        block->size = block_header->size; // it doesn't like block here bc it's not a struct for some reason
        block->next = HEAD;
        HEAD = block;
        coalesce(block);
    }

}

