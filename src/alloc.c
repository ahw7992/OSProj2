#include "alloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ALIGNMENT 16 /**< The alignment of the memory blocks */

static free_block *HEAD = NULL; /**< Pointer to the first element of the free list */
static free_block *last_allocated = NULL;

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
    intptr_t addr = (intptr_t)p & (ALIGNMENT - 1);
    intptr_t adjustment;

    if (addr != 0) {
        adjustment = ALIGNMENT - addr;
    }
    else {
        adjustment = 0;
    }
    void * block = sbrk(size + sizeof(header) + adjustment);
    if (block == (void *)-1) return NULL;  // aligns memory
    void *headstart = (void *)((intptr_t)block + adjustment); 
    header *hdr_start = (header *)headstart;

    hdr_start->magic = 0x01234567;
    hdr_start->size = size;

    return (headstart + sizeof(header));
}


void *tunextfit(size_t true_size) {
    //printf("Starting tunextfit search\n"); //debug
    if (HEAD == NULL) {
        return NULL;
    }
    free_block *block = (last_allocated) ? last_allocated : HEAD; // current blk
    if (block == NULL) {
        block = HEAD; // wrap around to the beginning
    }
    free_block *start = block;

    do {
        printf("Checking if block at %p (size %zu) against required %zu\n", block, block->size, true_size);
                
       // check: can it be split and leave enough room for another free block?
        if (block->size >= true_size) { 
            printf("Found suitable block\n");
            remove_free_block(block); 

            void *split_blk = split(block, true_size);
            if (split_blk) {
                printf("Splitting block. Remaining size: %zu\n", ((free_block*)split_blk)->size);
                free_block *unused = (free_block*)split_blk;    
                unused->next = HEAD;
                HEAD = unused;
            }
            last_allocated = block->next ? block->next : HEAD;

            header *hdr = (header *)block;
            hdr->size = true_size - sizeof(header);
            hdr->magic = 0x01234567;
            printf("Allocating block at %p with magic 0x%x\n", hdr, hdr->magic);
            
            return (void *)((char *)block + sizeof(header)); 
        }
        
        block = block->next;
        if (block == NULL) {
            block = HEAD; 
        }
    } while (block != start);

 return NULL;
}


/**
 * Allocates memory for the end user
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the requested block of memory
 */
void *tumalloc(size_t size) {
    //printf("Requesting %zu bytes\n", size); // debug
    void *ptr; 

    if (HEAD == NULL) {
        ptr = do_alloc(size); // confirmed good
        return ptr;
    }

    size_t true_size = size + sizeof(header);
    ptr = tunextfit(true_size); // going through the free list w/ next-fit
    if (ptr != NULL) {
        //printf("Returning nextfit ptr\n");
        return ptr;
    }
    else {
        //printf("ptr was null, setting ptr to do_alloc(size) and returning\n");
        ptr = do_alloc(size);
        return ptr;
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
    
    if (ptr != NULL) { 
        memset(ptr, 0, total_size);
        return ptr;
    }
    else {
        return NULL;
    }
}

/**
 * Reallocates a chunk of memory with a bigger size
 *
 * @param ptr A pointer to an already allocated piece of memory
 * @param new_size The new requested size to allocate
 * @return A new pointer containing the contents of ptr, but with the new_size
 */
void *turealloc(void *ptr, size_t new_size) {
    if (ptr == NULL) {
        return tumalloc(new_size);
    }

    void *new_block = tumalloc(new_size);
    if (new_block == NULL) {
        return NULL; 
    }
   
    header *hdr = (header *)((char *)ptr - sizeof(header));
    if (hdr->magic != 0x01234567) {
        printf("MEM CORRUPTION DETECTED IN TUREALLOC");
        abort();
    }
    else {
        // copy the old data & use the smaller of old size or new_size
        size_t copy_size = (hdr->size < new_size) ? hdr->size : new_size;
        memcpy(new_block, ptr, copy_size);

        tufree(ptr);
        
        return new_block;
    }
}

/**
 * Removes used chunk of memory and returns it to the free list
 *
 * @param ptr Pointer to the allocated piece of memory
 */
void tufree(void *ptr) {
    if (!ptr) return;

    header *hdr = (header*)(ptr - sizeof(header));
    //printf("Freeing memory at %p\n", ptr); //debug
    //printf("Block size: %zu, Magic: 0x%08x\n", hdr->size + sizeof(header), hdr->magic);

    if (hdr->magic != 0x01234567) { // a bit diff from pseudocode, but still same test case.
        printf("MEMORY CORRUPTION DETECTED\n");
        fflush(stdout);
        abort(); 
    }
    else {
        free_block *block = (free_block *)hdr;
        if (block->next == NULL && HEAD == block) {
            // if the block is already in the free list
            printf("Double free detected\n");
            abort();
        }
        
        block->size = hdr->size;
        block->next = HEAD;
        HEAD = block;
        coalesce(block);
        //printf("Free list head: %p\n", HEAD);
    }

}

