#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <wayland-cursor.h>
#include <wayland-util.h>
#include <linux/input-event-codes.h>
#include <pixman-1/pixman.h>
#include <fcft/fcft.h>
#include "uft8.h"
#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

#define BUFFER_AMNT 2
#define TOUCH_POINT_AMNT 16
#define VALUE_OR(data, default) (data ? data : default)
#define TITLE(title) (VALUE_OR(title, ""))
#define STATUS(status) (VALUE_OR(status, "dwl-bar 1.0"))
#define STRING_EQUAL(string1, string2) (strcmp(string1, string2) == 0)
#define TEXT_WIDTH(text, max) (draw_text_fg_bg(NULL, NULL, NULL, NULL, FCFT_SUBPIXEL_NONE, text, 0, 0, max, 0, 0, 0, 0))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define WL_ARRAY_LENGTH(array, type) ((array)->size/sizeof(type))
#define WL_ARRAY_AT(array, type, index) (*(((type*)((array)->data))+index))
#define WL_ARRAY_REMOVE(array, type, index) {\
    memmove(((type*)(array)->data)+index,  ((type*)((array)->data))+index+1, \
            sizeof(type*) * (WL_ARRAY_LENGTH(array, type) - index)); \
    (array)->size -= sizeof(type); \
}
#define WL_ARRAY_ADD(array, data) { \
    typeof(data) *thing = wl_array_add(array, sizeof(typeof(data))); \
    *thing = data; \
}

struct bar;

enum color_scheme {
    inactive_scheme = 0,
    active_scheme = 1,
    urgent_scheme = 2,
};

enum clicked {
    click_none = 0,
    click_tag = 1 << 0,
    click_layout = 1 << 1,
    click_title = 1 << 2,
    click_status = 1 << 3,
};

enum scroll {
    scroll_up,
    scroll_down,
    scroll_left,
    scroll_right,
};

union arg {
    uint32_t u32;
    int32_t i32;
    const void *v;
};

struct binding {
    enum clicked clicked;
    int button;
    void (*click)(struct bar *bar, const union arg *arg);
    bool bypass; /* Informs the click function that they should only pass the defined arg in this binding */
    const union arg arg;
};

struct pointer {
    struct wl_pointer *wl_pointer;
    struct bar *selected_bar; // Can be NULL

    struct wl_surface *cursor_surface;
    struct wl_cursor_image *cursor_image;
    struct wl_cursor_theme *cursor_theme;

    struct {
        wl_fixed_t scroll_value;
        bool discrete;
    } axis[2];

    uint32_t x, y;
};

struct touch {
    struct wl_touch *wl_touch;
    struct touch_point {
        int32_t id;
        struct bar *selected_bar; // Can be NULL;
        uint32_t time;
        wl_fixed_t x, start_x,
                   y, start_y;
    } touch_points[TOUCH_POINT_AMNT];
};

struct seat {
    struct wl_seat *wl_seat;
    uint32_t wl_seat_name;

    struct pointer *pointer;
    struct touch *touch;

    struct wl_list link;
};

struct bar {
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_output *wl_output;

    struct {
        void *ptr;
        size_t size;
    } memory_map;

    struct {
        struct wl_buffer *buffer;
        void *ptr;
    } buffers[BUFFER_AMNT];

    char *output_name,
         *layout,
         *title, /* Can be NULL */
         *status; /* Can be NULL */

    uint32_t width, height, wl_output_name,
             tagset, occupied, urgent, client_tagset;
    uint8_t current_buffer;
    int32_t scale;
    enum wl_output_subpixel subpixel; // At the moment wl_output_subpixel and fcft_subpixel
                                      // directly map to eachother but this is risky,
                                      // because if something in either lib changes we break.
    bool selected, // The output is selected
         floating, // The selected client floating
         visible,
         invalid; // false if bar's state is reflected in render,
                  // true if it isn't and we need to render again.

    struct wl_list link;
};

static int32_t allocate_shm(size_t size);
static void randomize_string(char *str);
static void bar_destroy(struct bar *bar);
static void bar_determine_visibility(struct bar *bar, bool visibility, bool toggle);
static void bar_draw(struct bar *bar);
static enum clicked bar_get_clicked(struct bar *bar, uint32_t *tag, uint32_t x);
static void bar_show(struct bar *bar);
static void bar_hide(struct bar *bar);
static void bar_render(struct bar *bar);
static void check_global(const char *name, void *data);
static void check_globals(void);
static void cleanup(void);
static int display_in(int fd, uint32_t mask, void *data);
static uint32_t draw_text_fg_bg(pixman_image_t *foreground, const pixman_color_t *foreground_color, pixman_image_t *background, const pixman_color_t *background_color, enum fcft_subpixel subpixel, const char *text, uint32_t left_padding, uint32_t bottom_padding, int32_t max_text_length, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
static void *ecalloc(size_t nmemb, size_t size);
static int fifo_in(int fd, uint32_t mask, void *data);
static void fifo_setup(void);
static void panic(const char *fmt, ...);
static void process_bindings(struct bar *bar, uint32_t x, uint32_t y, uint32_t button, void *value);
static void process_touch_point(struct touch_point *point);
static void seat_destroy(struct seat *seat);
static int signal_handler(int signal_number, void *data);
static int stdin_in(int fd, uint32_t mask, void *data);
static void spawn(struct bar *bar, const union arg *arg);
static struct touch_point *touch_find_point(struct touch *touch, int32_t id);
static void wl_callback_frame_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data);
static void wl_output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform);
static void wl_output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
static void wl_output_done(void *data, struct wl_output *wl_output);
static void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor);
static void wl_output_name(void *data, struct wl_output *wl_output, const char *name);
static void wl_output_description(void *data, struct wl_output *wl_output, const char *description);
static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface);
static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer);
static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source);
static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis);
static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete);
static void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120);
static void wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction);
static void wl_touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void wl_touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, int32_t id);
static void wl_touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void wl_touch_frame(void *data, struct wl_touch *wl_touch);
static void wl_touch_cancel(void *data, struct wl_touch *wl_touch);
static void wl_touch_shape(void *data, struct wl_touch *wl_touch, int32_t id, wl_fixed_t major, wl_fixed_t minor);
static void wl_touch_orientation(void *data, struct wl_touch *wl_touch, int32_t id, wl_fixed_t orientation);
static void wl_registry_global_add(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version);
static void wl_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name);
static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities);
static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name);
static void wlr_layer_surface_close(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1);
static void wlr_layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height);
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);

