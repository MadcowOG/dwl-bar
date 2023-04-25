#include "bar.h"
#include "event.h"
#include "log.h"
#include "render.h"
#include "tray.h"
#include "util.h"
#include "main.h"
#include "input.h"
#include "xdg-output-unstable-v1-protocol.h"
#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

static void check_global(void *global, const char *name);
static void check_globals(void);
static void cleanup(void);
static void dbus_in(int fd, short mask, void *data);
static void display_in(int fd, short mask, void *data);
static void fifo_handle(const char *line);
static void fifo_in(int fd, short mask, void *data);
static void fifo_setup(void);
static void monitor_destroy(struct Monitor *monitor);
static struct Monitor *monitor_from_name(const char *name);
struct Monitor *monitor_from_surface(const struct wl_surface *surface);
static void monitor_initialize(struct Monitor *monitor);
static void monitor_update(struct Monitor *monitor);
static void pipe_in(int fd, short mask, void *data);
static void registry_global_add(void *data, struct wl_registry *registry, uint32_t name,
                        const char *interface, uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void run(void);
static void set_cloexec(int fd);
static void setup(void);
static void stdin_handle(const char *line);
static void stdin_in(int fd, short mask, void *data);
static void sigaction_handler(int _);
static void xdg_output_name(void *data, struct zxdg_output_v1 *output, const char *name);
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);

static struct xdg_wm_base *base;
struct wl_compositor *compositor;
static struct wl_display *display;
static int display_fd;
static struct Events *events;
static int fifo_fd;
static char *fifo_path;
static struct wl_list monitors; // struct Monitor*
static struct zxdg_output_manager_v1 *output_manager;
static const struct wl_registry_listener registry_listener = {
    .global = registry_global_add,
    .global_remove = registry_global_remove,
};
static int running = 0;
static struct wl_list seats; // struct Seat*
static int self_pipe[2];
struct zwlr_layer_shell_v1 *shell;
struct wl_shm *shm;
static struct Tray *tray;
static const struct zxdg_output_v1_listener xdg_output_listener = {
    .name = xdg_output_name,
};
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

void check_global(void *global, const char *name) {
    if (global)
        return;
    panic("Wayland compositor did not export: %s", name);
}

void check_globals(void) {
    check_global(base, "xdg_wm_base");
    check_global(compositor, "wl_compositor");
    check_global(output_manager, "zxdg_output_manager_v1");
    check_global(shell, "zwlr_layer_shell_v1");
    check_global(shm, "wl_shm");
}

void cleanup(void) {
    xdg_wm_base_destroy(base);
    wl_compositor_destroy(compositor);
    close(fifo_fd);
    unlink(fifo_path);
    free(fifo_path);
    zxdg_output_manager_v1_destroy(output_manager);
    zwlr_layer_shell_v1_destroy(shell);
    wl_shm_destroy(shm);
    events_destroy(events);
    log_destroy();

    struct Monitor *monitor, *tmp_monitor;
    wl_list_for_each_safe(monitor, tmp_monitor, &monitors, link)
        monitor_destroy(monitor);

    struct Seat *seat, *tmp_seat;
    wl_list_for_each_safe(seat, tmp_seat, &seats, link)
        seat_destroy(seat);

    wl_display_disconnect(display);
}

void dbus_in(int fd, short mask, void *data) {
    if (mask & (POLLHUP | POLLERR)) {
        running = 0;
        return;
    }

    int return_value;
    while((return_value = sd_bus_process(tray->bus, NULL)) > 0);
    if (return_value < 0)
        panic("dbus_in: sd_bus_process failed to process bus.");
}

void display_in(int fd, short mask, void *data) {
    if (mask & (POLLHUP | POLLERR) ||
            wl_display_dispatch(display) == -1) {
        running = 0;
        return;
    }
}

void fifo_handle(const char *line) {
    char *command;
    unsigned long loc = 0;

    command = to_delimiter(line, &loc, ' ');

    if (STRING_EQUAL(command, "status")) {
        char *status = to_delimiter(line, &loc, '\n');
        struct Monitor *pos;
        wl_list_for_each(pos, &monitors, link) {
            bar_set_status(pos->bar, status);
            pipeline_invalidate(pos->pipeline);
        }
        free(status);
    }

    free(command);
}

