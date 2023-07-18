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
#include <linux/input-event-codes.h>
#include <wayland-util.h>
#include <pixman-1/pixman.h>
#include <fcft/fcft.h>
#include "uft8.h"
#include "log.h"
#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

#define BUFFER_AMNT 2
#define STRING_EQUAL(string1, string2) (strcmp(string1, string2) == 0)
#define LENGTH(X) (sizeof X / sizeof X[0] )
#define TEXT_WIDTH(text) (draw_text(NULL, NULL, 0, text, 0, 0))
#define WL_ARRAY_LENGTH(array, type) ((array)->size/sizeof(type*))
#define WL_ARRAY_AT(array, type, index) (*((type*)((array)->data)+index))
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
    click_none = 1 << 0,
    click_tag = 1 << 1,
    click_layout = 1 << 2,
    click_title = 1 << 3,
    click_status = 1 << 4,
};

union arg {
    uint32_t ui;
    int32_t i;
    const void *v;
};

struct binding {
    enum clicked clicked;
    int button;
    void (*click)(struct bar *bar, const union arg *arg);
    bool bypass; /* Informs the click function that they should only pass the defined arg in this binding */
    const union arg arg;
};


struct seat {
    struct wl_seat *wl_seat;
    uint32_t wl_seat_name;

    struct {
        // TODO: Fill this.
    } pointer;

    struct {
        // TODO: Fill this.
    } touch;

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
         *status;

    uint32_t width, height, wl_output_name,
             tagset, occupied, urgent, client_tagset;
    uint8_t current_buffer;
    int32_t scale;
    enum wl_output_subpixel subpixel; // At the moment wl_output_subpixel and fcft_subpixel
                                      // directly map to eachother but this is risky,
                                      // because if something in either lib changes we break.
    bool selected, // The output selected
         floating, // The selected client floating
         visible,
         invalid; // false if bar's state is reflected in render,
                  // true if it isn't and we need to render again.

    struct wl_list link;
};

static int32_t allocate_shm(size_t size);
static void randomize_string(char *str);
static void bar_destroy(struct bar *bar);
static void bar_draw(struct bar *bar);
static void bar_show(struct bar *bar);
static void bar_hide(struct bar *bar);
static void bar_render(struct bar *bar);
static void check_global(const char *name, void *data);
static void check_globals(void);
static void cleanup(void);
static int display_in(int fd, uint32_t mask, void *data);
static uint32_t draw_text(pixman_image_t *image, const pixman_color_t *color, enum fcft_subpixel subpixel, const char *text, uint32_t x, uint32_t y);
static void *ecalloc(size_t nmemb, size_t size);
static void panic(const char *fmt, ...);
static struct seat *seat_create(struct wl_seat *wl_seat, uint32_t wl_seat_name);
static void seat_destroy(struct seat *seat);
static int signal_handler(int signal_number, void *data);
static int stdin_in(int fd, uint32_t mask, void *data);
static void spawn(struct bar *bar, const union arg *arg);
static void wl_callback_frame_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data);
static void wl_output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform);
static void wl_output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
static void wl_output_done(void *data, struct wl_output *wl_output);
static void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor);
static void wl_output_name(void *data, struct wl_output *wl_output, const char *name);
static void wl_output_description(void *data, struct wl_output *wl_output, const char *description);
static void wl_registry_global_add(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version);
static void wl_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name);
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
static struct wl_array event_sources; // struct wl_event_source**
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