#include "config.h"

static const struct wl_registry_listener registry_listener = {
    .global = wl_registry_global_add,
    .global_remove = wl_registry_global_remove,
};

static const struct wl_output_listener output_listener = {
    .name = wl_output_name,
    .description = wl_output_description,
    .geometry = wl_output_geometry,
    .mode = wl_output_mode,
    .scale = wl_output_scale,
    .done = wl_output_done,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name = wl_seat_name,
};

static const struct wl_pointer_listener pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .frame = wl_pointer_frame,
    .button = wl_pointer_button,
    .motion = wl_pointer_motion,
    .axis = wl_pointer_axis,
    .axis_stop = wl_pointer_axis_stop,
    .axis_source = wl_pointer_axis_source,
    .axis_discrete = wl_pointer_axis_discrete,
    .axis_value120 = wl_pointer_axis_value120,
    .axis_relative_direction = wl_pointer_axis_relative_direction,
};

static const struct wl_touch_listener touch_listener = {
    .motion = wl_touch_motion,
    .up = wl_touch_up,
    .down = wl_touch_down,
    .frame = wl_touch_frame,
    .shape = wl_touch_shape,
    .cancel = wl_touch_cancel,
    .orientation = wl_touch_orientation,
};

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .closed = wlr_layer_surface_close,
    .configure = wlr_layer_surface_configure,
};

static const struct xdg_wm_base_listener base_listener = {
    .ping = xdg_wm_base_ping,
};

static const struct wl_callback_listener frame_callback_listener = {
    .done = wl_callback_frame_done,
};

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_event_loop *events;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *base;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct fcft_font *font;
static struct wl_list bars;
static struct wl_list seats;
static struct wl_array event_sources; // struct wl_event_source*
static struct wl_event_source *fifo_source;
static int32_t fifo_fd;
static char *fifo_path;
static bool running = false;

int32_t allocate_shm(size_t size) {
    char name[] = "wlclient_shm-XXXXXX";
    int32_t fd = -1, ret;

    for (int i = 0; i < 100; i++) {
        randomize_string(name + strlen(name) - 6);
        if ((fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600)) < 0 && errno != EEXIST) {
            panic("shm_open");
        };
    }

    shm_unlink(name);

    while (true) {
        ret = ftruncate(fd, size);
        if (ret == 0)
            break;
        if (ret < 0 && errno != EINTR) {
            close(fd);
            panic("ftruncate");
        }
    }

    return fd;
}

void randomize_string(char *str) {
    for (; *str; str++) *str = ('A' + (rand() % 25));
}

void bar_destroy(struct bar *bar) {
    if (!bar) return;

    if (bar->layer_surface) zwlr_layer_surface_v1_destroy(bar->layer_surface);
    if (bar->wl_surface) wl_surface_destroy(bar->wl_surface);
    if (bar->wl_output) wl_output_destroy(bar->wl_output);

    if (bar->memory_map.ptr) {
        munmap(bar->memory_map.ptr, bar->memory_map.size);
        for (int i = 0; i < BUFFER_AMNT; i++) {
            wl_buffer_destroy(bar->buffers[i].buffer);
        }
    }

    free(bar->layout);
    free(bar->title);
    free(bar->status);
    free(bar->output_name);
    free(bar);
}

void bar_determine_visibility(struct bar *bar, bool visibility, bool toggle) {
    if (!bar) return;

    if (visibility || (toggle && !bar->visible)) {
        bar_show(bar);
    }
    else if (!visibility || (toggle && bar->visible)) {
        bar_hide(bar);
    }
}

