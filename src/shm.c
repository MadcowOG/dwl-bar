#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "common.h"
#include "shm.h"

/* For unmapping the mapped memory when we're finished with the shared memory. */
typedef struct MemoryMapping {
    void* ptr;
    int size;
} MemoryMapping;

/* To help us keep track of our wayland buffers and their data. */
typedef struct Buffer {
    wl_buffer* buffer;
    uint8_t* buffer_ptr;
} Buffer;

struct ShmPriv {
    MemoryMapping* memory;
    Buffer buffers[2];
    int current; // Since we hold multiple buffers we need to know which we're currently using.
};

static MemoryMapping* memory_mapping_create(int fd, int pool_size);
void memory_mapping_destroy(MemoryMapping* map);

static Buffer buffer_create(MemoryMapping* memmap, wl_shm_pool* shm, int fd, int width, int height, int offset, wl_shm_format format);
static void buffer_destroy(Buffer* buf);

static int allocate_shm(int size);

static const int buffer_amnt = 2;
int allocate_shm(int size) {
    char name[] = "wl_shm";
    int fd;

    if ((fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600)) < 0) {
        die("shm_open when allocating shm");
    }
    shm_unlink(name);
    if (ftruncate(fd, size) < 0) {
        die("ftruncate when allocating shm");
    }

    return fd;
}

MemoryMapping* memory_mapping_create(int fd, int pool_size) {
    MemoryMapping* map = ecalloc(1, sizeof(MemoryMapping));
    void* ptr = mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED || !ptr) {
        close(fd);
        die("MAP_FAILED");
    }

    map->ptr = ptr;
    map->size = pool_size;

    return map;
}

void memory_mapping_destroy(MemoryMapping* map) {
    munmap(map->ptr, map->size);
    free(map);
}

Buffer buffer_create(MemoryMapping* memmap, wl_shm_pool* pool, int fd, int width, int height, int offset, wl_shm_format format) {
    int stride    = width * 4,
        pool_size = height * stride;
    Buffer buffer;

    if (!memmap)
        die("memmap is null");

    wl_buffer* wl_buf = wl_shm_pool_create_buffer(pool, offset, width, height, stride, format);

    buffer.buffer = wl_buf;
    buffer.buffer_ptr = memmap->ptr+offset;

    return buffer;
}

void buffer_destroy(Buffer* buffer) {
    wl_buffer_destroy(buffer->buffer);
}

Shm* shm_create(int w, int h, wl_shm_format format) {
    Shm* _shm = calloc(1, sizeof(Shm));
    ShmPriv* priv = calloc(1, sizeof(ShmPriv));
    MemoryMapping* memory;
    int i, offset,
        stride = w * 4,
        size = stride * h,
        total = size * buffer_amnt;
    int fd = allocate_shm(total);

    memory = memory_mapping_create(fd, total);
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, total);
    for (i = 0; i < buffer_amnt; i++) {
        offset = size*i;
        priv->buffers[i] = buffer_create(memory, pool, fd, w, h, offset, format);
    }
    close(fd);
    wl_shm_pool_destroy(pool);

    priv->memory = memory;
    priv->current = 0;

    _shm->priv = priv;
    _shm->height = h;
    _shm->width = w;
    _shm->stride = stride;

    return _shm;
}

void shm_destroy(Shm* shm) {
    uint i;
    if (shm->priv->memory) {
        memory_mapping_destroy(shm->priv->memory);
    }

    for (i = 0; i < buffer_amnt; i++) { // REVIEW this may cause problems
        if (&shm->priv->buffers[i]) {
            buffer_destroy(&shm->priv->buffers[i]);
        }
    }
}

uint8_t* shm_data(Shm *shm) {
    return shm->priv->buffers[shm->priv->current].buffer_ptr;
}

wl_buffer* shm_buffer(Shm *shm) {
    return shm->priv->buffers[shm->priv->current].buffer;
}

void shm_flip(Shm *shm) {
    shm->priv->current = 1-shm->priv->current;
}
