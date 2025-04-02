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
    free_block *block;

    if (size == 0) {                    //check for invalid size allotment
        return NULL;
    }

    size_t total_size = size + sizeof(free_block);  //calculate size of free_block struct + size to allocate
    block = sbrk(total_size);           //increment data space to total size

    if(block == ((void *) -1)) {        // standard error output for sbrk()
        return NULL;                    //return NULL if block assignment equivalent to sbrk() error
    }

    block->size = size;                 //set block size to size
    block->next = NULL;

    return (void *)(block + 1);
}

/**
 * Allocates memory for the end user
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the requested block of memory
 */
void *tumalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    free_block *curr_block = HEAD;          //initialize the current block as HEAD node

    while(curr_block) {
        if(curr_block->size >= size) {      
            remove_free_block(curr_block);      //if the allotment is less than the size of the current block, remove the block from the free list
            if (curr_block->size >= size + sizeof(free_block)) {    //check if the block has more room than the allotment
                split(curr_block, size);

                free_block *leftovers = (free_block *)((char *)curr_block + size + sizeof(free_block));     //leftover memory from the split

                if(HEAD == NULL || HEAD->next == NULL) {
                    leftovers->next = NULL;
                    HEAD = leftovers;
                }
                else {
                    leftovers->next = HEAD;
                    HEAD = leftovers;
                }
            }
            
            return (void *)(curr_block + 1);
        }

        curr_block = curr_block->next;
    }

    return do_alloc(size);
}

/**
 * Allocates and initializes a list of elements for the end user
 *
 * @param num How many elements to allocate
 * @param size The size of each element
 * @return A pointer to the requested block of initialized memory
 */
void *tucalloc(size_t num, size_t size) {
    free_block *block;
    if(!num || !size){
        return NULL;
    }

    if(num > 0 && size > 0 && (num > ((size_t)-1) / size)) { //overflow check, MAX_SIZE would not work even with the necessary header, something about the Intellisense Include Path 
        return NULL;
    }

    block = tumalloc(num * size);
    if(!block) {
        return NULL;
    }
    memset(block, 0, size);
    return block;
}

/**
 * Reallocates a chunk of memory with a bigger size
 *
 * @param ptr A pointer to an already allocated piece of memory
 * @param new_size The new requested size to allocate
 * @return A new pointer containing the contents of ptr, but with the new_size
 */
void *turealloc(void *ptr, size_t new_size) {
    free_block *header = (free_block *)((char *)ptr - sizeof(free_block)); //find the header of ptr and assign it to header
    void *redo;         //assign this later, for now just initialize 

    if(!ptr || !new_size) {         //error checking, makes sure there is a valid input for the function
        return tumalloc(new_size);
    }

    if(header->size >= new_size){       //check if the header size is larger than the new reallocation size
        return ptr;
    }

    redo = tumalloc(new_size);          //allocate new_size amount of memory

    if(!redo) {                         //if the allocation fails, return no value
        return NULL;
    }

    else {
        memcpy(redo, ptr, new_size < header->size ? new_size : header->size);       //copies memory to redo, from pointer, with the bytes specified to copy to the new reallocated piece of memory
        tufree(ptr);                    //free the previous piece of allocated memory
    }

    return redo;
}

/**
 * Removes used chunk of memory and returns it to the free list
 *
 * @param ptr Pointer to the allocated piece of memory
 */
void tufree(void *ptr) {
    void *programbreak;             //end of the heap
    free_block *tmp = (free_block *)((char *)ptr - sizeof(free_block));     //temporary pointer

    if(!ptr) {              //if no valid input, break from the function
        return;
    }

    programbreak = sbrk(0);     //assign to end of heap
    if ((char *)tmp+ tmp->size + sizeof(free_block) == programbreak) {      //check that the memory we're deallocating is at the end of the heap
        if(HEAD == tmp) {       //check that tmp is not already the HEAD
            HEAD = NULL;
        }
        else {
           free_block *prev_pointer = find_prev(tmp);       //find the previous node in the free list
           if(prev_pointer) {                               //if there is one,
                prev_pointer->next = NULL;                  //break the reference of the previous node to tmp
           }
        }
        sbrk(-(tmp->size + sizeof(free_block)));            //deallocate memory based on the total size of tmp
    }
    else {
        if(HEAD == NULL || HEAD->next == NULL) {            
            tmp->next = NULL;
            HEAD = tmp;
        }
        else {
            tmp->next = HEAD;
            HEAD = coalesce(tmp);
        }
    }
}
