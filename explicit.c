/* File: explicit.c
 * -------------------
 * An "explicit" allocator that allocates memory using headers,
 * which store the size and allocation status of a block, plus the
 * use of a free linked list connecting all free blocks in a LIFO
 * order. In this way, the allocator is able to recycle previously
 * allocated blocks and traverse free blocks with greater efficency
 * than searching the entire heap. Reallocting memory can happen in
 * place with the use of coalescing if needed, and freed blocks are
 * also coalesced with blocks to their right if free as well.
 */

#include <stdio.h>
#include <string.h>
#include "./allocator.h"
#include "./debug_break.h"

#define HEADER_SIZE 8
#define MALLOC 1
#define SIZE_BITMASK ~7


typedef struct header {
    size_t payload;
    struct header *prev;
    struct header *next;
} header;

static void *segment_start;
static void *segment_end;
static header *first_free;
static size_t segment_size;
static size_t nused;
static int num_freeblocks;
static int num_usedblocks;


/* Function: myinit
 * -------------------
 * This function initializes our global variables based on our
 * specified segment boundary parameters. Also, it initializes
 * the first header at the beginning of the heap which is our
 * initial first free block. Returns false when the heap_size
 * is smaller than the smallest allocation of a single dynamically
 * memory array.
 */
bool myinit(void *heap_start, size_t heap_size) {
    // return false when heap_size isn't large enough to service one request
    if (heap_size < (HEADER_SIZE + ALIGNMENT * 2)) {
        return false;
    }
    
    segment_start = heap_start;
    segment_end = (char *)heap_start + heap_size;
    segment_size = heap_size;
    nused = 0;
    num_usedblocks = 0;
    num_freeblocks = 1;

    // initialize first free block at beginning of heap
    first_free = segment_start;
    first_free->payload = heap_size - HEADER_SIZE;
    first_free->prev = NULL;
    first_free->next = NULL;
    
    return true;
}

/* Function: roundup
 * -------------------
 * This function rounds up the given number to the nearest given
 * multiple, which must be a power of 2, and returns the result.
 */
size_t roundup(size_t size, size_t multiple) {
    return (size + multiple - 1) & ~(multiple - 1);
}

/* Function: get_header
 * -------------------
 * This function returns a pointer to the header corresponding to
 * the pointer to the dynamicalling allocated array being passed in.
 */
header *get_header(void *ptr) {
    return (header *)((char *)ptr - HEADER_SIZE);
}

/* Function: get_size
 * -------------------
 * This function returns the size of a memory array from its header.
 */
size_t get_size(header *header_ptr) {
    return (header_ptr->payload) & SIZE_BITMASK;
}

/* Function: is_malloc
 * -------------------
 * This function returns true when a dynamically allocated arroy
 * is in use, and false when free.
 */
bool is_malloc(header *header_ptr) {
    return (header_ptr->payload) & MALLOC;
}

/* Function: add_freeblock
 * -------------------
 * This function adds a newly freed block to the beginning of the
 * free blocks double linked list, and sets it equal to the first_free
 * global variable.
 */
void add_freeblock(header *block) {
    // if there are no freeblocks, make the argument first_free
    if (first_free == NULL) {
        first_free = block;
    } else {
        first_free->prev = block;
        block->prev = NULL;
        block->next = first_free;
        first_free = block;
    }
}
   
/* Function: remove_freeblock
 * -------------------
 * This function removes a free block from the free block double
 * linked list.
 */
void remove_freeblock(header *block) {
    // update first_free if it's removed
    if (block == first_free) {
        first_free = block->next;
    }
    
    if (block->prev != NULL) {
        (block->prev)->next = block->next;
    }
    if (block->next != NULL) {
        (block->next)->prev = block->prev;
    }
}
        
/* Function: split_block
 * -------------------
 * This function splits a block of memory if there is enough space
 * to create a new array with the minimum size and header. It then
 * updates the size of the original block and sets the payload for
 * the new block with the correct allocation status. The new free 
 * block is then added to the free linked list. If there isn't
 * sufficient space, nothing is done.
 */
