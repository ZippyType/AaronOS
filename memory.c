#include <stdint.h>
#include <stddef.h>

#define HEAP_START 0x400000
#define HEAP_SIZE  0x100000
#define BLOCK_MAGIC 0xABBA
#define ALIGN 4

typedef struct block {
    uint16_t magic;
    uint16_t flags;
    uint32_t size;
    struct block* next;
    struct block* prev;
} block_t;

#define FLAG_USED 1
#define MIN_BLOCK (sizeof(block_t) + ALIGN)

static block_t* free_list = 0;

static void heap_init() {
    block_t* b = (block_t*)HEAP_START;
    b->magic = BLOCK_MAGIC;
    b->flags = 0;
    b->size = HEAP_SIZE - sizeof(block_t);
    b->next = 0;
    b->prev = 0;
    free_list = b;
}

static block_t* block_from_ptr(void* ptr) {
    return (block_t*)((uint8_t*)ptr - sizeof(block_t));
}

void* malloc(size_t size) {
    if (!free_list) heap_init();
    if (size == 0) size = 1;
    if (size & (ALIGN - 1)) size = (size + ALIGN - 1) & ~(ALIGN - 1);
    if (size < MIN_BLOCK) size = MIN_BLOCK;

    block_t* best = 0;
    uint32_t best_size = 0xFFFFFFFF;
    for (block_t* b = free_list; b; b = b->next) {
        if (b->magic != BLOCK_MAGIC) continue;
        if (b->flags & FLAG_USED) continue;
        if (b->size >= size && b->size < best_size) {
            best = b;
            best_size = b->size;
        }
    }
    if (!best) return 0;

    uint32_t remain = best->size - size;
    if (remain >= MIN_BLOCK + ALIGN) {
        block_t* newb = (block_t*)((uint8_t*)best + sizeof(block_t) + size);
        newb->magic = BLOCK_MAGIC;
        newb->flags = 0;
        newb->size = remain - sizeof(block_t);
        newb->next = best->next;
        newb->prev = best;
        if (best->next) best->next->prev = newb;
        best->next = newb;
        best->size = size;
    }
    best->flags |= FLAG_USED;

    if (best == free_list && (best->flags & FLAG_USED)) {
        free_list = best->next;
        if (free_list) free_list->prev = 0;
    } else if (best->prev && (best->prev->flags & FLAG_USED)) {
        /* not at front, fine */
    }

    return (void*)((uint8_t*)best + sizeof(block_t));
}

void free(void* ptr) {
    if (!ptr) return;
    block_t* b = block_from_ptr(ptr);
    if (b->magic != BLOCK_MAGIC) return;
    if (!(b->flags & FLAG_USED)) return;
    b->flags &= ~FLAG_USED;

    block_t* ins = free_list;
    block_t* ins_prev = 0;
    while (ins && (uint32_t)ins < (uint32_t)b) {
        ins_prev = ins;
        ins = ins->next;
    }
    b->prev = ins_prev;
    b->next = ins;
    if (ins_prev) ins_prev->next = b;
    else free_list = b;
    if (ins) ins->prev = b;

    /* coalesce next */
    if (b->next && (uint32_t)b->next == (uint32_t)b + sizeof(block_t) + b->size) {
        if (!(b->next->flags & FLAG_USED)) {
            b->size += sizeof(block_t) + b->next->size;
            b->next = b->next->next;
            if (b->next) b->next->prev = b;
        }
    }
    /* coalesce prev */
    if (b->prev && (uint32_t)b->prev + sizeof(block_t) + b->prev->size == (uint32_t)b) {
        if (!(b->prev->flags & FLAG_USED)) {
            b->prev->size += sizeof(block_t) + b->size;
            b->prev->next = b->next;
            if (b->next) b->next->prev = b->prev;
            b = b->prev;
        }
    }
}

void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    void* p = malloc(total);
    if (p) for (size_t i = 0; i < total; i++) ((uint8_t*)p)[i] = 0;
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    block_t* b = block_from_ptr(ptr);
    size_t old_size = b->size;
    void* newp = malloc(size);
    if (newp) {
        size_t copy = old_size < size ? old_size : size;
        for (size_t i = 0; i < copy; i++) ((uint8_t*)newp)[i] = ((uint8_t*)ptr)[i];
        free(ptr);
    }
    return newp;
}