void bar_draw(struct bar *bar) {
    if (!bar) return;

    pixman_image_t *main_image = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, bar->buffers[bar->current_buffer].ptr, bar->width * 4);
    pixman_image_t *foreground = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);
    pixman_image_t *background = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);

    uint32_t x = 0, bottom_padding = ((bar->height + font->descent) / 2) + (bar->height / 6),
             tag, component_width;
    uint32_t boxs = font->height / 9;
    uint32_t boxw = font->height / 6 + 1;
    const char *tag_name,
          *title  = TITLE(bar->title),
          *status = STATUS(bar->status);
    bool urgent, occupied, viewed, has_focused;
    enum color_scheme scheme;
    const pixman_color_t *foreground_color, *background_color;

    /* draw tags */
    for (int i = 0; i < LENGTH(tags); i++) {
        tag_name = tags[i];
        tag = 1 << i;
        urgent = bar->urgent & tag;
        occupied = bar->occupied & tag;
        viewed = bar->tagset & tag;
        has_focused = bar->client_tagset & tag;

        scheme = inactive_scheme;
        if (viewed) scheme = active_scheme;
        if (urgent) scheme = urgent_scheme;

        foreground_color = &schemes[scheme][0];
        background_color = &schemes[scheme][1];

        component_width = TEXT_WIDTH(tag_name, -1) + font->height;

        if (occupied) {
            pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, foreground_color, 1, &(pixman_box32_t){
                    .x1 = x + boxs, .x2 = x + boxs + boxw,
                    .y1 = boxs, .y2 = boxs + boxw,
                    });

            if (!has_focused) {
                pixman_image_fill_boxes(PIXMAN_OP_CLEAR, foreground, foreground_color, 1, &(pixman_box32_t){
                        .x1 = x + boxs + 1, .x2 = x + boxs + boxw - 1,
                        .y1 = boxs + 1, .y2 = boxs + boxw - 1,
                        });
            }
        }

        draw_text_fg_bg(foreground, foreground_color, background, background_color, (enum fcft_subpixel)bar->subpixel, tag_name,
                font->height / 2, bottom_padding, -1, x, 0, component_width, bar->height);

        x += component_width;
    }

    /* draw layout */
    foreground_color = &schemes[inactive_scheme][0];
    background_color = &schemes[inactive_scheme][1];

    component_width = TEXT_WIDTH(bar->layout, -1) + font->height;
    draw_text_fg_bg(foreground, foreground_color, background, background_color, (enum fcft_subpixel)bar->subpixel, bar->layout,
            font->height / 2, bottom_padding, -1, x, 0, component_width, bar->height);
    x += component_width;

    /* draw title */
    scheme = bar->selected ? active_scheme : inactive_scheme;
    foreground_color = &schemes[scheme][0];
    background_color = &schemes[scheme][1];

    if (status_on_active && !bar->selected) {
        component_width = bar->width - x;
    }
    else {
        component_width = TEXT_WIDTH(title, -1) + font->height;
        component_width = bar->width - x - (TEXT_WIDTH(status, bar->width - x - component_width - font->height) + font->height);
    }

    draw_text_fg_bg(foreground, foreground_color, background, background_color, (enum fcft_subpixel)bar->subpixel, title,
            font->height / 2, bottom_padding, -1, x, 0, component_width, bar->height);

    if (bar->floating) {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, foreground_color, 1, &(pixman_box32_t){
                .x1 = x + boxs, .x2 = x + boxs + boxw,
                .y1 = boxs, .y2 = boxs + boxw,
                });

        pixman_image_fill_boxes(PIXMAN_OP_CLEAR, foreground, foreground_color, 1, &(pixman_box32_t){
                .x1 = x + boxs + 1, .x2 = x + boxs + boxw - 1,
                .y1 = boxs + 1, .y2 = boxs + boxw - 1,
                });
    }
    x += component_width;

    /* draw status */
    if (status_on_active && bar->selected) {
        foreground_color = &schemes[inactive_scheme][0];
        background_color = &schemes[inactive_scheme][1];

        component_width = TEXT_WIDTH(status, bar->width - x - font->height) + font->height;
        draw_text_fg_bg(foreground, foreground_color, background, background_color, (enum fcft_subpixel)bar->subpixel, status,
                font->height / 2, bottom_padding, bar->width - x - font->height, x, 0, component_width, bar->height);
        x += component_width;
    }

    pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, main_image, 0, 0, 0, 0, 0, 0, bar->width, bar->height);
    pixman_image_composite32(PIXMAN_OP_OVER, foreground, NULL, main_image, 0, 0, 0, 0, 0, 0, bar->width, bar->height);

    pixman_image_unref(foreground);
    pixman_image_unref(background);
    pixman_image_unref(main_image);

    wl_surface_attach(bar->wl_surface, bar->buffers[bar->current_buffer].buffer, 0, 0);
    wl_surface_damage_buffer(bar->wl_surface, 0, 0, bar->width, bar->height);
    wl_surface_commit(bar->wl_surface);

    // Flip buffer.
    bar->current_buffer = 1 - bar->current_buffer;
}

enum clicked bar_get_clicked(struct bar *bar, uint32_t *tag, uint32_t x) {
    if (!bar) return click_none;

    const char *title = TITLE(bar->title),
          *status = STATUS(bar->status);
    uint32_t bar_x = 0;

    for (int i = 0; i < LENGTH(tags); i++) {
        bar_x += TEXT_WIDTH(tags[i], -1) + font->height;
        if (x < bar_x) {
            *tag = i+1;
            return click_tag;
        }
    }

    bar_x += TEXT_WIDTH(bar->layout, -1) + font->height;
    if (x < bar_x) return click_layout;

    bar_x += (status_on_active && !bar->selected ?
            bar->width - bar_x :
            bar->width - bar_x - (TEXT_WIDTH(status, bar->width - bar_x - (TEXT_WIDTH(title, -1) + font->height) - font->height) + font->height));
    if (x < bar_x) return click_title;

    if (status_on_active && bar->selected) {
        bar_x += TEXT_WIDTH(status, bar->width - bar_x - font->height) + font->height;
        if (x < bar_x) return click_status;
    }

