#ifndef WATCHER_H_
#define WATCHER_H_

#include "tray.h"

// In case there isn't already a watcher available.
struct Watcher {
    char *interface;
    sd_bus *bus;
    int version;

    struct List *hosts, *items; /* char* */
};

struct Watcher *watcher_create(char *protocol, sd_bus *bus);
void watcher_destroy(struct Watcher *watcher);

#endif // WATCHER_H_
