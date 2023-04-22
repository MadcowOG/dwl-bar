#ifndef MAIN_H_
#define MAIN_H_

#include "xdg-output-unstable-v1-protocol.h"
#include <stdint.h>
#include <wayland-client.h>
#include <wayland-util.h>

#define VERSION 0.0

struct Monitor {
    char *xdg_name;
    uint32_t wl_name;

    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;
    struct Pipeline *pipeline;
    struct List *hotspots; /* struct Hotspot* */
    struct Bar *bar;

    struct wl_list link;
};

void panic(const char *fmt, ...);
void monitors_update(void);
struct Monitor *monitor_from_surface(const struct wl_surface *surface);

extern struct wl_compositor *compositor;
extern struct zwlr_layer_shell_v1 *shell;
extern struct wl_shm *shm;

#endif // MAIN_H_