void bar_draw(struct bar *bar) {
    if (!bar) return;

    wlc_logln(LOG_DEBUG, "start bar_draw");

    pixman_image_t *main_image = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, bar->buffers[bar->current_buffer].ptr, bar->width * 4);
    pixman_image_t *foreground = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);
    pixman_image_t *background = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, NULL, bar->width * 4);

    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &cyan, 1, &(pixman_box32_t){
            .x1 = 0, .y1 = 0,
            .x2 = bar->width, .y2 = bar->height,
            });

    pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, main_image, 0, 0, 0, 0, 0, 0, bar->width, bar->height);

    goto done;

    uint32_t x = 0,
             tag, component_width;
    uint32_t boxs = font->height / 9;
    uint32_t boxw = font->height / 6 + 1;
    const char *tag_name;
    bool urgent, occupied, viewed, has_focused;
    enum color_scheme scheme;
    const pixman_color_t *foreground_color, *background_color;

    /* draw tags */
    for (int i = 0; i < LENGTH(tags); i++) {
        wlc_logln(LOG_DEBUG, "%d", x);

        tag_name = tags[i];
        tag = 1 << i;
        urgent = bar->urgent & tag;
        occupied = bar->occupied & tag;
        viewed = bar->tagset & tag;
        has_focused = bar->client_tagset & tag;

        wlc_logln(LOG_DEBUG, "%c %d %d %d %d %d", *tag_name, tag, urgent, occupied, viewed, has_focused);

        scheme = inactive_scheme;
        if (viewed) scheme = active_scheme;
        if (urgent) scheme = urgent_scheme;

        foreground_color = &schemes[scheme][0];
        background_color = &schemes[scheme][1];

        component_width = TEXT_WIDTH(tag_name) + font->height;

        wlc_logln(LOG_DEBUG, "%d", component_width);

        pixman_image_fill_boxes(PIXMAN_OP_SRC, background, background_color, 1, &(pixman_box32_t){
                .x1 = x, .x2 = x + component_width,
                .y1 = 1, .y2 = bar->height,
                });

        if (occupied) {
            pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, foreground_color, 1, &(pixman_box32_t){
                    .x1 = x + boxs, .x2 = x + boxs + boxw,
                    .y1 = boxs, .y2 = boxs + boxw,
                    });

            if (!has_focused) {
                pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground, &(pixman_color_t){0}, 1, &(pixman_box32_t){
                        .x1 = x + boxs + 1, .x2 = x + boxs + boxw - 1,
                        .y1 = boxs + 1, .y2 = boxs + boxw - 1,
                        });
            }
        }

        draw_text(foreground, foreground_color, (enum fcft_subpixel)bar->subpixel, tag_name, x + font->height/2, 1);

        x += component_width;
        wlc_logln(LOG_DEBUG, "%d", x);

    }

    pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, main_image, 0, 0, 0, 0, 0, 0, bar->width, bar->height);
    pixman_image_composite32(PIXMAN_OP_OVER, foreground, NULL, main_image, 0, 0, 0, 0, 0, 0, bar->width, bar->height);

done:

    wl_surface_attach(bar->wl_surface, bar->buffers[bar->current_buffer].buffer, 0, 0);
    wl_surface_damage_buffer(bar->wl_surface, 0, 0, bar->width, bar->height);
    wl_surface_commit(bar->wl_surface);

    pixman_image_unref(background);
    pixman_image_unref(foreground);
    pixman_image_unref(main_image);

    // Flip buffer.
    bar->current_buffer = 1 - bar->current_buffer;

    wlc_logln(LOG_DEBUG, "end bar_draw");
}

void bar_show(struct bar *bar) {
    if (!bar || bar->visible) return;

    if (!bar->wl_surface) {
        bar->wl_surface = wl_compositor_create_surface(compositor);
        wl_surface_set_buffer_scale(bar->wl_surface, bar->scale);
    }

    if (!bar->layer_surface) {
        bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, bar->wl_surface, bar->wl_output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "dwl-bar");
        if (zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layer_surface_listener, bar) == -1) panic("zwlr_layer_surface_v1_add_listener");
    }

    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
            (bar_top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
            | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    uint32_t height = font->height+2;
    zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, height);
    zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface, height);
    wl_surface_commit(bar->wl_surface);
    bar->visible = true;
    wlc_logln(LOG_DEBUG, "bar_show");

}

void bar_hide(struct bar *bar) {
    if (!bar | !bar->visible) return;

    wl_surface_attach(bar->wl_surface, NULL, 0, 0);
    wl_surface_commit(bar->wl_surface);
    bar->visible = false;
}

