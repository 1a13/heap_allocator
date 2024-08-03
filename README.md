An implicit and explicit implementation of a heap allocator. 

The implicit allocator has headers that track block information (size, status in-use or free) and search for large enough free blocks by traversing block-by-block.

The explicit allocator sees large performance improvements as it utilizes a free block linked list to find free blocks to fulfill malloc and realloc requests and supports in-place realloc via coalescing adjacent free blocks. The free list is implemented using a LIFO strategy and a global variable is kept to track the first free block in the linked list.

The function validate_heap is used to verify constraints of the heap allocator are met when the test_harness is run, and validate_heap provides a visual output for debugging.

A key assumption of this program is that the user has not overwritten any memory in the heap (as this would overwrite key information in the headers).
