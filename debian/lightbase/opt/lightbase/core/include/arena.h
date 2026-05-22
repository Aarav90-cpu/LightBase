#ifndef LIGHTBASE_ARENA_H
#define LIGHTBASE_ARENA_H

#include <stddef.h>
#include <stdint.h>

// Structural blueprint tracking our block boundaries
typedef struct {
    uint8_t* buffer;     // Raw pointer targeting the base address of our memory chunk
    size_t capacity;     // Total allocation footprint limit of the arena pool
    size_t offset;       // Current tracking offset bookmark for the next slice allocation
} MemoryArena;

// Interface signatures to manage arena lifecycles
MemoryArena* arena_init(size_t capacity);
void* arena_alloc(MemoryArena* arena, size_t size);
void arena_reset(MemoryArena* arena);
void arena_destroy(MemoryArena* arena);

#endif // LIGHTBASE_ARENA_H