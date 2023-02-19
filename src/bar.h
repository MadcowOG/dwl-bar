#ifndef BAR_H_
#define BAR_H_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <stdint.h>
#include <sys/types.h>
#include <wayland-client-protocol.h>
#include "common.h"

typedef struct Bar Bar;

enum TagState {
  None   = 0,
  Active = 1,
  Urgent = 2,
};

#define TAG_INACTIVE None
#define TAG_ACTIVE Active
#define TAG_URGENT Urgent

Bar* bar_create(void);
void bar_destroy(Bar* bar);
void bar_invalidate(Bar* bar);
void bar_show(Bar* bar, wl_output* output);
int bar_is_visible(Bar* bar);
void bar_click(Bar* bar, struct Monitor* monitor, int x, int y, uint32_t button);
void bar_set_status(Bar* bar, const char* text);
void bar_set_title(Bar* bar, const char* text);
void bar_set_layout(Bar* bar, const char* text);
void bar_set_active(Bar* bar, uint is_active);
void bar_set_floating(Bar* bar, uint is_floating);
void bar_set_tag(Bar* bar, uint tag, uint state, uint occupied, uint focusedClient);
wl_surface* bar_get_surface(Bar* bar);

#endif // BAR_H_