    return click_none;
}

void bar_show(struct bar *bar) {
    if (!bar || bar->visible) return;

    bar->wl_surface = wl_compositor_create_surface(compositor);
    wl_surface_set_buffer_scale(bar->wl_surface, bar->scale);

    bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, bar->wl_surface, bar->wl_output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "dwl-bar");
    if (zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layer_surface_listener, bar) == -1) panic("zwlr_layer_surface_v1_add_listener");

    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
            (bar_top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    uint32_t height = font->height+2;
    zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, height);
    zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface, height);
    wl_surface_commit(bar->wl_surface);
    bar->visible = true;
}

void bar_hide(struct bar *bar) {
    if (!bar | !bar->visible) return;

    zwlr_layer_surface_v1_destroy(bar->layer_surface);
    wl_surface_destroy(bar->wl_surface);
    bar->visible = false;
}

void bar_render(struct bar *bar) {
    if (!bar || !bar->visible || bar->invalid || !bar->wl_surface) return;

    struct wl_callback *frame_callback = wl_surface_frame(bar->wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, bar);
    wl_surface_commit(bar->wl_surface);
    bar->invalid = true;
}

void check_global(const char *name, void *data) {
    if (!data) panic("Compositor did not export: %s.", name);
}

void check_globals(void) {
    check_global("wl_compositor", compositor);
    check_global("wl_shm", shm);
    check_global("xdg_wm_base", base);
    check_global("zwlr_layer_shell_v1", layer_shell);
}

void cleanup(void) {
    close(fifo_fd);
    unlink(fifo_path);
    free(fifo_path);

    struct bar *bar, *bar_tmp;
    wl_list_for_each_safe(bar, bar_tmp, &bars, link) {
        wl_list_remove(&bar->link);
        bar_destroy(bar);
    }

    struct seat *seat, *seat_tmp;
    wl_list_for_each_safe(seat, seat_tmp, &seats, link) {
        wl_list_remove(&seat->link);
        seat_destroy(seat);
    }

    for (int i = 0; i < WL_ARRAY_LENGTH(&event_sources, struct wl_event_source*); i++) {
        struct wl_event_source *source = WL_ARRAY_AT(&event_sources, struct wl_event_source*, i);
        if (!source) continue;
        wl_event_source_remove(source);
    }
    wl_array_release(&event_sources);

    fcft_destroy(font);
    fcft_fini();
    wl_event_loop_destroy(events);
    wl_compositor_destroy(compositor);
    wl_shm_destroy(shm);
    zwlr_layer_shell_v1_destroy(layer_shell);
    xdg_wm_base_destroy(base);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
}

int display_in(int fd, uint32_t mask, void *data) {
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR) || wl_display_dispatch(display) == -1) running = false;
    return 0;
}


/*
 * I don't know how I feel about this function.
 * It was either wrap a function that just draws text,
 * or create a function that draws background and text.
 * The latter feels too coupled in a sense. But the former feels wrong for some reason,
 * like maybe it's too much work for something simple? Or just a little annoying? I'm not sure
 * I just didn't like it.
 * Also the latter felt a bit better because if you don't want to render the
 * background of the text then don't provide background and background_color.
 * Another problem I see is that this function does too much. I don't know.
 *
 * \param foreground_color is just text color.
 *
 * \param background if not provided then we don't render to background.
 *
 * \param background_color if not provided then we don't render to background.
 *
 * \param left_padding the padding to the left of the text.
 * x + left_padding is where the text will first be drawn.
 *
 * \param bottom_padding the padding to the bottom of the text.
 * y + bottom_padding is where the text will first be drawn.
 *
 * \return new x position based on provided x. To get text width do draw_text() - x.
 */
uint32_t draw_text_fg_bg(pixman_image_t *foreground, const pixman_color_t *foreground_color, pixman_image_t *background, const pixman_color_t *background_color, enum fcft_subpixel subpixel, const char *text,
        uint32_t left_padding, uint32_t bottom_padding, int32_t max_text_length, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!text) return 0;

    uint32_t tx = x + left_padding,
             ty = y + bottom_padding,
             state = UTF8_ACCEPT, codepoint, prev_codepoint = 0;
    int64_t kern;
    const struct fcft_glyph *glyph;
    pixman_image_t *text_image = foreground_color ? pixman_image_create_solid_fill(foreground_color) : NULL;
    bool draw_foreground = foreground && foreground_color,
         draw_background = background && background_color;

    for (; *text; text++) {
        if (utf8decode(&state, &codepoint, *text) == UTF8_REJECT) continue;


        glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE);
        if (!glyph) continue;

        kern = 0;
        if (prev_codepoint) fcft_kerning(font, prev_codepoint, codepoint, &kern, NULL);

        if (max_text_length != -1 && tx + kern + glyph->advance.x >  x + left_padding + max_text_length) break;

        if (draw_foreground) {
            if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
                pixman_image_composite32(PIXMAN_OP_OVER, glyph->pix, text_image, foreground,
                        0, 0, 0, 0,
                        tx + glyph->x, ty - glyph->y,
                        glyph->width, glyph->height);
            }
            else {
                pixman_image_composite32(PIXMAN_OP_OVER, text_image, glyph->pix, foreground,
                        0, 0, 0, 0,
                        tx + glyph->x, ty - glyph->y,
                        glyph->width, glyph->height);
            }
        }


        tx += kern + glyph->advance.x;
        prev_codepoint = codepoint;
    }

    if (draw_background) {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, background, background_color, 1, &(pixman_box32_t){
                .x1 = x, .x2 = x + width,
                .y1 = y, .y2 = y + height,
                });
    }

    if (text_image) pixman_image_unref(text_image);

    return tx;
}

