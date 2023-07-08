#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <wayland-util.h>

#include "log.h"
#include "tray.h"
#include "host.h"
#include "item.h"
#include "util.h"
#include "main.h"

static int get_registered_items_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int handle_new_watcher(sd_bus_message *m, void *userdata, sd_bus_error *error);
static int handle_registered_item(sd_bus_message *m, void *userdata, sd_bus_error *error);
static int handle_unregistered_item(sd_bus_message *m, void *userdata, sd_bus_error *error);
static int register_to_watcher(struct Host *host);

int get_registered_items_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    if (sd_bus_message_is_method_error(m, NULL))
        panic("get_registered_items_callback sd_bus_message_is_method_error %s", sd_bus_message_get_error(m)->message);

    if (sd_bus_message_enter_container(m, 'v', NULL) < 0)
        panic("get_registered_items_callback sd_bus_message_enter_container");

    char **ids;
    if (sd_bus_message_read_strv(m, &ids) < 0)
        panic("get_registered_items_callback sd_bus_message_read_strv");

    if (ids) {
        struct Tray *tray = userdata;
        for (char **id = ids; *id; id++) {
            struct Item *item = item_create(tray, *id);
            wl_list_insert(&tray->items, &item->link);
            free(*id);
        }
    }
    free(ids);
    return 0;
}

int handle_new_watcher(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    char *service, *old_owner, *new_owner;
    if (sd_bus_message_read(m, "sss", &service, &old_owner, &new_owner) < 0)
        panic("handle_new_watcher sd_bus_message_read");
    if (*old_owner)
        return 0;

    struct Host *host = userdata;
    if (STRING_EQUAL(service, host->interface))
        register_to_watcher(host);

    return 0;
}

int handle_registered_item(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    char *id;
    if (sd_bus_message_read(m, "s", &id) < 0)
        panic("handle_registered_item sd_bus_message_read");

    struct Tray *tray = userdata;

    struct Item *item;
    wl_list_for_each(item, &tray->items, link) {
        if (STRING_EQUAL(item->watcher_id, id)) // If we already have this item.
            return 0;
    }

    item = item_create(tray, id);
    wl_list_insert(&tray->items, &item->link);

    return 0;
}

int handle_unregistered_item(sd_bus_message *m, void *userdata, sd_bus_error *error) {
    char *id;
    if (sd_bus_message_read(m, "s", &id) < 0)
        panic("sd_bus_message_read");

    struct Tray *tray = userdata;

    struct Item *item, *tmp;
    wl_list_for_each_safe(item, tmp, &tray->items, link) {
        if (!(STRING_EQUAL(item->watcher_id, id)))
            continue;

        wl_list_remove(&item->link);
        item_destroy(item);
        monitors_update();
        break;
    }

    return 0;
}

struct Host *host_create(char *protocol, struct Tray *tray) {
    if (!protocol || !tray)
        return NULL;

    struct Host *host = ecalloc(1, sizeof(*host));
    sd_bus_slot *register_slot = NULL, *unregister_slot = NULL, *watcher_slot = NULL;
    const char *error;

    host->interface = string_create("org.%s.StatusNotifierWatcher", protocol);
    host->tray = tray;

    if (sd_bus_match_signal(tray->bus, &register_slot, host->interface, watcher_path, host->interface,
                "StatusNotifierItemRegistered", handle_registered_item, tray) < 0) {
        error = "sd_bus_match_signal StatusNotifierItemRegistered protocol: %s";
        goto error;
    }

    if (sd_bus_match_signal(tray->bus, &unregister_slot, host->interface, watcher_path, host->interface,
                "StatusNotifierItemUnregistered", handle_unregistered_item, tray) < 0) {
        error = "sd_bus_match_signal StatusNotifierItemUnregistered protocol: %s";
        goto error;
    }

    if (sd_bus_match_signal(tray->bus, &watcher_slot, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                handle_new_watcher, host) < 0) {
        error = "sd_bus_match_signal NameOwnerChanged protocol: %s";
        goto error;
    }

    pid_t pid = getpid();
    host->name = string_create("org.%s.StatusNotifierHost-%d", protocol,  pid);

    if (!register_to_watcher(host)) {
        error = "register_to_watcher protocol: %s";
        goto error;
    }

    sd_bus_slot_set_floating(register_slot, 0);
    sd_bus_slot_set_floating(unregister_slot, 0);
    sd_bus_slot_set_floating(watcher_slot, 0);

    return host;

error:
    sd_bus_slot_unref(register_slot);
    sd_bus_slot_unref(unregister_slot);
    sd_bus_slot_unref(watcher_slot);
    host_destroy(host);
    panic(error, protocol);
    return NULL;
}

void host_destroy(struct Host *host) {
    if (!host) return;

    sd_bus_release_name(host->tray->bus, host->name);
    free(host->name);
    free(host->interface);
}

int register_to_watcher(struct Host *host) {
    if (sd_bus_call_method_async(host->tray->bus, NULL,
			host->interface, watcher_path, host->interface,
			"RegisterStatusNotifierHost", NULL, NULL, "s", host->name) < 0)
        return 0;

    if (sd_bus_call_method_async(host->tray->bus, NULL,
			host->interface, watcher_path,
			"org.freedesktop.DBus.Properties", "Get",
			get_registered_items_callback, host, "ss",
			host->interface, "RegisteredStatusNotifierItems") < 0)
        return 0;

    return 1;
}
