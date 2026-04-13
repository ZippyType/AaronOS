#include <stdint.h>
#include <stddef.h>

#define HEAP_START 0x400000 // 4MB mark
#define HEAP_SIZE  0x100000 // 1MB size

static uint8_t* heap_ptr = (uint8_t*)HEAP_START;

void* kmalloc(size_t size) {
    // Simple bump allocation
    if ((uint32_t)heap_ptr + size > HEAP_START + HEAP_SIZE) {
        return NULL; // Out of memory
    }
    
    void* res = (void*)heap_ptr;
    heap_ptr += size;
    
    // Zero out the memory (simplifies things for now)
    for (size_t i = 0; i < size; i++) {
        ((uint8_t*)res)[i] = 0;
    }
    
    return res;
}

// In a bump allocator, we can't easily "free" specific blocks, 
// but we can reset the whole heap.
void kfree_all() {
    heap_ptr = (uint8_t*)HEAP_START;
}