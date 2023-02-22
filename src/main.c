#include <asm-generic/errno-base.h>
#include <stdarg.h>
#include <stddef.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include "bar.h"
#include "cairo.h"
#include "common.h"
#include "config.h"
#include "shm.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

/*
 * When checking to see if two strings are the same with strcmp,
 * 0 means they are the same, otherwise they are different.
 */
#define EQUAL 0
#define POLLFDS 4

// TODO Create Github for dwl-bar then patchset for dwl-bar ipc
//
// TODO Get the ipc wayland protocol working.
//      + Include a `hide` / `toggle_visibility` event so that the bar can hide itself when the user asks.
// TODO Create dwl-ipc patchset then submit to dwl wiki.

typedef struct Monitor {
  char *xdg_name;
  uint32_t registry_name;

  wl_output *output;
  Bar *bar;
  wl_list link;
} Monitor;

typedef struct {
  wl_pointer* pointer;
  Monitor* focused_monitor;

  int x, y;
  uint32_t* buttons;
  uint size;
} Pointer;

typedef struct {
  uint32_t registry_name;
  wl_seat* seat;
  wl_list link;

  Pointer* pointer;
} Seat;

typedef struct {
  wl_output *output;
  uint32_t name;
  wl_list link;
} uninitOutput;

typedef struct {
  wl_registry *registry;
  uint32_t name;
  const char *interface_str;
} HandleGlobal;

static void cleanup(void);
static void setup(void);
static void run(void);
static void globalChecks(void);
static void checkGlobal(void *global, const char *name);
static void monitorSetup(uint32_t name, wl_output *output);
static void set_cloexec(int fd);
static void sighandler(int _);
static void flush(void);
static void setupFifo(void);
static char* to_delimiter(char* string, ulong *start_end, char delimiter);
static char* get_line(int fd);
static void on_status(void);
static void on_stdin(void);
static void handle_stdin(char* line);
static Monitor* monitor_from_name(char* name);
static Monitor* monitor_from_surface(wl_surface* surface);
static void update_monitor(Monitor* monitor);

/* Register listener related functions */
static void onGlobalAdd(void *data, wl_registry *registry, uint32_t name,
                        const char *interface, uint32_t version);
static void onGlobalRemove(void *data, wl_registry *registry, uint32_t name);
static int regHandle(void **store, HandleGlobal helper,
                     const wl_interface *interface, int version);

/* xdg listener members */
static void ping(void *data, xdg_wm_base *xdg_wm_base, uint32_t serial);
static void name(void *data, zxdg_output_v1 *output, const char *name);

/* seat listener member */
static void capabilites(void* data, wl_seat* wl_seat, uint32_t capabilities);

/* pointer listener members */
static void enter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
static void leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
static void motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void button(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void frame(void* data, wl_pointer* pointer);

/* Also pointer listener members, but we don't do anything in these functions */
static void axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {}
static void axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {}
static void axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {}
static void axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {}
static void seat_name(void* data, wl_seat* seat, const char* name) {}

/* Globals */
wl_display *display;
wl_compositor *compositor;
wl_shm *shm;
zwlr_layer_shell_v1 *shell;
static xdg_wm_base *base;
static zxdg_output_manager_v1 *output_manager;

/* Cursor */
static wl_cursor_image* cursor_image;
static wl_surface* cursor_surface;

/* Lists */
static wl_list seats;
static wl_list monitors;
static wl_list uninitializedOutputs;

/* File Descriptors */
static pollfd *pollfds;
static int wl_display_fd;
static int self_pipe[2];
static int fifo_fd;
static char* fifo_path;

/* Sigactions */
static struct sigaction sighandle;
static struct sigaction child_handle;

/*
 * So that the global handler knows that we can initialize an output.
 * Rather than just store it for when we have all of our globals.
 *
 * Since wayland is asynchronous we may get our outputs before we're ready for
 * them.
 */
static int ready = 0;

static const struct wl_registry_listener registry_listener = {
    .global = onGlobalAdd,
    .global_remove = onGlobalRemove,
};

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = ping,
};