void fifo_in(int fd, short mask, void *data) {
    if (mask & POLLERR) {
        events_remove(events, fd);
        char *default_status = string_create("dwl %.1f", VERSION);
        struct Monitor *pos;
        wl_list_for_each(pos, &monitors, link) {
            bar_set_status(pos->bar, default_status);
            pipeline_invalidate(pos->pipeline);
        }
        free(default_status);
        return;
    }

    int new_fd = dup(fd);
    FILE *fifo_file = fdopen(new_fd, "r");
    char *buffer = NULL;
    size_t size = 0;
    while (1) {
        if (getline(&buffer, &size, fifo_file) == -1)
            break;

        fifo_handle(buffer);
    }
    free(buffer);
    fclose(fifo_file);
    close(new_fd);

}

void fifo_setup(void) {
  int result, i;
  char *runtime_path = getenv("XDG_RUNTIME_DIR");

  for (i = 0; i < 100; i++) {
    fifo_path = string_create("%s/dwl-bar-%d", runtime_path, i);

    result = mkfifo(fifo_path, 0666);
    if (result < 0) {
      if (errno != EEXIST)
          panic("mkfifo");

      continue;
    }

    if ((fifo_fd = open(fifo_path, O_CLOEXEC | O_RDONLY | O_NONBLOCK)) < 0)
        panic("open fifo");

    return;
  }

  panic("setup fifo"); /* If we get here then we couldn't setup the fifo */
}

void monitor_destroy(struct Monitor *monitor) {
    if (!monitor)
        return;

    free(monitor->xdg_name);
    if (wl_output_get_version(monitor->wl_output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
        wl_output_release(monitor->wl_output);
    list_elements_destroy(monitor->hotspots, free);
    pipeline_destroy(monitor->pipeline);
    bar_destroy(monitor->bar);
    free(monitor);
}

struct Monitor *monitor_from_name(const char *name) {
    struct Monitor *pos;
    wl_list_for_each(pos, &monitors, link) {
        if (STRING_EQUAL(name, pos->xdg_name))
            return pos;
    }

    return NULL;
}

struct Monitor *monitor_from_surface(const struct wl_surface *surface) {
    struct Monitor *pos;
    wl_list_for_each(pos, &monitors, link) {
        if (pos->pipeline->surface == surface)
            return pos;
    }

    return NULL;
}

void monitor_initialize(struct Monitor *monitor) {
    if (!monitor) return;

    monitor->hotspots = list_create(1);
    monitor->pipeline = pipeline_create();
    monitor->bar = bar_create(monitor->hotspots, monitor->pipeline);
    tray_register_to_monitor(tray, monitor->hotspots, monitor->pipeline);
    if (!monitor->pipeline || !monitor->bar)
        panic("Failed to create a pipline or bar for monitor: %s", monitor->xdg_name);
    monitor_update(monitor);
}

void monitor_update(struct Monitor *monitor) {
    if (!monitor)
        return;

    if (!pipeline_is_visible(monitor->pipeline)) {
        pipeline_show(monitor->pipeline, monitor->wl_output);
        return;
    }

    pipeline_invalidate(monitor->pipeline);
}

void monitors_update(void) {
    struct Monitor *monitor;
    wl_list_for_each(monitor, &monitors, link) {
        monitor_update(monitor);
    }
}

void pipe_in(int fd, short mask, void *data) {
    running = 0;
}

void registry_global_add(void *data, struct wl_registry *registry, uint32_t name,
                        const char *interface, uint32_t version) {
    if (STRING_EQUAL(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (STRING_EQUAL(interface, wl_output_interface.name)) {
        struct Monitor *monitor = ecalloc(1, sizeof(*monitor));
        monitor->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        monitor->wl_name = name;
        monitor->xdg_name = NULL;
        monitor->xdg_output = NULL;

        wl_list_insert(&monitors, &monitor->link);

        if (!output_manager)
            return;

        monitor->xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, monitor->wl_output);
        zxdg_output_v1_add_listener(monitor->xdg_output, &xdg_output_listener, monitor);

        if (!running) return;
        monitor_initialize(monitor);
    }
    else if (STRING_EQUAL(interface, wl_seat_interface.name)) {
        struct Seat *seat = ecalloc(1, sizeof(*seat));
        seat->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        seat->wl_name = name;
        seat->pointer = NULL;
        seat->touch = NULL;
        wl_list_insert(&seats, &seat->link);
        wl_seat_add_listener(seat->seat, &seat_listener, seat);
    }
    else if (STRING_EQUAL(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (STRING_EQUAL(interface, xdg_wm_base_interface.name)) {
        base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 2);
        xdg_wm_base_add_listener(base, &xdg_wm_base_listener, NULL);
    }
    else if (STRING_EQUAL(interface, zxdg_output_manager_v1_interface.name)) {
        output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 3);

        struct Monitor *pos;
        wl_list_for_each(pos, &monitors, link) {
            // If the monitor is getting or has the xdg_name.
            if (pos->xdg_output || pos->xdg_name)
                continue;

            pos->xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, pos->wl_output);
            zxdg_output_v1_add_listener(pos->xdg_output, &xdg_output_listener, pos);
        }
    }
    else if (STRING_EQUAL(interface, zwlr_layer_shell_v1_interface.name))
        shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
}

void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct Monitor *monitor, *tmp_monitor;
    wl_list_for_each_safe(monitor, tmp_monitor, &monitors, link) {
        if (monitor->wl_name != name) continue;
        wl_list_remove(&monitor->link);
        monitor_destroy(monitor);
    }

    struct Seat *seat, *tmp_seat;
    wl_list_for_each_safe(seat, tmp_seat, &seats, link) {
        if (seat->wl_name != name) continue;
        wl_list_remove(&seat->link);
        seat_destroy(seat);
    }
}

void run(void) {
    running = 1;

    while (running) {
        wl_display_dispatch_pending(display);
        if (wl_display_flush(display) == -1 && errno != EAGAIN)
            break;

        events_poll(events);
    }
}

void set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1)
        panic("F_GETFD");
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        panic("FD_SETFD");
}

