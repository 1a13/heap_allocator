/* File: implicit.c
 * -------------------
 * An "implicit" allocator that allocates memory by using headers
 * which indicate a block of memory's allocation status and size.
 * In this way, the allocator is able to recycle previously allocated
 * memory blocks. Reallocating memory does not happen in place and repeated
 * realloc calls to the same memory block lead to poor utilization. Searching
 * free blocks is also tedious as every block is searched incrementally.
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
} header;

static void *segment_start;
static void *segment_end;
static size_t segment_size;
static size_t nused;
static int num_freeblocks;
static int num_usedblocks;


/* Function: myinit
 * -------------------
 * This function initializes our global variables based on our
 * specified segment boundary parameters. Also, it initializes
 * the first header at the beginning of the heap. Returns false
 * when the the heap_size is smaller than the smallest allocation
 * of a single dynamically allocated memory array.
 */
bool myinit(void *heap_start, size_t heap_size) {
    // return false when heap isn't large enough to service one request
    if (heap_size < HEADER_SIZE + ALIGNMENT) {
        return false;
    }
    
    segment_start = heap_start;
    segment_end = (char *)heap_start + heap_size;
    segment_size = heap_size;
    nused = 0;
    num_usedblocks = 0;
    num_freeblocks = 1;

    // initalize first free block at beginning of heap
    header *start = heap_start;
    start->payload = heap_size - HEADER_SIZE;
    
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
 * the pointer to dynamically allocated array being passed in.
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
 * This function returns true when a dynamically allocated array
 * is in use, and false when free.
 */
bool is_malloc(header *header_ptr) {
    return (header_ptr->payload) & MALLOC;
}

/* Function: split_block
 * -------------------
 * This function splits a block of memory if there is enough space
 * to create a new array with the minimum size and header. It then
 * updates the size of the original block and sets the paylaod for
 * the new block. If there isn't sufficent space, nothing is done.
 */
void split_block(header *old_block, size_t size_needed) {   
    size_t old_size = get_size(old_block);
    if (old_size - size_needed >= HEADER_SIZE + ALIGNMENT) {
        
        // find next block header and set payload size
        size_t total = size_needed + HEADER_SIZE;
        header *new_block = (header *)((char *)old_block + total);
        new_block->payload = old_size - total;

        // update original header
        old_block->payload = size_needed;
        num_freeblocks++;
    }
}

/* Function: find_first_free
 * -------------------
 * This function finds a free block of memory big enough to 
 * service the payload request using a first fit approach, starting
 * at the beginning of the heap, and returns a pointer to the header.
 */
header *find_first_free(header *ptr, size_t size_needed) {
    while ((void *)ptr < segment_end) {
        size_t size = get_size(ptr);
        if (size >= size_needed && !is_malloc(ptr)) {
            return ptr;
        }
        ptr = (header *)((char *)ptr + size + HEADER_SIZE);
    }
    return NULL;
}

/* Function: mymalloc
 * -------------------
 * My custom version of malloc(), which searches for free blocks
 * by traversing from the beginning of the heap.
 */
void *mymalloc(size_t requested_size) {
    // invalid request size
    if (requested_size > MAX_REQUEST_SIZE || requested_size == 0) {
        return NULL;
    }

    // heap doesn't have enough space to service request
    size_t size_needed = roundup(requested_size, ALIGNMENT);
    if (size_needed + nused > segment_size) {
        return NULL;
    }

    // check if heap has a block that can fit request
    header *temp = (header *)segment_start;
    header *ptr = find_first_free(temp, size_needed);
    if (ptr == NULL) {
        return NULL;
    }

    split_block(ptr, size_needed);
    ptr->payload += MALLOC;
    nused += (get_size(ptr) + HEADER_SIZE);
    num_usedblocks++;
    num_freeblocks--;
    void *payload_ptr = (void *)((char *)ptr + HEADER_SIZE);
    return payload_ptr;
}

/* Function: myfree
 * -------------------
 * My custom version of free().
 */
void myfree(void *ptr) {
    // invalid pointer
    if (ptr == NULL) {
        return;
    }
    
    header *header_ptr = get_header(ptr);
    header_ptr->payload -= MALLOC;
    num_usedblocks--;
    num_freeblocks++;
    nused -= (get_size(header_ptr) + HEADER_SIZE);
}

/* Function: myrealloc
 * -------------------
 * My custom version of realloc(). Does not support in place
 * reallocation even if possible.
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
  
    void *new_ptr = mymalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    size_t old_size = get_size(get_header(old_ptr));

    // in the case of a shrink
    if (new_size < old_size) {
        old_size = new_size;
    }

    // implicit heap allocator doesn't support in-place realloc
    memcpy(new_ptr, old_ptr, old_size);
    myfree(old_ptr);
    return new_ptr;
}

/* Function: validate_heap
 * -------------------
 * This function checks for potential errors/inconsistencies in the heap data
 * structures and returns false if there were issues, or true otherwise. 
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
        if (size < ALIGNMENT) {
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
            printf("%lu Free\n", size);
        }
        size_t total = size + HEADER_SIZE;
        block = (header *)((char *)block + total);
    }
}
