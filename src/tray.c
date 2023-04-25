#include "log.h"
#include "main.h"
#include "icon.h"
#include "item.h"
#include "render.h"
#include "tray.h"
#include "user.h"
#include "watcher.h"
#include "host.h"
#include "util.h"
#include "input.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <wayland-util.h>
#include <linux/input-event-codes.h>

// Thanks to the ianyfan from swaybar for the tray code,
// I don't have a clue of how to use sd-bus, so that code was useful.

static int handle_lost_watcher(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static void tray_click(struct Monitor *monitor, void *data, uint32_t button, double x, double y);
static void tray_render(struct Pipeline *pipeline, void *data, cairo_t *painter, int *x, int *y);
static int tray_width(struct Pipeline *pipeline, void *data, unsigned int future_widths);
static void tray_bounds(void *data, double *x, double *y, double *width, double *height);

static const struct PipelineListener tray_pipeline_listener = { .render = tray_render, .width = tray_width, };
static const struct HotspotListener tray_hotspot_listener = { .click = tray_click, .bounds = tray_bounds };

int cmp_id(const void *left, const void *right) {
    if (!left || !right)
        return -1;

    return strcmp(left, right);
}

int handle_lost_watcher(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    char *service, *old_owner, *new_owner;
    if (sd_bus_message_read(m, "sss", &service, &old_owner, &new_owner) < 0)
        panic("handle_lost_watcher sd_bus_message_read");

    if (!*new_owner) {
        struct Tray *tray = userdata;

        if (STRING_EQUAL(service, "org.freedesktop.StatusNotifierWatcher")) {
            tray->xdg_watcher = watcher_create("freedesktop", tray->bus);
        } else if (STRING_EQUAL(service, "org.kde.StatusNotifierWatcher")) {
            tray->kde_watcher = watcher_create("kde", tray->bus);
        }
    }

    return 0;
}

struct Tray *tray_create(void) {
    struct Tray *tray;
    sd_bus *bus;

    if (sd_bus_open_user(&bus) < 0)
        panic("sd_bus_open_user");

    tray = ecalloc(1, sizeof(*tray));
    tray->bus = bus;
    tray->bus_fd = sd_bus_get_fd(tray->bus);
    tray->themes = list_create(0);
    tray->basedirs = list_create(0);
    wl_list_init(&tray->items);

    tray->xdg_watcher = watcher_create("freedesktop", bus);
    tray->kde_watcher = watcher_create("kde", bus);

    if (sd_bus_match_signal(bus, NULL, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "NameOwnerChanged", handle_lost_watcher, tray) < 0)
        panic("Failed to subscribe to NameOwnerChange");

    tray->xdg_host = host_create("freedesktop", tray);
    tray->kde_host = host_create("kde", tray);

    themes_create(tray->themes, tray->basedirs);

    return tray;
}

void tray_destroy(struct Tray *tray) {
    if (!tray) return;

    host_destroy(tray->xdg_host);
    host_destroy(tray->kde_host);

    watcher_destroy(tray->kde_watcher);
    watcher_destroy(tray->xdg_watcher);

    struct Item *item, *tmp;
    wl_list_for_each_safe(item, tmp, &tray->items, link) {
        wl_list_remove(&item->link);
        item_destroy(item);
    }

    themes_destroy(tray->themes, tray->basedirs);
    free(tray);
}

void tray_click(struct Monitor *monitor, void *data, uint32_t button, double x, double y) {
    struct Tray *tray = data;
    struct Item *item = NULL;
    wl_list_for_each(item, &tray->items, link) {
        if (item_is_clicked(item, x, y))
            break;
    }

    if (!item)
        return;

    union Arg arg;
    struct ItemClick click = { item, x, y, button };
    arg.v = &click;

    const struct Binding *binding;
    for (int i = 0; i < LENGTH(bindings); i++) {
        binding = &bindings[i];
        if (binding->button != button || binding->clicked != Click_Systray)
            continue;

        binding->callback(monitor, binding->bypass ? &binding->arg : &arg);
    }
}

void tray_register_to_monitor(struct Tray *tray, struct List *hotspots, struct Pipeline *pipeline) {
    if (!tray || !hotspots || !pipeline) return;

    tray->pipeline = pipeline;
    pipeline_add(pipeline, &tray_pipeline_listener, tray);
    struct Hotspot *hotspot = list_add(hotspots, ecalloc(1, sizeof(*hotspot)));
    hotspot->listener = &tray_hotspot_listener;
    hotspot->data = tray;
}

void tray_render(struct Pipeline *pipeline, void *data, cairo_t *painter, int *x, int *y) {
    struct Tray *tray = data;
    tray->x = *x;
    tray->y = *y;
    struct Item *item;
    wl_list_for_each(item, &tray->items, link)
        item_render(pipeline, painter, item, x, y);
}

int tray_width(struct Pipeline *pipeline, void *data, unsigned int future_widths) {
    struct Tray *tray = data;

    int amount = 0;
    struct Item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (!is_passive(item) && is_ready(item))
            amount++;
    }

    return (pipeline->font->height + 2) * amount;
}

static void tray_bounds(void *data, double *x, double *y, double *width, double *height) {
    struct Tray *tray = data;
    *x = tray->x;
    *y = tray->y;
    *width = tray_width(tray->pipeline, tray, 0);
    *height = tray->pipeline->shm->height;
}