void setup(void) {
    if (pipe(self_pipe) == -1)
        panic("pipe");

    set_cloexec(self_pipe[0]);
    set_cloexec(self_pipe[1]);

    static struct sigaction sighandle;
    static struct sigaction child_sigaction;

    sighandle.sa_handler = &sigaction_handler;
    child_sigaction.sa_handler = SIG_IGN;

    if (sigaction(SIGTERM, &sighandle, NULL) < 0)
        panic("sigaction SIGTERM");
    if (sigaction(SIGINT, &sighandle, NULL) < 0)
        panic("sigaction SIGINT");
    if (sigaction(SIGCHLD, &child_sigaction, NULL) < 0)
        panic("sigaction SIGCHLD");

    display = wl_display_connect(NULL);
    if (!display)
        panic("Failed to connect to Wayland compositor.");
    display_fd = wl_display_get_fd(display);

    wl_list_init(&seats);
    wl_list_init(&monitors);

    tray = tray_create();

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    fifo_setup();

    check_globals();

    wl_display_roundtrip(display);

    struct Monitor *monitor;
    wl_list_for_each(monitor, &monitors, link) {
        monitor_initialize(monitor);
    }

    if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0)
        panic("STDIN_FILENO O_NONBLOCK");

    events = events_create();
    events_add(events, display_fd, POLLIN, NULL, display_in);
    events_add(events, self_pipe[0], POLLIN, NULL, pipe_in);
    events_add(events, STDIN_FILENO, POLLIN, NULL, stdin_in);
    events_add(events, fifo_fd, POLLIN, NULL, fifo_in);
    events_add(events, tray->bus_fd, POLLIN, NULL, dbus_in);
}

