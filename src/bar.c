#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client-protocol.h>

#include <pango-1.0/pango/pangocairo.h>
#include "cairo-deprecated.h"
#include "cairo.h"
#include "pango/pango-context.h"
#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"

#include "config.h"
#include "bar.h"
#include "common.h"
#include "shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef struct Font Font;
typedef struct BarComponent BarComponent;

static void layerSurface(void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t width, uint32_t height);
static void frame(void* data, wl_callback* callback, uint32_t callback_data);
static void bar_render(Bar* bar);
static void bar_tags_render(Bar* bar, cairo_t* painter, int* x);
static void bar_layout_render(Bar* bar, cairo_t* painter, int* x);
static void bar_title_render(Bar* bar, cairo_t* painter, int* x);
static void bar_status_render(Bar* bar, cairo_t* painter, int* x);
static void bar_set_colorscheme(Bar* bar, const int** scheme);
static void set_color(cairo_t* painter, const int rgba[4]);
static void bar_color_background(Bar* bar, cairo_t* painter);
static void bar_color_foreground(Bar* bar, cairo_t* painter);
static Font getFont(void);
static void bar_component_render(Bar* bar, BarComponent* component, cairo_t* painter, uint width, int* x);
static int bar_component_width(BarComponent* component);
static int bar_component_height(BarComponent* component);

struct Font {
    PangoFontDescription* description;

    uint height; /* This is also the same as lrpad from dwm. */
};

struct BarComponent {
    PangoLayout* layout;
    int x; /* Right bound of box */
};

typedef struct {
    uint occupied;
    uint focusedClient; /* If the tag has a focused client */
    uint state;
    BarComponent component;
} Tag;

struct Bar {
    BarComponent layout, title, status;
    Tag tags[9];

    PangoContext* context;
    Font font;

    /* Colors */
    int background[4], foreground[4];

    uint invalid; /* So we don't redraw twice. */
    uint active; /* If this bar is on the active monitor */
    uint floating; /* If the focused client is floating */

    wl_surface* surface;
    zwlr_layer_surface_v1* layer_surface;

    Shm* shm;
};

// So that the compositor can tell us when it's a good time to render again.
const wl_callback_listener frameListener = {
.done = frame
};

// So that wlroots can tell us we need to resize.
// We really only need to worry about this when the bar is visible (sometimes it isn't).
const zwlr_layer_surface_v1_listener layerSurfaceListener = {
.configure = layerSurface
};

void layerSurface(void* data, zwlr_layer_surface_v1* _, uint32_t serial, uint32_t width, uint32_t height) {
    Bar* bar = data;
    zwlr_layer_surface_v1_ack_configure(bar->layer_surface, serial);

    if (bar->shm) {
        if (bar->shm->width == width && bar->shm->height) {
          return;
        }
        shm_destroy(bar->shm);
    }

    bar->shm = shm_create(width, height, WL_SHM_FORMAT_XRGB8888);
    bar_render(bar);
}

void frame(void* data, wl_callback* callback, uint32_t callback_data) {
    Bar* bar = data;
    bar_render(bar);
    wl_callback_destroy(callback);
}

Font getFont(void) {
    PangoFontMap* map = pango_cairo_font_map_get_default();
    if (!map)
        die("font map");

    PangoFontDescription* desc = pango_font_description_from_string(font);
    if (!desc)
        die("font description");

    PangoContext* context = pango_font_map_create_context(map);
    if (!context)
        die("temp context");

    PangoFont* fnt = pango_font_map_load_font(map, context, desc);
    if (!fnt)
        die("font load");

    PangoFontMetrics* metrics = pango_font_get_metrics(fnt, pango_language_get_default());
    if (!metrics)
        die("font metrics");

    Font in = {desc, PANGO_PIXELS(pango_font_metrics_get_height(metrics))};

    pango_font_metrics_unref(metrics);
    g_object_unref(fnt);
    g_object_unref(context);

    return in;
}

int bar_component_width(BarComponent* component) {
    int w;
    pango_layout_get_size(component->layout, &w, NULL);
    return PANGO_PIXELS(w);
}

int bar_component_height(BarComponent* component) {
    int h;
    pango_layout_get_size(component->layout, NULL, &h);
    return PANGO_PIXELS(h);
}

void bar_set_colorscheme(Bar* bar, const int** scheme) {
    for (int i = 0; i < 4; i++) {
        bar->foreground[i] = scheme[0][i];
        bar->background[i] = scheme[1][i];
    }
}

void set_color(cairo_t* painter, const int rgba[4]) {
    cairo_set_source_rgba(painter, rgba[0]/255.0, rgba[1]/255.0, rgba[2]/255.0, rgba[3]/255.0);
}

void bar_color_background(Bar* bar, cairo_t* painter) {
    set_color(painter, bar->background);
}

void bar_color_foreground(Bar* bar, cairo_t* painter) {
    set_color(painter, bar->foreground);
}