int fifo_in(int fd, uint32_t mask, void *data) {
    struct bar *bar = NULL;

    if (mask & WL_EVENT_ERROR) {
        // REVIEW: This may break things
        wl_event_source_remove(fifo_source);
        close(fifo_fd);
        fifo_fd = -1;

        wl_list_for_each(bar, &bars, link) {
            bar->status = NULL;
        }

        return 0;
    }

    FILE *fifo_file = fdopen(dup(fd), "r");
    size_t size = 0;
    char *line = NULL, *output_name, *command, *value;
    while (true) {
        if (getline(&line, &size, fifo_file) == -1) break;

        command = strtok(line, " ");
        if (!command) continue;

        if (STRING_EQUAL(command, "monitor")) {
            output_name = strtok(NULL, "\n");
            if (!output_name) continue;

            wl_list_for_each(bar, &bars, link) {
                if (STRING_EQUAL(output_name, bar->output_name)) break;
            }
            // Make sure we have the desired output after the loop.
            if (!STRING_EQUAL(output_name, bar->output_name) && !STRING_EQUAL(output_name, "selected")) {
                bar = NULL;
                continue;
            }
            if (STRING_EQUAL(output_name, "selected")) {
                wl_list_for_each(bar, &bars, link) {
                    if (bar->selected) break;
                }
            }
        }
        else if (STRING_EQUAL(command, "visibility")) {
            value = strtok(NULL, "\n");
            if (!value) continue;

            bool visibility = false, toggle = false;
            if (STRING_EQUAL(value, "toggle")) {
                toggle = true;
            }
            else if (STRING_EQUAL(value, "on")) {
                visibility = true;
            }
            else if (STRING_EQUAL(value, "off")) {
                visibility = false;
            }
            else {
                continue;
            }

            if (!bar) {
                wl_list_for_each(bar, &bars, link) bar_determine_visibility(bar, visibility, toggle);
                bar = NULL;
            }
            else {
                bar_determine_visibility(bar, visibility, toggle);
            }
        }
        else if (STRING_EQUAL(command, "status")) {
            value = strtok(NULL, "\n");
            if (!value) continue;

            if (!bar) {
                wl_list_for_each(bar, &bars, link) {
                    free(bar->status);
                    bar->status = strdup(value);
                }
                bar = NULL;
            }
            else {
                free(bar->status);
                bar->status = strdup(value);
            }
        }
    }
    free(line);
    fclose(fifo_file);

    wl_list_for_each(bar, &bars, link) {
        bar_render(bar);
    }

    return 0;
}

void fifo_setup(void) {
    size_t len;
    const char *runtime_path = getenv("XDG_RUNTIME_DIR");
    if (!runtime_path) panic("runtime_path");

    for (int i = 0; i < 100; i++) {
        len = snprintf(NULL, 0, "%s/test-dwl-bar-%d", runtime_path, i) + 1;
        fifo_path = ecalloc(1, len);
        snprintf(fifo_path, len, "%s/test-dwl-bar-%d", runtime_path, i);

        if (mkfifo(fifo_path, 0666) == -1) {
            if (errno != EEXIST) panic("mkfifo");
            free(fifo_path);
            continue;
        }

        if ((fifo_fd = open(fifo_path, O_CLOEXEC | O_RDWR | O_NONBLOCK)) == -1) panic("fifo_fd == -1");

        return;
    }

    panic("fifo_setup");
}

/*
 * Is passing in void* a good idea for the possible values in this? Or could this be a potential
 * burden in the future?
 */
void process_bindings(struct bar *bar, uint32_t x, uint32_t y, uint32_t button, void *value) {
    if (!bar) return;

    const struct binding *binding;
    bool arg_modified = false;
    union arg arg;
    enum clicked clicked;
    uint32_t tag = 0;

    // We do this because we just want to run through a set of getters for enum clicked
    // And really only care that one of them doesn't return click_none.
    if ((clicked = bar_get_clicked(bar, &tag, x)) != click_none) {/*Left blank on purpose*/}
    else {return;}

    if (button == scroll_up || button == scroll_down ||
            button == scroll_right || button == scroll_left) {
        arg.i32 = value ? *(int32_t*)value : -1;
        arg_modified = true;
    }

    for (int i = 0; i < LENGTH(bindings); i++) {
        binding = &bindings[i];
        if (binding->button != button || !(binding->clicked & clicked)) continue;

        binding->click(bar, (arg_modified && !binding->bypass ? &arg : &binding->arg));

        return;
    }
}

/*
 * I don't even really know if any of this(wl_touch) works as I don't have a touch device.
 */
