#ifndef SHM_H_
#define SHM_H_
#include "common.h"
#include <stdint.h>
#include <wayland-client-protocol.h>

typedef struct ShmPriv ShmPriv;

struct Shm {
    int width;
    int height;
    int stride;

    ShmPriv* priv;
};
typedef struct Shm Shm;

Shm* shm_create(int w, int h, wl_shm_format format);
void shm_destroy(Shm* shm);
uint8_t* shm_data(Shm* shm);
wl_buffer* shm_buffer(Shm* shm);
void shm_flip(Shm* shm);

#endif // SHM_H_