void bar_render(struct bar *bar) {
    if (!bar || !bar->visible || bar->invalid) return;

    wlc_logln(LOG_DEBUG, "bar_render start");
    struct wl_callback *frame_callback = wl_surface_frame(bar->wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, bar);
    wl_surface_commit(bar->wl_surface);
    bar->invalid = true;
    wlc_logln(LOG_DEBUG, "bar_render end");
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
        wl_event_source_remove(source);
    }
    wl_array_release(&event_sources);

    fcft_destroy(font);
    fcft_fini();
    wl_event_loop_destroy(events);
    wl_compositor_destroy(compositor);
    wl_shm_destroy(shm);
    zwlr_layer_shell_v1_destroy(layer_shell);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
}

int display_in(int fd, uint32_t mask, void *data) {
    wlc_logln(LOG_DEBUG, "display_in");
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR) || wl_display_dispatch(display) == -1) running = false;
    return 0;
}

/*
 * \return new x position based on provided x. To get text width do draw_text() - x.
 */
uint32_t draw_text(pixman_image_t *image, const pixman_color_t *color, enum fcft_subpixel subpixel, const char *text, uint32_t x, uint32_t y) {
    if (!text) return 0;

    uint32_t new_x = x, state = UTF8_ACCEPT, codepoint, prev_codepoint = 0;
    int64_t kern;
    const struct fcft_glyph *glyph;
    pixman_image_t *color_image = NULL;
    if (color) {
        pixman_image_create_solid_fill(color);
    }

    for (; *text; text++) {
        if (utf8decode(&state, &codepoint, *text) == UTF8_REJECT) continue;

        glyph = fcft_rasterize_char_utf32(font, codepoint, subpixel);
        if (!glyph) continue;

        kern = 0;
        if (prev_codepoint && !fcft_kerning(font, prev_codepoint, codepoint, &kern, NULL)) continue;

        new_x += kern + glyph->advance.x;

        if (!image || !color_image) goto done;

        if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
            pixman_image_composite32(PIXMAN_OP_OVER, glyph->pix, color_image, image,
                    0, 0, 0, 0,
                    x + glyph->x, y + glyph->y,
                    glyph->width, glyph->height);
        }
        else {
            pixman_image_composite32(PIXMAN_OP_OVER, color_image, glyph->pix, image,
                    0, 0, 0, 0,
                    x + glyph->x, y + glyph->y,
                    glyph->width, glyph->height);
        }

done:
        x = new_x;
    }

    if (color_image) pixman_image_unref(color_image);

    return new_x;
}

struct seat *seat_create(struct wl_seat *wl_seat, uint32_t wl_seat_name) {
    if (!wl_seat) return NULL;
    // TODO: Create seat and get it into working order.
    return NULL;
}

void seat_destroy(struct seat *seat) {
    if (!seat) return;
    // TODO: Destroy seat.
}

int signal_handler(int signal_number, void *data) {
    panic("SIGTERM, SIGINT or SIGHUP");
    return 0;
}

int stdin_in(int fd, uint32_t mask, void *data) {
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) panic("stdin_in (WL_EVENT_HANGUP | WL_EVENT_ERROR)");

    wlc_logln(LOG_DEBUG, "start stdin_in");

    FILE *stdin_file = fdopen(dup(fd), "r");
    size_t size = 0;
    char *line = NULL,
         *output_name, *command,
         *value, *occupied, *tagset, *client, *urgent;
    struct bar *bar;
    while (true) {
        if (getline(&line, &size, stdin_file) == -1) break;

        wlc_logln(LOG_DEBUG, "line: '%s'", line);

        output_name = strtok(line, " ");
        command = strtok(NULL, " ");

        wl_list_for_each(bar, &bars, link) {
            if (STRING_EQUAL(output_name, bar->output_name)) break;
        }
        // If we get out of the loop and we don't have the desired output.
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

    wlc_logln(LOG_DEBUG, "end stdin_in");

    return 0;
}