void process_touch_point(struct touch_point *point) {
    if (!point) return;

    int32_t x = wl_fixed_to_int(point->x);
    int32_t y = wl_fixed_to_int(point->y);
    int32_t start_x = wl_fixed_to_int(point->start_x);
    int32_t start_y = wl_fixed_to_int(point->start_y);

    /* "progress" is a measure from 0..100 representing the fraction of the
     * output the touch gesture has travelled, positive when moving to the right
     * and negative when moving to the left.
     * In this case we translate progress to a scroll event. So that touch devices
     * can in some sense mimic scrolling.
     */
    int32_t y_progress = ((y - start_y) / (point->selected_bar->height / point->selected_bar->scale) * 100);
    int32_t x_progress = ((x - start_x) / (point->selected_bar->width / point->selected_bar->scale) * 100);
    int32_t scroll_value;
    uint32_t button;

    if (abs(y_progress) > 20) {
        scroll_value = abs(y_progress);
        process_bindings(point->selected_bar, start_x, start_y, y_progress < 0 ? scroll_up : scroll_down , &scroll_value);
    }
    else if (abs(x_progress) > 20) {
        scroll_value = abs(x_progress);
        process_bindings(point->selected_bar, start_x, start_y, x_progress < 0 ? scroll_left : scroll_right , &scroll_value);
    }

    if (point->time < 500) {
        button = BTN_LEFT;
    }
    else if (point->time < 1000) {
        button = BTN_RIGHT;
    }
    else {
        button = BTN_MIDDLE;
    }

    process_bindings(point->selected_bar, start_x, start_y, button, NULL);
}

void seat_destroy(struct seat *seat) {
    if (!seat) return;

    struct pointer *pointer;
    struct touch *touch;

    if (seat->touch) {
        touch = seat->touch;
        wl_touch_destroy(touch->wl_touch);
        free(touch);
    }

    if (seat->pointer) {
        pointer = seat->pointer;
        wl_surface_destroy(pointer->cursor_surface);
        wl_cursor_theme_destroy(pointer->cursor_theme);
        wl_pointer_destroy(pointer->wl_pointer);
        free(pointer);
    }

    wl_seat_destroy(seat->wl_seat);
    free(seat);
}

int signal_handler(int signal_number, void *data) {
    panic("SIGTERM, SIGINT or SIGHUP");
    return 0;
}

int stdin_in(int fd, uint32_t mask, void *data) {
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) panic("stdin_in (WL_EVENT_HANGUP | WL_EVENT_ERROR)");

    FILE *stdin_file = fdopen(dup(fd), "r");
    size_t size = 0;
    char *line = NULL,
         *output_name, *command,
         *value, *occupied, *tagset, *client, *urgent;
    struct bar *bar;
    while (true) {
        if (getline(&line, &size, stdin_file) == -1) break;

        output_name = strtok(line, " ");
        command = strtok(NULL, " ");

        wl_list_for_each(bar, &bars, link) {
            if (STRING_EQUAL(output_name, bar->output_name)) break;
        }
        // Make sure we have the desired output after the loop.
        if (!STRING_EQUAL(output_name, bar->output_name)) continue;

        if (STRING_EQUAL(command, "title")) {
            value = strtok(NULL, "\n");
            free(bar->title);
            bar->title = value ? strdup(value) : NULL;
        }
        else if (STRING_EQUAL(command, "floating")) {
            value = strtok(NULL, "\n");
            value = value ? value : "0";
            bar->floating = STRING_EQUAL(value, "1");
        }
        else if (STRING_EQUAL(command, "selmon")) {
            value = strtok(NULL, "\n");
            bar->selected = STRING_EQUAL(value, "1");
        }
        else if (STRING_EQUAL(command, "layout")) {
            value = strtok(NULL, "\n");
            free(bar->layout);
            bar->layout = strdup(value);
        }
        else if (STRING_EQUAL(command, "tags")) {
            occupied = strtok(NULL, " ");
            tagset = strtok(NULL, " ");
            client = strtok(NULL, " ");
            urgent = strtok(NULL, "\n");

            bar->tagset = strtoul(tagset, NULL, 0);
            bar->urgent = strtoul(urgent, NULL, 0);
            bar->occupied = strtoul(occupied, NULL, 0);
            bar->client_tagset = strtoul(client, NULL, 0);
        }
    }
    free(line);
    fclose(stdin_file);

    wl_list_for_each(bar, &bars, link) {
        bar_render(bar);
    }

    return 0;
}

void spawn(struct bar *bar, const union arg *arg) {
    char **prog = (char**)arg->v;

    if (fork() != 0) return;

    setsid();
    execvp(prog[0], prog);
    panic("execvp failed: '%s'", prog[0]);
}

struct touch_point *touch_find_point(struct touch *touch, int32_t id) {
    if (!touch) return NULL;

    for (int i = 0; i < LENGTH(touch->touch_points); i++) {
        struct touch_point *point = &touch->touch_points[i];
        if (id != point->id) continue;

        return point;
    }

    return NULL;
}

void wl_callback_frame_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data) {
    struct bar *bar = data;

    bar_draw(bar);
    bar->invalid = false;
    wl_callback_destroy(wl_callback);
}

void wl_output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform) {
    struct bar *bar = data;
    bar->subpixel = subpixel;
}

void wl_output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {/*Noop*/}

void wl_output_done(void *data, struct wl_output *wl_output) {/*Noop*/}

void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    struct bar *bar = data;
    bar->scale = factor;
}

void wl_output_name(void *data, struct wl_output *wl_output, const char *name) {
    struct bar *bar = data;
    bar->output_name = strdup(name);
}

void wl_output_description(void *data, struct wl_output *wl_output, const char *description) {/*Noop*/}