void stdin_handle(const char *line) {
    if (!line)
        return;

    char *name, *command;
    struct Monitor *monitor;
    unsigned long loc = 0; /* Keep track of where we are in the string `line` */

    name = to_delimiter(line, &loc, ' ');
    command = to_delimiter(line, &loc, ' ');
    monitor = monitor_from_name(name);
    if (!monitor) {
        free(name);
        free(command);
        return;
    }
    free(name);

    if (STRING_EQUAL(command, "title")) {
        char *title = to_delimiter(line, &loc, '\n');
        if (*title == '\0') {
            bar_set_title(monitor->bar, "");
        } else
            bar_set_title(monitor->bar, title);
        free(title);
    } else if (STRING_EQUAL(command, "appid")) {
        /* Do nothing */
    } else if (STRING_EQUAL(command, "floating")) {
        char *is_floating = to_delimiter(line, &loc, '\n');
        if (*is_floating == '1')
            bar_set_floating(monitor->bar, 1);
        else
            bar_set_floating(monitor->bar, 0);
        free(is_floating);
    } else if (STRING_EQUAL(command, "fullscreen")) {
        /* Do nothing */
    } else if (STRING_EQUAL(command, "selmon")) {
        char *selmon = to_delimiter(line, &loc, '\n');
        if (*selmon == '1')
            bar_set_active(monitor->bar, 1);
        else
            bar_set_active(monitor->bar, 0);
        free(selmon);
    } else if (STRING_EQUAL(command, "tags")) {
        char *occupied_str, *tags_str, *clients_str, *urgent_str;
        int occupied, _tags, clients, urgent, i, tag_mask, state;

        occupied_str = to_delimiter(line, &loc, ' ');
        tags_str    = to_delimiter(line, &loc, ' ');
        clients_str = to_delimiter(line, &loc, ' ');
        urgent_str  = to_delimiter(line, &loc, ' ');

        occupied = atoi(occupied_str);
        _tags    = atoi(tags_str);
        clients  = atoi(clients_str);
        urgent   = atoi(urgent_str);

        for (i = 0; i < LENGTH(tags); i++) {
            state    = Tag_None;
            tag_mask = 1 << i;

            if (_tags & tag_mask)
                state |= Tag_Active;
            if (urgent & tag_mask)
                state |= Tag_Urgent;

            bar_set_tag(monitor->bar, i, state, occupied & tag_mask ? 1 : 0, clients & tag_mask ? 1 : 0);
        }

        free(occupied_str);
        free(tags_str);
        free(clients_str);
        free(urgent_str);
    } else if (STRING_EQUAL(command, "layout")) {
        char *layout = to_delimiter(line, &loc, '\n');
        bar_set_layout(monitor->bar, layout);
        free(layout);
    }

    free(command);
    monitor_update(monitor);
}

void stdin_in(int fd, short mask, void *data) {
    if (mask & (POLLHUP | POLLERR)) {
        running = 0;
        return;
    }

    int new_fd = dup(fd);
    FILE *stdin_file = fdopen(new_fd, "r");
    char *buffer = NULL;
    size_t size = 0;
    while(1) {
         if (getline(&buffer, &size, stdin_file) == -1)
            break;

        stdin_handle(buffer);
    }
    free(buffer);
    fclose(stdin_file);
    close(new_fd);
}

void sigaction_handler(int _) {
    if (write(self_pipe[1], "0", 1) < 0)
        panic("sigaction_handler");
}

void xdg_output_name(void *data, struct zxdg_output_v1 *output, const char *name) {
    struct Monitor *monitor = data;
    monitor->xdg_name = strdup(name);
    zxdg_output_v1_destroy(output);
    monitor->xdg_output = NULL;
}

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

int main(int argc, char *argv[]) {
    int opt;
    while((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
            case 'l':
                if (!setup_log())
                    panic("Failed to setup logging");
                break;
            case 'h':
                printf("Usage: %s [-h] [-v]\n", argv[0]);
                exit(EXIT_SUCCESS);
            case 'v':
                printf("%s %.1f\n", argv[0], VERSION);
                exit(EXIT_SUCCESS);
            case '?':
                printf("Invalid Argument\n");
                printf("Usage: %s [-h] [-v] [-l]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    setup();
    run();
    cleanup();
}

void panic(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[dwl-bar] panic: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);

    } else {
        fputc('\n', stderr);
    }

    cleanup();
    exit(EXIT_FAILURE);
}