void spawn(struct bar *bar, const union arg *arg) {
    char **prog = (char**)arg->v;

    if (fork() != 0) return;

    setsid();
    execvp(prog[0], prog);
    panic("execvp failed: '%s'", prog[0]);
}

void wl_callback_frame_done(void *data, struct wl_callback *wl_callback, uint32_t callback_data) {
    struct bar *bar = data;

    wlc_logln(LOG_DEBUG, "wl_callback_frame_done start");
    bar_draw(bar);
    bar->invalid = false;
    wl_callback_destroy(wl_callback);
    wlc_logln(LOG_DEBUG, "wl_callback_frame_done end");
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

void wl_registry_global_add(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version) {
    if (STRING_EQUAL(wl_output_interface.name, interface)) {
        struct bar *bar = ecalloc(1, sizeof(*bar));
        bar->wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        bar->wl_output_name = name;
        bar->scale = 1;
        bar->invalid = false;
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

void wlr_layer_surface_close(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
    running = false;
}

void wlr_layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height) {
    struct bar *bar = data;

    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);

    width *= bar->scale;
    height *= bar->scale;

    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure %d x %d %d x %d", width, height, width/bar->scale, height/bar->scale);

    if (bar->height == height && bar->width == width && bar->memory_map.ptr) return;

    bar->height = height;
    bar->width = width;

    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure mismatch");
    if (bar->memory_map.ptr) {
        wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure destroyed memory");
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
    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure allocated shm");

    bar->memory_map.ptr = mmap(NULL, total, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    bar->memory_map.size = total;
    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure allocated mmaped");

    if (!bar->memory_map.ptr || bar->memory_map.ptr == MAP_FAILED) {
        close(fd);
        panic("mmap");
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, total);
    for (int i = 0; i < BUFFER_AMNT; i++) {
        offset = size*i;
        bar->buffers[i].buffer = wl_shm_pool_create_buffer(pool, offset, bar->width, bar->height, stride, WL_SHM_FORMAT_ARGB8888);
        bar->buffers[i].ptr = bar->memory_map.ptr + offset;
        wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure allocated buffer: %d %p %p", i, bar->buffers[i].buffer, bar->buffers[i].ptr);
    }
    close(fd);
    wl_shm_pool_destroy(pool);
    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure created memory");

    bar_draw(bar);
    wlc_logln(LOG_DEBUG, "wlr_layer_surface_configure configured");
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

int main(int argc, char **argv) {
    wlc_log_create("bar.log");

    display = wl_display_connect(NULL);
    if (!display) panic("wl_display_connect");

    static int display_fd;
    display_fd = wl_display_get_fd(display);

    registry = wl_display_get_registry(display);
    if (!registry) panic("wl_display_get_registry");

    events = wl_event_loop_create();
    wl_array_init(&event_sources);
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGTERM, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGINT, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_signal(events, SIGHUP, signal_handler, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_fd(events, display_fd, WL_EVENT_READABLE, display_in, NULL));
    WL_ARRAY_ADD(&event_sources, wl_event_loop_add_fd(events, STDIN_FILENO, WL_EVENT_READABLE, stdin_in, NULL));

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

    running = true;

    wlc_logln(LOG_DEBUG, "We are running!");

    while (running) {
        wlc_logln(LOG_DEBUG, "RUNNING START");

        wl_display_dispatch_pending(display);
        if (wl_display_flush(display) == -1 && errno != EAGAIN) {
            wlc_logln(LOG_DEBUG, "wl_display_flush");
            break;
        }

        if (wl_event_loop_dispatch(events, -1) == -1) {
            wlc_logln(LOG_DEBUG, "wl_event_loop_dispatch");
            running = false;
        }

        wlc_logln(LOG_DEBUG, "RUNNING END");
    }

    wlc_logln(LOG_DEBUG, "We are no longer running!");

    cleanup();
    wlc_logln(LOG_DEBUG, "Done with cleanup!");
    wlc_log_destroy();
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