/* So that we can get the monitor names to match with dwl monitor names. */
static const struct zxdg_output_v1_listener xdg_output_listener = {
    .name = name,
};

static const struct wl_pointer_listener pointer_listener = {
    .enter  = enter,
    .leave  = leave,
    .motion = motion,
    .button = button,
    .frame  = frame,

    .axis = axis,
    .axis_discrete = axis_discrete,
    .axis_source = axis_source,
    .axis_stop = axis_stop,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = capabilites,
    .name = seat_name,
};

void enter(void *data, wl_pointer *pointer, uint32_t serial,
           wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
  Seat seat = *(Seat*)data;
  seat.pointer->focused_monitor = monitor_from_surface(surface);

  if (!cursor_image) {
    wl_cursor_theme *theme = wl_cursor_theme_load(NULL, 24, shm);
    cursor_image = wl_cursor_theme_get_cursor(theme, "left_ptr")->images[0];
    cursor_surface = wl_compositor_create_surface(compositor);
    wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(cursor_image), 0, 0);
    wl_surface_commit(cursor_surface);
  }

  wl_pointer_set_cursor(pointer, serial, cursor_surface,
                        cursor_image->hotspot_x, cursor_image->hotspot_y);
}

void leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface) {
  Seat seat = *(Seat*)data;
  seat.pointer->focused_monitor = NULL;
}

void motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
  Seat seat = *(Seat*)data;
  seat.pointer->x = wl_fixed_to_int(x);
  seat.pointer->y = wl_fixed_to_int(y);
}

void button(void *data, wl_pointer *pointer, uint32_t serial, uint32_t time,
            uint32_t button, uint32_t state) {
  Seat seat = *(Seat*)data;
  uint32_t* new_buttons = NULL;
  int i, prev = -1; /* The index of this button */

  for (i = 0; i < seat.pointer->size; i++) {
    if (seat.pointer->buttons[i] == button)
      prev = i;
  }

  /* If this button was newly pressed. */
  if (state == WL_POINTER_BUTTON_STATE_PRESSED && prev == -1) {
    new_buttons = ecalloc(seat.pointer->size+1, sizeof(uint32_t));
    for (i = 0; i < seat.pointer->size+1; i++) {
      if (i == seat.pointer->size) {
        new_buttons[i] = button;
        break;
      }

      new_buttons[i] = seat.pointer->buttons[i];
    }

    seat.pointer->size++;
  }

  /* If this button was released and we have it. */
  if(state == WL_KEYBOARD_KEY_STATE_RELEASED && prev != -1) {
    new_buttons = ecalloc(seat.pointer->size-1, sizeof(uint32_t));
    for (i = 0; i < seat.pointer->size; i++) {
      if (i == prev)
        continue;

      if (i < prev)
        new_buttons[i] = seat.pointer->buttons[i];

      if (i > prev)
        new_buttons[i-1] = seat.pointer->buttons[i];
    }

    seat.pointer->size--;
  }

  free(seat.pointer->buttons);
  seat.pointer->buttons = new_buttons;
  return;
}

static void frame(void* data, wl_pointer* pointer) {
  Seat seat = *(Seat*)data;
  Monitor* monitor = seat.pointer->focused_monitor;
  if (!monitor) {
    return;
  }

  for (int i = 0; i < seat.pointer->size; i++) {
    bar_click(monitor->bar, monitor, seat.pointer->x, seat.pointer->y, seat.pointer->buttons[i]);
  }

  free(seat.pointer->buttons);
  seat.pointer->buttons = NULL;
  seat.pointer->size = 0;
}

void capabilites(void* data, wl_seat* wl_seat, uint32_t capabilities) {
  Seat* seat = data;
  int has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (!seat->pointer && has_pointer) {
    seat->pointer = ecalloc(1, sizeof(Pointer));
    seat->pointer->pointer = wl_seat_get_pointer(wl_seat);
    seat->pointer->buttons = NULL;
    seat->pointer->size = 0;
    wl_pointer_add_listener(seat->pointer->pointer, &pointer_listener, seat);
    return;
  }

  if (seat->pointer && !has_pointer) {
    wl_pointer_release(seat->pointer->pointer);
    seat->pointer->focused_monitor = NULL;
    if (seat->pointer->buttons) {
      free(seat->pointer->buttons);
    }
    free(seat->pointer);
  }
}

