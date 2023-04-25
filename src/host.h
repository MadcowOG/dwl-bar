#ifndef HOST_H_
#define HOST_H_

#include "tray.h"

struct Host {
    struct Tray *tray;
    char *name;
    char *interface;
};

struct Host *host_create(char *protocol, struct Tray *tray);
void host_destroy(struct Host *host);

#endif // HOST_H_