void split_block(header *old_block, size_t size_needed) {
    size_t old_size = get_size(old_block);
    if (old_size - size_needed >= (2 * ALIGNMENT + HEADER_SIZE)) {
        
        // find next block header and set payload size
        size_t total = size_needed + HEADER_SIZE;
        header *new_block = (header *)((char *)old_block + total);
        new_block->payload = old_size - total;

        // update the original header with correct allocation status
        if (is_malloc(old_block)) {
            old_block->payload = size_needed + MALLOC;
        } else {
            old_block->payload = size_needed;
        }

        // add new free block to free list
        add_freeblock(new_block);
        num_freeblocks++;
    }
}

/* Function: coalesce_block
 * -------------------
 * This function merges a block of memory with another block directly
 * to its right if that block is free. It then updates the block's
 * payload size and the free block double linked list. 
 */
void coalesce_block(header *block) {
    // return if no other free blocks exist
    if (block->prev == NULL && block->next == NULL) {
        return;
    }
    
    header *next = (header *)((char *)block + get_size(block) + HEADER_SIZE);

    // return if next block is at or past the end of the heap
    if ((void *)next >= segment_end) {
        return;
    }

    // coalesce if block to the right is free
    if (!is_malloc(next)) {
        remove_freeblock(next);
        block->payload += (get_size(next) + HEADER_SIZE);
        num_freeblocks--;
    }
}

/* Function: find_freeblock
 * -------------------
 * This function traverses the free block double linked list from
 * the start, given by the first_free global variable, and returns the
 * first block of free memory in the list that can accomodate a 
 * requested size.
 */