static void name(void *data, zxdg_output_v1 *xdg_output, const char *name) {
  Monitor *monitor = data;
  monitor->xdg_name = strdup(name);
  zxdg_output_v1_destroy(xdg_output);
}

void ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}

int regHandle(void **store, HandleGlobal helper, const wl_interface *interface,
              int version) {
  if (strcmp(helper.interface_str, interface->name) != EQUAL)
    return 0;

  *store = wl_registry_bind(helper.registry, helper.name, interface, version);
  return 1;
}

void onGlobalAdd(void *_, wl_registry *registry, uint32_t name,
                 const char *interface, uint32_t version) {

  HandleGlobal helper = {registry, name, interface};
  if (regHandle((void **)&compositor, helper, &wl_compositor_interface, 4))
    return;
  if (regHandle((void **)&shm, helper, &wl_shm_interface, 1))
    return;
  if (regHandle((void **)&output_manager, helper, &zxdg_output_manager_v1_interface, 3))
    return;
  if (regHandle((void **)&shell, helper, &zwlr_layer_shell_v1_interface, 4))
    return;
  if (regHandle((void **)&base, helper, &xdg_wm_base_interface, 2)) {
    xdg_wm_base_add_listener(base, &xdg_wm_base_listener, NULL);
    return;
  }

  {
    wl_output *output;
    if (regHandle((void **)&output, helper, &wl_output_interface, 1)) {
      if (ready) {
        monitorSetup(name, output);
      } else {
        uninitOutput *uninit = ecalloc(1, sizeof(uninitOutput));
        uninit->output = output;
        uninit->name = name;
        wl_list_insert(&uninitializedOutputs, &uninit->link);
      }
      return;
    }
  }

  {
    wl_seat* seat;
    if (regHandle((void**)&seat, helper, &wl_seat_interface, 7)) {
      Seat* seat_ = ecalloc(1, sizeof(*seat_));
      seat_->seat = seat;
      seat_->registry_name = name;
      wl_list_insert(&seats, &seat_->link);
      wl_seat_add_listener(seat, &seat_listener, seat_);
      return;
    }
  }
}

void onGlobalRemove(void *_, wl_registry *registry, uint32_t name) {
  /* Deconstruct a monitor when it disappears */
  Monitor *current_monitor, *tmp_monitor;
  wl_list_for_each_safe(current_monitor, tmp_monitor, &monitors, link) {
    if (current_monitor->registry_name == name) {
      wl_list_remove(&current_monitor->link);
      bar_destroy(current_monitor->bar);
    }
  }

  /* Deconstruct seat when it disappears */
  Seat *seat, *tmp_seat;
  wl_list_for_each_safe(seat, tmp_seat, &seats, link) {
    if (seat->registry_name == name) {
      seat->pointer->focused_monitor = NULL;
      wl_pointer_release(seat->pointer->pointer);
      wl_list_remove(&seat->link);
      free(seat->pointer->buttons);
      free(seat->pointer);
      free(seat);
    }
  }
}

void spawn(Monitor* monitor, const Arg *arg) {
  if (fork() != 0)
    return;

  char* const* argv = arg->v;
  setsid();
  execvp(argv[0], argv);
  fprintf(stderr, "bar: execvp %s", argv[0]);
  perror(" failed\n");
  exit(1);
}

void checkGlobal(void *global, const char *name) {
  if (global)
    return;
  fprintf(stderr, "Wayland server did not export %s\n", name);
  cleanup();
  exit(1);
}

/*
 * We just check and make sure we have our needed globals if any fail then we
 * exit.
 */
void globalChecks() {
  checkGlobal(compositor, "wl_compositor");
  checkGlobal(shm, "wl_shm");
  checkGlobal(base, "xdg_wm_base");
  checkGlobal(shell, "wlr_layer_shell");
  checkGlobal(output_manager, "zxdg_output_manager");

  ready = 1;
}