void bar_component_render(Bar* bar, BarComponent* component, cairo_t* painter, uint width, int* x) {
    pango_cairo_update_layout(painter, component->layout);
    component->x = *x+width;

    bar_color_background(bar, painter);
    cairo_rectangle(painter, *x, 0, width, bar->shm->height);
    cairo_fill(painter);

    bar_color_foreground(bar, painter);
    cairo_move_to(painter, *x+(bar->font.height/2.0), 1);
    pango_cairo_show_layout(painter, component->layout);
}

void bar_tags_render(Bar* bar, cairo_t* painter, int* x) {
    for ( int i = 0; i < LENGTH(tags); i++ ) {
        Tag tag = bar->tags[i];
        uint tagWidth = bar_component_width(&tag.component) + bar->font.height;

        /* Creating the tag */
        if (tag.state & TAG_ACTIVE) {
            bar_set_colorscheme(bar, schemes[Active_Scheme]);
        } else if (tag.state & TAG_URGENT) {
            bar_set_colorscheme(bar, schemes[Urgent_Scheme]);
        } else {
            bar_set_colorscheme(bar, schemes[InActive_Scheme]);
        }

        bar_component_render(bar, &tag.component, painter, tagWidth, x);

        if (!tag.occupied)
            goto done;

        /*  Creating the occupied tag box */
        int boxHeight = bar->font.height / 9;
        int boxWidth = bar->font.height / 6 + 1;

        if (tag.focusedClient) {
          cairo_rectangle(painter, *x + boxHeight, boxHeight, boxWidth, boxWidth);
          cairo_fill(painter);
        } else {
          cairo_rectangle(painter, *x + boxHeight + 0.5, boxHeight + 0.5, boxWidth, boxWidth);
          cairo_set_line_width(painter, 1);
          cairo_stroke(painter);
        }

        done:
        *x += tagWidth;
    }
}

void bar_layout_render(Bar* bar, cairo_t* painter, int* x) {
    if (!bar)
        return;

    uint layoutWidth = bar_component_width(&bar->layout) + bar->font.height;

    bar_set_colorscheme(bar, schemes[InActive_Scheme]);
    bar_component_render(bar, &bar->layout, painter, layoutWidth, x);

    *x += layoutWidth;
}

void bar_title_render(Bar* bar, cairo_t* painter, int* x) {
    if (!bar)
        return;

    // HUH For some reason ww - x - (status width) works, but ww - x - status width doesn't?
    uint titleWidth = bar->shm->width - *x - (bar_component_width(&bar->status) + bar->font.height);

    bar->active ? bar_set_colorscheme(bar, schemes[Active_Scheme]) : bar_set_colorscheme(bar, schemes[InActive_Scheme]);

    bar_component_render(bar, &bar->title, painter, titleWidth, x);

    if (!bar->floating)
        goto done;

    int boxHeight = bar->font.height / 9;
    int boxWidth = bar->font.height / 6 + 1;

    set_color(painter, grey3);
    cairo_rectangle(painter, *x + boxHeight + 0.5, boxHeight + 0.5, boxWidth, boxWidth);
    cairo_set_line_width(painter, 1);
    cairo_stroke(painter);

    done:
    *x += titleWidth;
}

void bar_status_render(Bar* bar, cairo_t* painter, int* x) {
    if (!bar)
        return;

    uint statusWidth = bar_component_width(&bar->status) + bar->font.height;

    bar_set_colorscheme(bar, schemes[InActive_Scheme]);
    if (!bar->active && status_on_active)
        bar_set_colorscheme(bar, (const int*[4]){ grey1, grey1 } );

    bar_component_render(bar, &bar->status, painter, statusWidth, x);
}

void bar_render(Bar* bar) {
    if (!bar || !bar->shm)
        return;

    int x = 0; /* Keep track of the cairo cursor */
    cairo_surface_t* image = cairo_image_surface_create_for_data(shm_data(bar->shm),
                                                                 CAIRO_FORMAT_ARGB32,
                                                                 bar->shm->width,
                                                                 bar->shm->height,
                                                                 bar->shm->stride);

    cairo_t* painter = cairo_create(image);
    pango_cairo_update_context(painter, bar->context);

    bar_tags_render(bar, painter, &x);
    bar_layout_render(bar, painter, &x);
    bar_title_render(bar, painter, &x);
    bar_status_render(bar, painter, &x);

    wl_surface_attach(bar->surface, shm_buffer(bar->shm), 0, 0);
    wl_surface_damage(bar->surface, 0, 0, bar->shm->width, bar->shm->height);
    wl_surface_commit(bar->surface);

    cairo_destroy(painter);
    cairo_surface_destroy(image);

    shm_flip(bar->shm);
    bar->invalid = 0;
}

