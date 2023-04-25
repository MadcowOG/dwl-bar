#include "item.h"
#include "config.def.h"
#include "icon.h"
#include "cairo.h"
#include "config.h"
#include "icon.h"
#include "main.h"
#include "render.h"
#include "util.h"
#include "log.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <systemd/sd-bus.h>
#include <wayland-util.h>

static int check_message_sender(struct Item *item, sd_bus_message *m, const char *signal);
static void get_item_property(struct Item *item, const char *property, const char *type, void *dest);
static int get_property_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int handle_new_attention_icon(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int handle_new_icon(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int handle_new_status(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static void item_invalidate(struct Item *item);
static void item_match_signal(struct Item *item, char *signal, sd_bus_message_handler_t callback);
static cairo_surface_t *load_image(const char *path);
static cairo_surface_t *load_icon(struct Item *item, const char *theme, int size);
static int read_pixmap(sd_bus_message *m, struct Item *item, const char *property, struct wl_list *destination);
static cairo_surface_t *scale_icon(cairo_surface_t *icon, int width, int height);

const char *button_to_method(int button) {
    switch (button) {
        case BTN_LEFT:
            return "Activate";
        case BTN_MIDDLE:
            return "SecondaryActivate";
        case BTN_RIGHT:
            return "ContextMenu";
        case Scroll_Up:
            return "ScrollUp";
        case Scroll_Down:
            return "ScrollDown";
        case Scroll_Left:
            return "ScrollLeft";
        case Scroll_Right:
            return "ScrollRight";
        default:
            return "nop";
    }
}

int check_message_sender(struct Item *item, sd_bus_message *m, const char *signal) {
    int has_well_known_names = sd_bus_creds_get_mask(sd_bus_message_get_creds(m)) & SD_BUS_CREDS_WELL_KNOWN_NAMES;
    if (item->service[0] == ':' || has_well_known_names)
        return 1;

    return 0;
}

void get_item_property(struct Item *item, const char *property, const char *type, void *destination) {
    struct ItemData *data = ecalloc(1, sizeof(*data));
    data->item        = item;
    data->property    = property;
    data->type        = type;
    data->destination = destination;

    if (sd_bus_call_method_async(item->tray->bus, &data->slot, item->service,
			item->path, "org.freedesktop.DBus.Properties", "Get",
			get_property_callback, data, "ss", item->interface, property) < 0) {
        free(data);
        return;
    }

    wl_list_insert(&item->item_data, &data->link);
}

int get_property_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct ItemData *data = userdata;
    struct Item *item = data->item;
    const char *property = data->property,
               *type = data->type;
    if (sd_bus_message_is_method_error(m, NULL))
        goto cleanup;

    if (sd_bus_message_enter_container(m, 'v', type) < 0)
        panic("get_property_callback sd_bus_message_enter_container");

    if (!type) {
        if (!read_pixmap(m, item, property, (struct wl_list*)data->destination))
            goto cleanup;
    } else {
        if (*type == 's' || *type == 'o')
            free(*(char**)data->destination);

        if (sd_bus_message_read(m, type, data->destination) < 0)
            panic("get_property_callback sd_bus_message_read");

        if (*type == 's' || *type == 'o') {
            char **str = data->destination;
            *str = strdup(*str);
        }
    }

    item_invalidate(item);

cleanup:
    wl_list_remove(&data->link);
    free(data);
    return 1;
}

int handle_new_attention_icon(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct ItemData *data = userdata;
    struct Item *item = data->item;
    struct Pixmap *pixmap, *tmp;
    wl_list_for_each_safe(pixmap, tmp, &item->attention_pixmap, link) {
        wl_list_remove(&pixmap->link);
        free(pixmap->pixels);
        free(pixmap);
    }

    get_item_property(item, "AttentionIconName", "s", &item->attention_icon_name);
    get_item_property(item, "AttentionIconPixmap", NULL, &item->attention_pixmap);

    return check_message_sender(item, m, "attention icon");
}

int handle_new_icon(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct ItemData *data = userdata;
    struct Item *item = data->item;
    struct Pixmap *pixmap, *tmp;
    wl_list_for_each_safe(pixmap, tmp, &item->icon_pixmap, link) {
        if (!pixmap) continue;
        wl_list_remove(&pixmap->link);
        free(pixmap->pixels);
        free(pixmap);
    }

    get_item_property(item, "IconName", "s", &item->icon_name);
    get_item_property(item, "IconPixmap", NULL, &item->icon_pixmap);
    if (STRING_EQUAL(item->interface, "org.kde.StatusNotifierItem"))
        get_item_property(item, "IconThemePath", "s", &item->icon_theme_path);

    return check_message_sender(item, m, "icon");
}

int handle_new_status(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct ItemData *data = userdata;
    struct Item *item = data->item;
    int ret = check_message_sender(item, m, "status");
    if (ret) {
        char *status;
        if (sd_bus_message_read(m, "s", &status) < 0) {
            return 0;
        } else {
            if (item->status)
                free(item->status);
            item->status = strdup(status);
            item_invalidate(item);
        }
    } else {
        get_item_property(item, "Status", "s", &item->status);
    }

    return ret;
}

int is_passive(struct Item *item) {
    if (!item) return 0;

    return item->status && item->status[0] == 'P';
}

int is_ready(struct Item *item) {
    if (!item) return 0;

    return item->status && (item->status[0] == 'N' ?
            item->attention_icon_name || !(wl_list_empty(&item->attention_pixmap)) :
            item->icon_name || !(wl_list_empty(&item->icon_pixmap)));
}

struct Item *item_create(struct Tray* tray, char *id) {
    if (!tray || !id) return NULL;

    struct Item *item = ecalloc(1, sizeof(*item));
    char *path;

    item->tray        = tray;
    item->watcher_id  = strdup(id);
    item->status = item->icon_name = item->attention_icon_name =
        item->menu_path =  item->menu_path = NULL;
    wl_list_init(&item->icon_pixmap);
    wl_list_init(&item->attention_pixmap);
    wl_list_init(&item->item_data);

    path = strchr(id, '/');
    if (!path) {
        item->service   = strdup(id);
        item->path      = strdup("/StatusNotifierItem");
        item->interface = "org.freedesktop.StatusNotifierItem";
    } else {
        item->service   = strndup(id, path - id);
        item->path      = strdup(path);
        item->interface = "org.kde.StatusNotifierItem";
        get_item_property(item, "IconThemePath", "s", (void**)&item->icon_theme_path);
    }

    get_item_property(item, "Status", "s", &item->status);
    get_item_property(item, "IconName", "s", &item->icon_name);
    get_item_property(item, "IconPixmap", NULL, &item->icon_pixmap);
    get_item_property(item, "AttentionIconName", "s", &item->attention_icon_name);
    get_item_property(item, "AttentionIconPixmap", NULL, &item->attention_pixmap);
    get_item_property(item, "ItemIsMenu", "b", &item->is_menu);
    get_item_property(item, "Menu", "o", &item->menu_path);

    item_match_signal(item, "NewIcon", handle_new_icon);
    item_match_signal(item, "NewAttentionIcon", handle_new_attention_icon);
    item_match_signal(item, "NewStatus", handle_new_status);

    return item;
}

void item_destroy(struct Item* item) {
    if (!item) return;

    free(item->watcher_id);
    free(item->service);
    free(item->path);
    free(item->status);
    free(item->icon_name);
    free(item->attention_icon_name);
    free(item->menu_path);
    free(item->icon_theme_path);
    cairo_surface_destroy(item->icon);

    struct Pixmap *pixmap, *tmp;
    wl_list_for_each_safe(pixmap, tmp, &item->icon_pixmap, link) {
        wl_list_remove(&pixmap->link);
        free(pixmap->pixels);
        free(pixmap);
    }
    wl_list_for_each_safe(pixmap, tmp, &item->attention_pixmap, link) {
        wl_list_remove(&pixmap->link);
        free(pixmap->pixels);
        free(pixmap);
    }

    struct ItemData *data, *temp;
    wl_list_for_each_safe(data, temp, &item->item_data, link) {
        wl_list_remove(&data->link);
        sd_bus_slot_unref(data->slot);
        free(data);
    }

    free(item);
}

void item_invalidate(struct Item *item) {
    item->invalid = 1;
    monitors_update();
}

int item_is_clicked(struct Item *item, double x, double y) {
    if (!item) return 0;

    return (x > item->x && y > item->y &&
            x < (item->x + item->width) && y < (item->y + item->height));
}

void item_match_signal(struct Item *item, char *signal, sd_bus_message_handler_t callback) {
    struct ItemData *data = ecalloc(1, sizeof(*data));
    data->item = item;
    if (sd_bus_match_signal_async(item->tray->bus, &data->slot,
                item->service, item->path, item->interface, signal, callback, NULL, data) < 0) {
        free(data);
        return;
    }

    wl_list_insert(&item->item_data, &data->link);
}

void item_render(struct Pipeline *pipeline, cairo_t *painter, struct Item *item, int *x, int *y) {
    if (is_passive(item) || !is_ready(item) || !pipeline || !painter || !item || !x || !y)
        return;

    int icon_size = pipeline->font->height;
    item->x       = *x;
    item->y       = *y;
    item->width   = icon_size+2;
    item->height  = pipeline->shm->height;

    pipeline_set_colorscheme(pipeline, schemes[InActive_Scheme]);
    pipeline_color_background(pipeline, painter);
    cairo_rectangle(painter, *x, 0, item->width, item->height);
    cairo_fill(painter);

    if (item->invalid) {
        cairo_surface_destroy(item->icon);
        item->icon = load_icon(item, icon_theme, icon_size);
        item->invalid = 0;
    }

    if (!item->icon) {
        int sad_face_size = icon_size*0.8;
        cairo_surface_t *item_icon = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                icon_size, icon_size);
        cairo_t *sad_face = cairo_create(item_icon);
        cairo_set_source_rgba(painter,
                (double)schemes[Active_Scheme][0][0]/255,
                (double)schemes[Active_Scheme][0][1]/255,
                (double)schemes[Active_Scheme][0][2]/255,
                (double)schemes[Active_Scheme][0][3]/255);
        cairo_translate(sad_face, (double)icon_size/2, (double)icon_size/2);
        cairo_scale(sad_face, (double)icon_size/2, (double)icon_size/2);
        cairo_arc(sad_face, 0, 0, 1, 0, 7);
        cairo_fill(sad_face);
        cairo_set_operator(sad_face, CAIRO_OPERATOR_CLEAR);
        cairo_arc(sad_face, 0.35, -0.3, 0.1, 0, 7);
        cairo_fill(sad_face);
        cairo_arc(sad_face, -0.35, -0.3, 0.1, 0, 7);
        cairo_fill(sad_face);
        cairo_arc(sad_face, 0, 0.75, 0.5, 3.71238898038469, 5.71238898038469);
        cairo_set_line_width(sad_face, 0.1);
        cairo_stroke(sad_face);
        cairo_destroy(sad_face);
        item->icon = item_icon;
    }

    cairo_operator_t op = cairo_get_operator(painter);
    cairo_set_operator(painter, CAIRO_OPERATOR_OVER);

    int actual_size = cairo_image_surface_get_height(item->icon);
    icon_size = actual_size < icon_size ?
        actual_size * (icon_size / actual_size) : icon_size;
    cairo_surface_t *image = scale_icon(item->icon, icon_size, icon_size);
    cairo_set_source_surface(painter, image, *x+1, item->height - icon_size - 1);
    cairo_paint(painter);

    cairo_set_operator(painter, op);
    *x += item->width;
}

cairo_surface_t *load_image(const char *path) {
    cairo_surface_t *image = cairo_image_surface_create_from_png(path);
    return (!image || cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) ? NULL : image;
}

cairo_surface_t *load_icon(struct Item *item, const char *theme, int size) {
    cairo_surface_t *icon = NULL;
    char *name = item->status[0] == 'N' ? item->attention_icon_name : item->icon_name;
    if (name) {
        struct List *search_paths = list_create(item->tray->basedirs->length+1);
        list_copy(search_paths, item->tray->basedirs);
        if (item->icon_theme_path)
            list_add(search_paths, item->icon_theme_path);

        char *path = get_icon(item->tray->themes, search_paths, name, size, theme);

        list_destroy(search_paths);

        if (path) {
            icon = load_image(path);
            free(path);
            return icon;
        }
    }

    struct wl_list *pixmaps = item->status[0] == 'N' ? &item->attention_pixmap : &item->icon_pixmap;
    if (!wl_list_empty(pixmaps)) {
        struct Pixmap *pixmap = NULL, *pos;
        int min_error = INT_MAX;
        wl_list_for_each(pos, pixmaps, link) {
            int e = abs(size - pos->size);
            if (e < min_error) {
                pixmap = pos;
                min_error = e;
            }
        }
        icon = cairo_image_surface_create_for_data((unsigned char*)pixmap->pixels, CAIRO_FORMAT_ARGB32,
                pixmap->size, pixmap->size, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pixmap->size));
    }

    return icon;
}