void monitorSetup(uint32_t name, wl_output *output) {
  Monitor *monitor = ecalloc(1, sizeof(*monitor));

  monitor->bar = bar_create();
  monitor->output = output;
  monitor->registry_name = name;

  wl_list_insert(&monitors, &monitor->link);

  // So we can get the monitor name.
  zxdg_output_v1 *xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);
  zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, monitor);
}

void set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags == -1)
    die("FD_GETFD");
  if (fcntl(fd, flags | FD_CLOEXEC) < 0)
    die("FDD_SETFD");
}

void sighandler(int _) {
  if (write(self_pipe[1], "0", 1) < 0)
    die("sighandler");
}

void flush(void) {
  wl_display_dispatch_pending(display);
  if (wl_display_flush(display) < 0 && errno == EAGAIN) {
    for (int i = 0; i < POLLFDS; i++) {
      struct pollfd *poll = &pollfds[i];
      if (poll->fd == wl_display_fd)
        poll->events |= POLLOUT;
    }
  }
}

void setupFifo(void) {
  int result, fd, i;
  char *runtime_path = getenv("XDG_RUNTIME_DIR"), *file_name, *path;

  for (i = 0; i < 100; i++) {
    file_name = ecalloc(12, sizeof(*file_name));
    sprintf(file_name, "/dwl-bar-%d", i);

    path = ecalloc(strlen(runtime_path)+strlen(file_name)+1, sizeof(char));
    strcat(path, runtime_path);
    strcat(path, file_name);
    free(file_name);

    result = mkfifo(path, 0666);
    if (result < 0) {
      if (errno != EEXIST)
        die("mkfifo");

      free(path);
      continue;
    }

    if ((fd = open(path, O_CLOEXEC | O_RDONLY | O_NONBLOCK)) < 0)
      die("open fifo");

    fifo_path = path;
    fifo_fd = fd;

    return;
  }

  die("setup fifo"); /* If we get here then we couldn't setup the fifo */
}

void update_monitor(Monitor* monitor) {
  if (!bar_is_visible(monitor->bar)) {
    bar_show(monitor->bar, monitor->output);
    return;
  }

  bar_invalidate(monitor->bar);
  return;
}

Monitor* monitor_from_name(char* name) {
  Monitor* monitor;
  wl_list_for_each(monitor, &monitors, link) {
    if (strcmp(name, monitor->xdg_name) == EQUAL)
      return monitor;
  }

  return NULL;
}

Monitor* monitor_from_surface(wl_surface* surface) {
  Monitor* monitor;
  wl_list_for_each(monitor, &monitors, link) {
    if (surface == bar_get_surface(monitor->bar))
      return monitor;
  }

  return NULL;
}

/*
 * Parse and extract a substring based on a delimiter
 * start_end is a ulong that we will use to base our starting location.
 * Then replace as the end point to be used later on.
 */
char* to_delimiter(char* string, ulong *start_end, char delimiter) {
  char* output;
  ulong i, len = strlen(string);

  if (*start_end > len)
    return NULL;

  for ( i = *start_end; i < len; i++ ) {
    if (string[i] == delimiter || i == len-1) { // We've reached the delimiter or the end.
      break;
    }
  }

  /* Create and copy the substring what we need */
  output = strncpy(ecalloc(i - *start_end, sizeof(*output)),
                   string + *start_end, i - *start_end);

  output[i - *start_end] = '\0'; // null terminate
  *start_end = i+1;

  return output;
}

/* The `getline` from stdio.h wasn't working so I've made my own. */
char* get_line(int fd) {
  char *output, buffer[512], character;
  int i = 0, r = i;

  while ((r = read(fd, &character, 1)) > 0 && i < 512) {
    buffer[i] = character;
    i++;

    if (character == '\n')
      break;
  }

  /* Checking for edge cases */
  if ((r == 0 && i == 0) || (i == 1 && buffer[i] == '\n'))
    return NULL;

  buffer[i] = '\0';
  output = strcpy(ecalloc(i, sizeof(*output)), buffer);

  return output;
}

