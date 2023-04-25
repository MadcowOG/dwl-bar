#ifndef ITEM_H_
#define ITEM_H_

#include <stdint.h>
#include <sys/types.h>
#include <wayland-util.h>
#include "cairo.h"
#include "tray.h"
#include "main.h"
#include "user.h"

struct Pixmap {
    int size;
    uint32_t *pixels;
    struct wl_list link;
};

struct ItemData {
    struct Item *item;
    const char *property, *type;
    void *destination;
    sd_bus_slot *slot;

    struct wl_list link;
};

struct ItemClick {
    const struct Item *item;
    const int x, y, button;
};

struct Item {
    struct Tray *tray;
    cairo_surface_t *icon;
    int is_menu, invalid, x, y, width, height;

    char *watcher_id, *service, *path, *interface,
         *status, *icon_name, *attention_icon_name,
         *menu_path, *icon_theme_path /* Non-standard kde property */;

    struct wl_list icon_pixmap, attention_pixmap, item_data, link;
};

const char *button_to_method(int button);
int is_passive(struct Item *item);
int is_ready(struct Item *item);
struct Item *item_create(struct Tray* tray, char *id);
void item_destroy(struct Item* item);
int item_is_clicked(struct Item *item, double x, double y);
void item_render(struct Pipeline *pipeline, cairo_t *painter, struct Item *item, int *x, int *y);

#endif // ITEM_H_