header *find_freeblock(header *ptr, size_t size_needed) {
    while (ptr != NULL) {
        size_t size = get_size(ptr);
        if (size >= size_needed) {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/* Function: mymalloc
 * -------------------
 * My custom version of malloc(), which searches for free blocks
 * with a free-block double linked list and updates the free linked
 * list.
 */
void *mymalloc(size_t requested_size) {
    // invalid request size
    if (requested_size > MAX_REQUEST_SIZE || requested_size == 0) {
        return NULL;
    }

    // payload size needs to be at least large enough for the double linked free list
    size_t size_needed = 2 * ALIGNMENT;
    if (requested_size > size_needed) {
        size_needed = roundup(requested_size, ALIGNMENT);
    }

    // heap doesn't have enough space to service request
    if (size_needed + nused > segment_size) {
        return NULL;
    }

    header *temp = first_free;
    header *ptr = find_freeblock(temp, size_needed);
    if (ptr == NULL) {
        return NULL;
    }
    
    split_block(ptr, size_needed);  
    remove_freeblock(ptr);
    ptr->payload += MALLOC;
    nused += (get_size(ptr) + HEADER_SIZE);
    num_usedblocks++;
    num_freeblocks--;
    void *payload_ptr = (void *)((char *)ptr + HEADER_SIZE);
    return payload_ptr;
}

/* Function: myfree
 * -------------------
 * My custom version of free(), which updates the free block linked
 * list and coalesces with the block to the right if possible.
 */
void myfree(void *ptr) {
    // invalid pointer
    if (ptr == NULL) {
        return;
    }
    
    header *header_ptr = get_header(ptr);
    header_ptr->payload -= MALLOC;
    nused -= (get_size(header_ptr) + HEADER_SIZE);
    num_usedblocks--;
    num_freeblocks++;
    add_freeblock(header_ptr);
    coalesce_block(header_ptr);
}

/* Function: myrealloc
 * -------------------
 * My custom version of realloc(), which supports inplace
 * reallocation with the use of coalescing free blocks to
 * the right if possible.
 */
void *myrealloc(void *old_ptr, size_t new_size) {
    // edge case: when pointer is NULL, it's a malloc request
    if (old_ptr == NULL) {
        return mymalloc(new_size);
    }
    // edge case: free pointer when new_size equals 0
    if (new_size == 0) {
        myfree(old_ptr);
        return NULL;
    }

    header *oldheader_ptr = get_header(old_ptr);
    size_t old_size = get_size(oldheader_ptr);
    size_t newsize_needed = 2 * ALIGNMENT;
    if (new_size > newsize_needed) {
        newsize_needed = roundup(new_size, ALIGNMENT);
    }

    // in the case of a shrink
    if (old_size > newsize_needed) {
        split_block(oldheader_ptr, newsize_needed);
        nused -= (old_size - get_size(oldheader_ptr));
        return old_ptr;
    }

    // old_ptr payload included padding
    if (old_size == newsize_needed) {
        return old_ptr;
    }
    
    // coalesce in place until you reach an allocated block
    header *temp = oldheader_ptr;
    size_t temp_size = old_size;
    while (true) {
        coalesce_block(temp);
        if (get_size(oldheader_ptr) != temp_size) {
            temp_size = get_size(oldheader_ptr); 
        } else {
            break;
        }
    }

    // coalescing has added more space than needed
    if (temp_size >= newsize_needed) {
        split_block(oldheader_ptr, newsize_needed);
        nused += (get_size(oldheader_ptr) - old_size);
        return old_ptr;
    }

    // not enough space for realloc request after coalescing
    void *new_ptr = mymalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, old_ptr, old_size);
    myfree(old_ptr);
    return new_ptr;
}

/* Function: validate_heap
 * -------------------
 * This function checks for potential errors/inconsistencies in the
 * heap data structures and returns false if there were issues, or
 * true otherwise.
 */
bool validate_heap() {
    // check validity of heap
    int free_blocks = 0;
    int used_blocks = 0;
    header *block = (header *)segment_start;
    while ((void *)block < segment_end) {
        // return false when too much memory has been allocated or if payload is too small
        if (nused > segment_size) {
            printf("Number of bytes used is greater than segment size");
            breakpoint();
            return false;
        }
        size_t size = get_size(block);
        if (size < 2 * ALIGNMENT) {
            printf("Block size is too small");
            breakpoint();
            return false;
        }
        
        if (is_malloc(block)) {
            used_blocks++;
        } else {
            free_blocks++;
        }
        size_t total = size + HEADER_SIZE;
        block = (header *)((char *)block + total);
    }

    // return false if inconsistencies with number of free or used blocks in heap
    if (used_blocks != num_usedblocks) {
        printf("Number of used blocks in heap is inconsistent with counter");
        return false;
    }
    if (free_blocks != num_freeblocks) {
        printf("Number of free blocks in heap is inconsistent with counter");
        return false;
    }

    // check validity of free blocks linked list
    free_blocks = 0;
    header *temp = first_free;
    while (temp != NULL) {
        if (is_malloc(temp)) {
            printf("Block in free linked list is actually in use");
            breakpoint();
            return false;
        }
        free_blocks++;
        temp = temp->next;
    }

    // return false if inconsistency with number of free blocks in linked list
    if (free_blocks != num_freeblocks) {
        printf("Number of free blocks in linked list is inconsistent with counter");
        return false;
    }
    
    return true;
}

/* Function: dump_heap
 * -------------------
 * This function prints out the the block contents of the heap.  It is not
 * called anywhere, but is a useful helper function to call from gdb when
 * tracing through programs.  It prints out the total range of the heap, and
 * information about each block within it.
 */
void dump_heap() {
    printf("Heap starts at address %p, ends at %p. %lu bytes currently used.\n",
           segment_start, segment_end, nused);
    header *block = (header *)segment_start;
    while ((void *)block < segment_end) {
        printf("Address %p: ", block);
        size_t size = get_size(block);
        bool malloc = is_malloc(block);
        if (malloc) {
            printf("%lu Used\n", size);
        } else {
            printf("%lu Free, prev at %p and next at %p\n",
                   size, block->prev, block->next);
        }
        size_t total = size + HEADER_SIZE;
        block = (header *)((char *)block + total);
    }
}