// We assume that when we get the list that it is valid.
int read_pixmap(sd_bus_message *m, struct Item *item, const char *property, struct wl_list *destination) {
    if (sd_bus_message_enter_container(m, 'a', "(iiay)") < 0)
        panic("read_pixmap sd_bus_message_enter_container 'a'");

    if (sd_bus_message_at_end(m, 0)) // No Icon
        return 0;

    while(!sd_bus_message_at_end(m, 0)) {
        if (sd_bus_message_enter_container(m, 'r', "iiay") < 0)
            panic("read_pixmap sd_bus_message_enter_container 'r'");

        int width, height;
        if (sd_bus_message_read(m, "ii", &width, &height) < 0)
            panic("read_pixmap sd_bus_message_read width height");

        const void *pixels;
        size_t npixels;
        if (sd_bus_message_read_array(m, 'y', &pixels, &npixels) < 0)
            panic("read_pixmap sd_bus_message_read_array pixels npixels");

        if (height > 0 && width == height) { // If is valid icon
            struct Pixmap *pixmap = ecalloc(1, sizeof(struct Pixmap));
            pixmap->pixels = ecalloc(npixels, sizeof(uint32_t*));
            pixmap->size   = height;

            // Covert to host byte order from network order.
            for (int i = 0; i < height * width; i++)
                pixmap->pixels[i] = ntohl(((uint32_t*)pixels)[i]);

            wl_list_insert(destination, &pixmap->link);
        }

        sd_bus_message_exit_container(m);
    }

    if (wl_list_empty(destination))
        return 0;

    return 1;
}

cairo_surface_t *scale_icon(cairo_surface_t *icon, int width, int height) {
    int image_height = cairo_image_surface_get_height(icon);
    int image_width  = cairo_image_surface_get_width(icon);

    cairo_surface_t *new = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *painter = cairo_create(new);
    cairo_scale(painter, (double)width / image_width,
                         (double)height / image_height);
    cairo_set_source_surface(painter, icon, 0, 0);

    cairo_paint(painter);
    cairo_destroy(painter);
    return new;
}
