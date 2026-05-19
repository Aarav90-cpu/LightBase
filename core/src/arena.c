#include "arena.h"
#include <stdlib.h>
#include <string.h>

// Initialize a fixed continuous block of heap space from the host OS
MemoryArena* arena_init(size_t capacity) {
    MemoryArena* arena = (MemoryArena*)malloc(sizeof(MemoryArena));
    if (!arena) return NULL;

    arena->buffer = (uint8_t*)malloc(capacity);
    if (!arena->buffer) {
        free(arena);
        return NULL;
    }

    arena->capacity = capacity;
    arena->offset = 0;
    return arena;
}

// Slice out a clean chunk of bytes by simply sliding our offset bookmark forward
void* arena_alloc(MemoryArena* arena, size_t size) {
    // Implement 8-byte structural alignment to keep CPUs reading memory efficiently
    size_t aligned_size = (size + 7) & ~7;

    // Safety check: ensure our allocation request doesn't spill past our capacity wall
    if (arena->offset + aligned_size > arena->capacity) {
        return NULL; // Out of Arena bounds signal interrupt
    }

    void* ptr = &arena->buffer[arena->offset];
    arena->offset += aligned_size; // Slide the bookmark pointer forward!
    
    // Zero out the freshly allocated slice space defensively
    memset(ptr, 0, aligned_size);
    return ptr;
}

// Reset the entire tracking allocation stack instantly with zero overhead
void arena_reset(MemoryArena* arena) {
    arena->offset = 0;
}

// Release the global memory runway back to the kernel system tables
void arena_destroy(MemoryArena* arena) {
    if (arena) {
        free(arena->buffer);
        free(arena);
    }
}