void on_stdin(void) {
  while (1) {
    char *buffer = get_line(STDIN_FILENO);
    if (!buffer || !strlen(buffer)) {
      free(buffer);
      return;
    }

    handle_stdin(buffer);
    free(buffer);
  }
}

void handle_stdin(char* line) {
  char *name, *command;
  Monitor* monitor;
  ulong loc = 0; /* Keep track of where we are in the string `line` */

  name = to_delimiter(line, &loc, ' ');
  command = to_delimiter(line, &loc, ' ');
  monitor = monitor_from_name(name);
  if (!monitor)
    return;
  free(name);

  // Hate the way these if statements look. Is being this explicit worth it?
  if (strcmp(command, "title") == EQUAL) {
    if (line[strlen(line)-2] == ' ') {
      bar_set_title(monitor->bar, "");
      goto done;
    }

    char* title = to_delimiter(line, &loc, '\n');
    bar_set_title(monitor->bar, title);
    free(title);

  } else if (strcmp(command, "floating") == EQUAL) {
    if (line[strlen(line)-2] == ' ') {
      bar_set_floating(monitor->bar, 0);
      goto done;
    }

    char* is_floating = to_delimiter(line, &loc, '\n');
    strcmp(is_floating, "1") == EQUAL ? bar_set_floating(monitor->bar, 1) : bar_set_floating(monitor->bar, 0);
    free(is_floating);

  } else if (strcmp(command, "fullscreen") == EQUAL) {
    /* Do nothing */
  } else if (strcmp(command, "selmon") == EQUAL) {
    char* selmon = to_delimiter(line, &loc, '\n');
    strcmp(selmon, "1") == EQUAL ? bar_set_active(monitor->bar, 1) : bar_set_active(monitor->bar, 0);
    free(selmon);

  } else if (strcmp(command, "tags") == EQUAL) {
    char *occupied_, *tags__, *clients_, *urgent_;
    int occupied, tags_, clients, urgent, i, tag_mask;

    occupied_ = to_delimiter(line, &loc, ' ');
    tags__    = to_delimiter(line, &loc, ' ');
    clients_  = to_delimiter(line, &loc, ' ');
    urgent_  = to_delimiter(line,  &loc, '\n');

    occupied = atoi(occupied_);
    tags_    = atoi(tags__);
    clients  = atoi(clients_);
    urgent   = atoi(urgent_);

    for(i = 0; i < LENGTH(tags); i++) {
      int state = TAG_INACTIVE;
      tag_mask = 1 << i;

      if (tags_ & tag_mask)
        state |= TAG_ACTIVE;
      if (urgent & tag_mask)
        state |= TAG_URGENT;

      bar_set_tag(monitor->bar, i, state, occupied & tag_mask ? 1: 0, clients & tag_mask ? 1 : 0);
    }

    free(occupied_);
    free(tags__);
    free(clients_);
    free(urgent_);

  } else if (strcmp(command, "layout") == EQUAL) {
    char* layout = to_delimiter(line, &loc, '\n');
    bar_set_layout(monitor->bar, layout);
    free(layout);
  } else {
    die("command unrecognized");
  }

  done:
  free(command);
  update_monitor(monitor);
}

void on_status(void) {
  while (1) {
    char *line = get_line(fifo_fd), *command, *status;
    ulong loc = 0;
    if (!line || !strlen(line)) {
      free(line);
      return;
    }

    command = to_delimiter(line, &loc, ' ');
    status  = to_delimiter(line, &loc, '\n');

    if (strcmp(command, "status") != EQUAL) {
      free(status);
      goto done;
    }

    Monitor *current_monitor;
    wl_list_for_each(current_monitor, &monitors, link) {
        bar_set_status(current_monitor->bar, status);
        update_monitor(current_monitor);
    }


    done:
    free(line);
    free(command);
    free(status);
  }
}

