#ifndef TRAY_H_
#define TRAY_H_

#include <wayland-util.h>
#include <cairo.h>

#include "render.h"
#include "lib.h"
#if SYSTEMD
#include <systemd/sd-bus.h>
#elif ELOGIND
#include <elogind/sd-bus.h>
#elif BASU
#include <basu/sd-bus.h>
#else
#error "No dbus library to use"
#endif

static const char *watcher_path = "/StatusNotifierWatcher";

struct Tray {
    sd_bus *bus;
    int bus_fd, x, y;

    struct Pipeline *pipeline;

    struct Watcher *xdg_watcher, *kde_watcher;
    struct Host *xdg_host, *kde_host;

    struct wl_list items; // struct Item*

    struct List *themes   /* struct Theme* */,
                *basedirs /* char* */;
};

int cmp_id(const void *left, const void *right);
struct Tray *tray_create(void);
void tray_destroy(struct Tray *tray);
void tray_register_to_monitor(struct Tray *tray, struct List *hotspots, struct Pipeline *pipeline);

#endif // TRAY_H_
