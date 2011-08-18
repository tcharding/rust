// Object stacks, used in lieu of dynamically-sized frames.

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <stdint.h>

#include "rust_internal.h"
#include "rust_obstack.h"
#include "rust_task.h"

// ISAAC, let go of max()!
#ifdef max
#undef max
#endif

//#define DPRINT(fmt,...)     fprintf(stderr, fmt, ##__VA_ARGS__)
#define DPRINT(fmt,...)

//const size_t DEFAULT_CHUNK_SIZE = 4096;
const size_t DEFAULT_CHUNK_SIZE = 300000;
const size_t DEFAULT_ALIGNMENT = 16;

struct rust_obstack_chunk {
    rust_obstack_chunk *prev;
    size_t size;
    size_t alen;
    size_t pad;
    uint8_t data[];

    rust_obstack_chunk(rust_obstack_chunk *in_prev, size_t in_size)
    : prev(in_prev), size(in_size), alen(0) {}

    void *alloc(size_t len);
    bool free(void *ptr);
};

void *
rust_obstack_chunk::alloc(size_t len) {
    alen = align_to(alen, DEFAULT_ALIGNMENT);

    if (len > size - alen) {
        DPRINT("Not enough space, len=%lu!\n", len);
        assert(0);
        return NULL;    // Not enough space.
    }
    void *result = data + alen;
    alen += len;
    return result;
}

bool
rust_obstack_chunk::free(void *ptr) {
    uint8_t *p = (uint8_t *)ptr;
    if (p < data || p > data + size)
        return false;
    assert(p <= data + alen);
    alen = (size_t)(p - data);
    return true;
}

// Allocates the given number of bytes in a new chunk.
void *
rust_obstack::alloc_new(size_t len) {
    size_t chunk_size = std::max(len, DEFAULT_CHUNK_SIZE);
    void *ptr = task->malloc(sizeof(chunk) + chunk_size, "obstack");
    DPRINT("making new chunk at %p, len %lu\n", ptr, chunk_size);
    chunk = new(ptr) rust_obstack_chunk(chunk, chunk_size);
    return chunk->alloc(len);
}

rust_obstack::~rust_obstack() {
    while (chunk) {
        rust_obstack_chunk *prev = chunk->prev;
        task->free(chunk);
        chunk = prev;
    }
}

void *
rust_obstack::alloc(size_t len) {
    if (!chunk)
        return alloc_new(len);

    DPRINT("alloc sz %u", (uint32_t)len);

    void *ptr = chunk->alloc(len);
    ptr = ptr ? ptr : alloc_new(len);

    return ptr;
}

void
rust_obstack::free(void *ptr) {
    if (!ptr)
        return;

    DPRINT("free ptr %p\n", ptr);

    assert(chunk);
    while (!chunk->free(ptr)) {
        DPRINT("deleting chunk at %p\n", chunk);
        rust_obstack_chunk *prev = chunk->prev;
        task->free(chunk);
        chunk = prev;
        assert(chunk);
    }
}