Bar* bar_create(void) {
    Bar* bar = ecalloc(1, sizeof(*bar));
    bar->invalid = 0;
    bar->active = 0;
    bar->floating = 0;

    bar->context = pango_font_map_create_context(pango_cairo_font_map_get_default());
    if (!bar->context)
        die("pango context");

    bar->font          = getFont();
    bar->layout.layout = pango_layout_new(bar->context);
    bar->title.layout  = pango_layout_new(bar->context);
    bar->status.layout = pango_layout_new(bar->context);

    bar->layout.x = 0;
    bar->title.x = 0;
    bar->status.x = 0;

    pango_layout_set_font_description(bar->layout.layout, bar->font.description);
    pango_layout_set_font_description(bar->title.layout, bar->font.description);
    pango_layout_set_font_description(bar->status.layout, bar->font.description);

    char* status = ecalloc(8, sizeof(*status));
    snprintf(status, 8, "dwl %.1f", VERSION);

    pango_layout_set_text(bar->layout.layout, "[]=", -1);
    pango_layout_set_text(bar->status.layout, status, -1);

    for ( int i = 0; i < LENGTH(tags); i++ ) { // Initalize the tags
        PangoLayout* layout = pango_layout_new(bar->context);
        pango_layout_set_text(layout, tags[i], strlen(tags[i]));
        pango_layout_set_font_description(layout, bar->font.description);
        Tag tag = { 0, 0, 0, layout };
        bar->tags[i] = tag;
    }

    return bar;
}

void bar_destroy(Bar* bar) {
    uint i;
    if ( !bar )
        return;

    if ( bar->shm )
        shm_destroy(bar->shm);

    if ( bar->surface )
        wl_surface_destroy(bar->surface);

    if ( bar->layer_surface )
        zwlr_layer_surface_v1_destroy(bar->layer_surface);

    if ( bar->context )
        g_object_unref(bar->context);

    if ( bar->layout.layout )
        g_object_unref(bar->layout.layout);

    if ( bar->status.layout )
        g_object_unref(bar->status.layout);

    if ( bar->title.layout )
        g_object_unref(bar->title.layout);

    for ( i = 0; i < LENGTH(tags); i++) {
        Tag tag = bar->tags[i];
        if (tag.component.layout)
            g_object_unref(tag.component.layout);
    }

    return free(bar);
}

// When we need to redraw the bar, because of new information or changes.
// We don't just redraw the bar immediately, we will wait for the compositor to say it's ready.
// This is only for if the bar is shown
void bar_invalidate(Bar* bar) {
    if ( !bar || bar->invalid || !bar_is_visible(bar))
        return;

    wl_callback* cb = wl_surface_frame(bar->surface);

    wl_callback_add_listener(cb, &frameListener, bar);
    wl_surface_commit(bar->surface);
    bar->invalid = 1;
}

void bar_show(Bar* bar, wl_output* output) {
    if (!bar || !output || bar_is_visible(bar)) {
      return;
    }

    bar->surface = wl_compositor_create_surface(compositor);
    bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(shell, bar->surface, output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "doom.dwl-bar");
    zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layerSurfaceListener, bar);

    int anchor = bar_top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
                                     anchor | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

    int height = bar->font.height + 2;
    zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, height);
    zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface, height);
    wl_surface_commit(bar->surface);

}

int bar_is_visible(Bar* bar) {
    // This is dumb, but I don't know how else to do this.
    // We do a negation to convert to boolean int.
    // Then another negation to get the right boolean output.
    // That is 1 when there is a surface, 0 when there isn't.
    return !(!bar->surface);
}

void bar_set_layout(Bar *bar, const char* text) {
    pango_layout_set_text(bar->layout.layout, text, strlen(text));
}

void bar_set_title(Bar *bar, const char* text) {
    pango_layout_set_text(bar->title.layout, text, strlen(text));
}

void bar_set_status(Bar *bar, const char* text) {
    pango_layout_set_text(bar->status.layout, text, strlen(text));
}

void bar_set_active(Bar* bar, uint is_active) {
    bar->active = is_active;
}

void bar_set_floating(Bar* bar, uint is_floating) {
    bar->floating = is_floating;
}

void bar_set_tag(Bar *bar, uint i, uint state, uint occupied, uint focusedClient) {
    Tag* tag = &bar->tags[i];
    tag->focusedClient = focusedClient;
    tag->occupied = occupied;
    tag->state = state;
}

wl_surface* bar_get_surface(Bar *bar) {
    return bar->surface;
}

void bar_click(Bar* bar, struct Monitor* monitor, int x, int y, uint32_t button) {
    Arg* arg = NULL;
    Clicked location = Click_None;

    if (x < bar->tags[LENGTH(bar->tags)-1].component.x) {
        location = Click_Tag;
        for (int i = 0; i < LENGTH(bar->tags); i++) {
            if (x < bar->tags[i].component.x) {
                arg->ui = 1<<i;
                break;
            }
        }
    } else if (x < bar->layout.x) {
        location = Click_Layout;
    } else if (x < bar->title.x) {
        location = Click_Title;
    } else {
        location = Click_Status;
    }

    if (location == Click_None)
        return;

    for (int i = 0; i < LENGTH(buttons); i++) {
        if (buttons[i].location == location && buttons[i].button == button) {
            buttons[i].func(monitor, arg ? arg : &buttons[i].arg);
            return;
        }
    }
}
