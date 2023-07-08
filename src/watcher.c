#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "log.h"
#include "tray.h"
#include "watcher.h"
#include "util.h"
#include "main.h"

static int get_registered_items(sd_bus *bus, const char *path, const char *interface, const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error);
static int handle_lost_service(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int is_registered_host(sd_bus *bus, const char *path, const char *interface, const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error);
static int register_host(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int register_item(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int watcher_using_freedesktop(struct Watcher *watcher);

static const sd_bus_vtable watcher_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", register_item,
            SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", register_host,
            SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", get_registered_items,
            0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", is_registered_host,
            0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ProtocolVersion", "i", NULL, offsetof(struct Watcher, version),
            SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", NULL, 0),
    SD_BUS_VTABLE_END,
};

int get_registered_items(sd_bus *bus, const char *path, const char *interface,
        const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    struct Watcher *watcher = userdata;
    list_add(watcher->items, NULL); // Must be NULL terminated array.
    int ret = sd_bus_message_append_strv(reply, (char**)watcher->items->data);
    list_remove(watcher->items, watcher->items->length - 1);
    return ret;
}

int handle_lost_service(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    char *service, *old_owner, *new_owner;
    if (sd_bus_message_read(m, "sss", &service, &new_owner, &new_owner) < 0)
        panic("handle_lost_service sd_bus_message_read");

    struct Watcher *watcher = userdata;

    if (*new_owner)
        return 0;

    int i;
    char *id;
    for (i = 0; i < watcher->items->length; i++) { // Check to see if item.
        id = watcher->items->data[i];

        int same_service = (watcher_using_freedesktop(watcher) ? strcmp(id, service) :
                    strncmp(id, service, strlen(service))) == 0;
        if (!same_service)
            continue;

        list_remove(watcher->items, i--);
        sd_bus_emit_signal(watcher->bus, watcher_path, watcher->interface,
                "StatusNotifierItemUnregistered", "s", id);
        free(id);
        if (watcher_using_freedesktop(watcher))
            break;
    }

    if ((i = list_cmp_find(watcher->hosts, service, cmp_id)) != -1) // If not item then host.
        free(list_remove(watcher->hosts, i));

    return 0;
}

int is_registered_host(sd_bus *bus, const char *path, const char *interface,
        const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    struct Watcher *watcher = userdata;
    int has = watcher->hosts->length > 0;
    return sd_bus_message_append_basic(reply, 'b', &has);
}

int register_host(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    char *service;
    if (sd_bus_message_read(m, "s", &service) < 0)
        panic("register_host sd_bus_message_read");

    struct Watcher *watcher = userdata;
    if (list_cmp_find(watcher->hosts, service, cmp_id) == -1) {
        list_add(watcher->hosts, strdup(service));
        sd_bus_emit_signal(watcher->bus, watcher_path, watcher->interface,
                "StatusNotifierHostRegistered", NULL);
    }

    return sd_bus_reply_method_return(m, "");
}


int register_item(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    char *service_path, *id;
    if (sd_bus_message_read(m, "s", &service_path) < 0)
        panic("register_item sd_bus_message_read");

    struct Watcher *watcher = userdata;
    if (watcher_using_freedesktop(watcher)) {
        id = strdup(service_path);
    } else {
        const char *service, *path;
        if (service_path[0] == '/') {
            service = sd_bus_message_get_sender(m);
            path = service_path;
        } else {
            service = service_path;
            path = "/StatusNotifierItem";
        }
        id = string_create("%s%s", service, path);
    }

    if (list_cmp_find(watcher->items, id, cmp_id) != -1) {
        free(id);
    } else {
        list_add(watcher->items, id);
        sd_bus_emit_signal(watcher->bus, watcher_path, watcher->interface,
                "StatusNotifierItemRegistered", "s", id);
    }

    return sd_bus_reply_method_return(m, "");
}

struct Watcher *watcher_create(char *protocol, sd_bus *bus) {
    if (!protocol || !bus)
        return NULL;

    struct Watcher *watcher = ecalloc(1, sizeof(*watcher));
    sd_bus_slot *signal = NULL, *vtable = NULL;
    watcher->interface = string_create("org.%s.StatusNotifierWatcher", protocol);

    if (sd_bus_add_object_vtable(bus, &vtable, watcher_path,
                watcher->interface, watcher_vtable, watcher) < 0)
        goto error;

    if (sd_bus_match_signal(bus, &signal, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "NameOwnerChanged", handle_lost_service, watcher) < 0)
        goto error;

    if (sd_bus_request_name(bus, watcher->interface, 0) < 0)
        goto error;

    sd_bus_slot_set_floating(signal, 0);
    sd_bus_slot_set_floating(vtable, 0);

    watcher->bus   = bus;
    watcher->hosts = list_create(0);
    watcher->items = list_create(0);

    return watcher;

error:
    sd_bus_slot_unref(vtable);
    sd_bus_slot_unref(signal);
    watcher_destroy(watcher);
    panic("Creating %s watcher failed dying.", protocol);
    return NULL;
}

void watcher_destroy(struct Watcher *watcher) {
    if (!watcher) return;

    list_elements_destroy(watcher->hosts, free);
    list_elements_destroy(watcher->items, free);
    free(watcher->interface);
    free(watcher);
}

int watcher_using_freedesktop(struct Watcher *watcher) {
    return watcher->interface[strlen("org.")] == 'f';
}