void setup(void) {
  if (pipe(self_pipe) < 0)
    die("pipe");

  set_cloexec(self_pipe[0]);
  set_cloexec(self_pipe[1]);

  sighandle.sa_handler = &sighandler;
  child_handle.sa_handler = SIG_IGN;

  if (sigaction(SIGTERM, &sighandle, NULL) < 0 ||
      sigaction(SIGINT, &sighandle, NULL) < 0 ||
      sigaction(SIGCHLD, &child_handle, NULL) < 0)
    die("sigaction");

  display = wl_display_connect(NULL);
  if (!display)
    die("Failed to connect to a Wayland display.");
  wl_display_fd = wl_display_get_fd(display);

  wl_list_init(&seats);
  wl_list_init(&monitors);
  wl_list_init(&uninitializedOutputs);

  wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);

  setupFifo();

  globalChecks(); /* Make sure we're ready to start the rest of the program */

  wl_display_roundtrip(display);

  // Initalize any outputs we got from registry. Then free the outputs.
  uninitOutput *output, *tmp;
  wl_list_for_each_safe(output, tmp, &uninitializedOutputs, link) {
    monitorSetup(output->name, output->output);
    wl_list_remove(&output->link);
    free(output);
  }

  wl_display_roundtrip(display);

  pollfds = ecalloc(POLLFDS, sizeof(*pollfds));

  pollfds[0] = (pollfd) {STDIN_FILENO, POLLIN};
  pollfds[1] = (pollfd) {wl_display_fd, POLLIN};
  pollfds[2] = (pollfd) {self_pipe[0], POLLIN};
  pollfds[3] = (pollfd) {fifo_fd, POLLIN};

  if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0)
    die("O_NONBLOCK");
}

void run(void) {
  struct pollfd *pollfd;
  int i, quitting = 0;

  while (!quitting) {
    flush();
    if (poll(pollfds, POLLFDS, -1) < 0 && errno != EINTR)
        die("poll");

    for (i = 0; i < POLLFDS; i++) {
      pollfd = &pollfds[i];
      if (pollfd->revents & POLLNVAL)
        die("POLLNVAL");

      if (pollfd->fd == wl_display_fd) {
        if ((pollfd->revents & POLLIN) && (wl_display_dispatch(display) < 0))
          die("wl_display_dispatch");

        if (pollfd->revents & POLLOUT) {
          pollfd->events = POLLIN;
          flush();
        }
      } else if (pollfd->fd == STDIN_FILENO && (pollfd->revents & POLLIN)) {
        on_stdin();
      } else if (pollfd->fd == fifo_fd      && (pollfd->revents & POLLIN)) {
        on_status();
      } else if (pollfd->fd == self_pipe[0] && (pollfd->revents & POLLIN)) {
        quitting = 1;
      }
    }
  }
}

void cleanup(void) {
  if (shm)
    wl_shm_destroy(shm);
  if (compositor)
    wl_compositor_destroy(compositor);
  if (base)
    xdg_wm_base_destroy(base);
  if (shell)
    zwlr_layer_shell_v1_destroy(shell);
  if (output_manager)
    zxdg_output_manager_v1_destroy(output_manager);
  if (fifo_path) {
    unlink(fifo_path);
    remove(fifo_path);
  }
  if (display)
    wl_display_disconnect(display);
}

int main(int argc, char *argv[]) {
  int opt; // Just char for parsing args
  while ((opt = getopt(argc, argv, "vh")) != -1) {
    switch (opt) {
    case 'h':
      printf("Usage: [-h] [-v]\n");
      printf("  -h: show this\n");
      printf("  -v: get version\n");
      exit(0);

    case 'v':
      printf("A Version");
      exit(0);
    }
  }

  setup();
  run();
  cleanup();

  return 0;
}

void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "error: ");
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
    fputc(' ', stderr);
    perror(NULL);

  } else {
    fputc('\n', stderr);
  }

  cleanup();
  exit(1);
}

void* ecalloc(size_t amnt, size_t size) {
  void *p;

  if (!(p = calloc(amnt, size))) {
    die("calloc did not allocate");
  }

  return p;
}
