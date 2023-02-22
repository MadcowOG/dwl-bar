#ifndef COMMON_H_
#define COMMON_H_
#include "xdg-output-unstable-v1-client-protocol.h"
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
#include <wayland-client-protocol.h>
#include <linux/input-event-codes.h>
#include <wayland-cursor.h>

#define VERSION 1.0
#define LENGTH(X) (sizeof X / sizeof X[0] )

// Commonly used typedefs which makes using these structs easier.
typedef struct wl_registry wl_registry;
typedef struct wl_interface wl_interface;
typedef struct wl_display wl_display;
typedef struct wl_compositor wl_compositor;
typedef struct wl_shm wl_shm;
typedef struct wl_shm_pool wl_shm_pool;
typedef struct wl_buffer wl_buffer;
typedef struct wl_list wl_list;
typedef struct wl_output wl_output;
typedef struct xdg_wm_base xdg_wm_base;
typedef struct zwlr_layer_shell_v1 zwlr_layer_shell_v1;
typedef struct wl_surface wl_surface;
typedef struct zwlr_layer_surface_v1 zwlr_layer_surface_v1;
typedef struct wl_callback wl_callback;
typedef struct wl_callback_listener wl_callback_listener;
typedef struct zwlr_layer_surface_v1_listener zwlr_layer_surface_v1_listener;
typedef struct zxdg_output_manager_v1 zxdg_output_manager_v1;
typedef struct zxdg_output_v1 zxdg_output_v1;
typedef struct wl_seat wl_seat;
typedef struct wl_pointer wl_pointer;
typedef struct wl_keyboard wl_keyboard;
typedef struct wl_array wl_array;
typedef struct wl_cursor_theme wl_cursor_theme;
typedef struct wl_cursor_image wl_cursor_image;
typedef struct pollfd pollfd;
typedef enum wl_shm_format wl_shm_format;

extern wl_display* display;
extern wl_compositor* compositor;
extern wl_shm* shm;
extern zwlr_layer_shell_v1* shell;

struct Monitor;

typedef enum { Click_None, Click_Tag, Click_Layout, Click_Title, Click_Status } Clicked;
typedef enum { InActive_Scheme = 0, Active_Scheme = 1, Urgent_Scheme = 2 }      ColorScheme;

typedef union {
    uint ui;
    const void* v;
} Arg;

typedef struct {
    Clicked location;
    int button;
    void (*func)(struct Monitor* monitor, const Arg* arg);
    Arg arg;
} Button;

/* Commonly used functions for allocation and program killing */
void die(const char* fmt, ...);
void* ecalloc(size_t amnt, size_t size);

/*
 * User function definitions.
 * Usually used for when clicking buttons with a pointer.
 */
void spawn(struct Monitor* monitor, const Arg* arg);

#endif // COMMON_H_