void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct pointer *pointer = data;
    struct bar *bar;
    wl_list_for_each(bar, &bars, link) {
        if (bar->wl_surface != surface) continue;
        pointer->selected_bar = bar;
    }
    if (!pointer->selected_bar) panic("wl_pointer_enter: we couldn't find a bar for the given wl_surface, something is seriously fucked.");

    if (!pointer->cursor_surface) {
        pointer->cursor_theme = wl_cursor_theme_load(NULL, 24, shm);
        pointer->cursor_image = wl_cursor_theme_get_cursor(pointer->cursor_theme, "left_ptr")->images[0];
        pointer->cursor_surface = wl_compositor_create_surface(compositor);
        wl_surface_attach(pointer->cursor_surface, wl_cursor_image_get_buffer(pointer->cursor_image), 0, 0);
        wl_surface_commit(pointer->cursor_surface);
    }

    wl_pointer_set_cursor(pointer->wl_pointer, serial, pointer->cursor_surface, pointer->cursor_image->hotspot_x, pointer->cursor_image->hotspot_y);
}

void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
    struct pointer *pointer = data;
    pointer->selected_bar = NULL;
}

void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct pointer *pointer = data;

    pointer->x = wl_fixed_to_int(surface_x);
    pointer->y = wl_fixed_to_int(surface_y);
}

void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    struct pointer *pointer = data;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        process_bindings(pointer->selected_bar, pointer->x, pointer->y, button, NULL);
    }
}

void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    struct pointer *pointer = data;
    if (pointer->axis[axis].discrete) return;

    pointer->axis[axis].scroll_value += value;
}

void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
    struct pointer *pointer = data;

    for (int axis = 0; axis < 2; axis++) {
        if (!pointer->axis[axis].scroll_value) continue;

        // This could probably create conflicts and problems, should find a better way to digest this stuff.
        // We don't really use the value but to check for direction. Although in the future we might also want to
        // use the value itself.
        enum scroll scroll;
        int32_t value = wl_fixed_to_int(pointer->axis[axis].scroll_value);
        value = pointer->axis[axis].discrete ? value / 120 : value;
        bool negative_value =  value < 0;

        switch (axis) {
            case WL_POINTER_AXIS_VERTICAL_SCROLL:
                scroll = negative_value ? scroll_up : scroll_down;
                break;
            case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
                scroll = negative_value ? scroll_left : scroll_right;
                break;
        }

        process_bindings(pointer->selected_bar, pointer->x, pointer->y, scroll, &value);

        pointer->axis[axis].discrete = false;
        pointer->axis[axis].scroll_value = 0;
    }
}

void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {/*Noop*/}

void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {/*Noop*/}

void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {/*Noop*/}

void wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
    struct pointer *pointer = data;
    if (!pointer->axis[axis].discrete) {
        pointer->axis[axis].discrete = true;
        pointer->axis[axis].scroll_value = 0;
    }
    pointer->axis[axis].scroll_value += value120;
}

void wl_pointer_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {/*Noop*/}

void wl_touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    struct touch *touch = data;
    struct touch_point *point;

    for (int i = 0; i < LENGTH(touch->touch_points); i++) {
        point = &touch->touch_points[i];
        if (point->id == -1) continue;

        point->id = id;
        point->start_x = x;
        point->start_y = y;
        point->x = 0;
        point->y = 0;
        point->time = time;
        wl_list_for_each(point->selected_bar, &bars, link) {
            if (point->selected_bar->wl_surface == surface) break;
        }
        if (point->selected_bar->wl_surface != surface) point->id = -1;
    }
}

void wl_touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, int32_t id) {
    struct touch *touch = data;
    struct touch_point *point = touch_find_point(touch, id);
    if (!point) return;

    process_touch_point(point);

    point->id = -1;
}

void wl_touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    struct touch *touch = data;
    struct touch_point *point = touch_find_point(touch, id);
    if (!point) return;

    point->time = time;
    point->x = x;
    point->y = y;
}

void wl_touch_frame(void *data, struct wl_touch *wl_touch) {/*Noop*/}

void wl_touch_cancel(void *data, struct wl_touch *wl_touch) {
    struct touch *touch = data;
    struct touch_point *point;
    for (int i = 0; i < LENGTH(touch->touch_points); i++) {
        point = &touch->touch_points[i];
        point->id = -1;
    }
}

void wl_touch_shape(void *data, struct wl_touch *wl_touch, int32_t id, wl_fixed_t major, wl_fixed_t minor) {/*Noop*/}

void wl_touch_orientation(void *data, struct wl_touch *wl_touch, int32_t id, wl_fixed_t orientation) {/*Noop*/}

void wl_registry_global_add(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version) {
    if (STRING_EQUAL(wl_output_interface.name, interface)) {
        struct bar *bar = ecalloc(1, sizeof(*bar));
        bar->wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        bar->wl_output_name = name;
        bar->scale = 1;
        bar->tagset = 1;
        if (wl_output_add_listener(bar->wl_output, &output_listener, bar) == -1) panic("wl_output_add_listener");
        wl_list_insert(&bars, &bar->link);

        if (running) {
            bar_show(bar);
        }
    }
    else if (STRING_EQUAL(wl_compositor_interface.name, interface)) {
        compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 5);
    }
    else if (STRING_EQUAL(wl_shm_interface.name, interface)) {
        shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    }
    else if (STRING_EQUAL(zwlr_layer_shell_v1_interface.name, interface)) {
        layer_shell = wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 4);
    }
    else if (STRING_EQUAL(xdg_wm_base_interface.name, interface)) {
        base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 2);
        if (xdg_wm_base_add_listener(base, &base_listener, NULL) == -1) panic("xdg_wm_base_add_listener");
    }
    else if (STRING_EQUAL(wl_seat_interface.name, interface)) {
        struct seat *seat = ecalloc(1, sizeof(*seat));
        seat->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 8);
        seat->wl_seat_name = name;

        if (wl_seat_add_listener(seat->wl_seat, &seat_listener, seat) == -1) panic("wl_seat_add_listener");

        wl_list_insert(&seats, &seat->link);
    }
}

void wl_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {
    struct bar *bar, *bar_tmp;
    wl_list_for_each_safe(bar, bar_tmp, &bars, link) {
        if (bar->wl_output_name != name) continue;
        bar_destroy(bar);
    }

    struct seat *seat, *seat_tmp;
    wl_list_for_each_safe(seat, seat_tmp, &seats, link) {
        if (seat->wl_seat_name != name) continue;
        seat_destroy(seat);
    }
}

void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    struct seat *seat = data;

    if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
        struct touch *touch = ecalloc(1, sizeof(*touch));
        touch->wl_touch = wl_seat_get_touch(wl_seat);
        seat->touch = touch;

        for (int i = 0; i < LENGTH(touch->touch_points); i++) {
            touch->touch_points[i].id = -1;
        }
        if (wl_touch_add_listener(touch->wl_touch, &touch_listener, touch) == -1) panic("wl_touch_add_listener");
    }
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        struct pointer *pointer = ecalloc(1, sizeof(*pointer));
        pointer->wl_pointer = wl_seat_get_pointer(wl_seat);

        if (wl_pointer_add_listener(pointer->wl_pointer, &pointer_listener, pointer) == -1) panic("wl_pointer_add_listener");
    }
}

void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {/*Noop*/}

void wlr_layer_surface_close(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
    running = false;
}

void wlr_layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height) {
    struct bar *bar = data;

    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);

    width *= bar->scale;
    height *= bar->scale;

    if (bar->height != height || bar->width != width || !bar->memory_map.ptr) {
        bar->width = width;
        bar->height = height;

        if (bar->memory_map.ptr) {
            munmap(bar->memory_map.ptr, bar->memory_map.size);
            for (int i = 0; i < BUFFER_AMNT; i++) {
                wl_buffer_destroy(bar->buffers[i].buffer);
            }
        }

        uint32_t offset,
                 stride = width * 4,
                 size = height * stride,
                 total = size * BUFFER_AMNT;
        int fd = allocate_shm(total);

        bar->memory_map.ptr = mmap(NULL, total, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
        bar->memory_map.size = total;

        if (!bar->memory_map.ptr || bar->memory_map.ptr == MAP_FAILED) {
            close(fd);
            panic("mmap");
        }

        struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, total);
        for (int i = 0; i < BUFFER_AMNT; i++) {
            offset = size*i;
            bar->buffers[i].buffer = wl_shm_pool_create_buffer(pool, offset, bar->width, bar->height, stride, WL_SHM_FORMAT_ARGB8888);
            bar->buffers[i].ptr = bar->memory_map.ptr + offset;
        }
        close(fd);
        wl_shm_pool_destroy(pool);
    }

    bar_draw(bar);
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

int main(int argc, char **argv) {
    display = wl_display_connect("wayland-1");
    if (!display) panic("wl_display_connect");

    registry = wl_display_get_registry(display);
    if (!registry) panic("wl_display_get_registry");

    fifo_setup();

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == -1) panic("STDIN_FILENO O_NONBLOCK");

    if (!fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_ERROR)) panic("fcft_init");
    if (!fcft_set_scaling_filter(FCFT_SCALING_FILTER_LANCZOS3)) panic("fcft_set_scaling_filter");
    font = fcft_from_name(LENGTH(fonts), fonts, NULL);
    if (!font) panic("fcft_from_name");

    wl_list_init(&bars);
    wl_list_init(&seats);

    if (wl_registry_add_listener(registry, &registry_listener, NULL) == -1) panic("wl_registry_add_listener");

    wl_display_roundtrip(display);

    check_globals();

    wl_display_roundtrip(display);

    struct bar *bar;
    wl_list_for_each(bar, &bars, link) {
        bar_show(bar);
    }

    events = wl_event_loop_create();
    wl_array_init(&event_sources);

    fifo_source = wl_event_loop_add_fd(events, fifo_fd, WL_EVENT_READABLE, fifo_in, NULL);

    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGTERM, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGINT, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGHUP, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_fd(events, wl_display_get_fd(display), WL_EVENT_READABLE, display_in, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_fd(events, STDIN_FILENO, WL_EVENT_READABLE, stdin_in, NULL));
    WL_ARRAY_ADD(&event_sources, fifo_source);

    running = true;

    while (running) {
        wl_display_dispatch_pending(display);
        if (wl_display_flush(display) == -1 && errno != EAGAIN) break;

        if (wl_event_loop_dispatch(events, -1) == -1) running = false;
    }

    cleanup();
    return EXIT_SUCCESS;
}

void panic(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[dwl-bar] panic: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);

    cleanup();
    exit(EXIT_FAILURE);
}

void *ecalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr) panic("Failed to allocate");
    return ptr;
}
