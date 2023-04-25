#include "icon.h"
#include "util.h"
#include "log.h"
#include <bits/pthreadtypes.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-util.h>

static void add_dir(struct List *list, char *dir);
static int basedir_has_theme(char *theme, char *dir);
static int dir_exists(char *path);
static char *entry_handler(char *group, char *key, char *value, struct Theme *theme);
static void get_basedirs(struct List *basedirs);
static char *get_fallback_icon(struct List *themes, struct List *basedirs, char *name);
static char *group_handler(char *last_group, char *new_group, struct Theme *theme);
static void load_themes(struct List *themes, char *basedir);
static struct Theme *read_theme_file(char *dir, char *name);
static struct List *split_string(const char *str, const char *delim);
static char *subdir_get_icon(char *name, char *basedir, char *theme, char *subdir);
static void theme_destroy(struct Theme *theme);
static void theme_element_destroy(void *ptr);
static char *theme_get_icon(struct List *themes, struct List *basedirs, char *name, int size, const char *theme_name);
static int theme_name_compare(const void *left, const void *right);

void add_dir(struct List *list, char *dir) {
    // If the directory doesn't exist then free it and don't think about it.
    if (!dir_exists(dir)) {
        free(dir);
        return;
    }

    list_add(list, dir);
}

int basedir_has_theme(char *theme, char *dir) {
    char *path = string_create("%s/%s", dir, theme);
    int exist = dir_exists(path);
    free(path);
    return exist;
}

int dir_exists(char *path) {
    struct stat sb;
    return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

// Return error as string.
char *entry_handler(char *group, char *key, char *value, struct Theme *theme) {
    struct List *temp = NULL;

    if (STRING_EQUAL(group, "Icon Theme")) {
        if (STRING_EQUAL(key, "Name")) {
            theme->name = strdup(value);
        } else if (STRING_EQUAL(key, "Comment")) {
            theme->comment = strdup(value);
        } else if (STRING_EQUAL(key, "Inherits")) {
            temp = split_string(value, ","); // This only happens once
            list_copy(theme->inherits, temp);
        } else if (STRING_EQUAL(key, "Directories")) {
            temp = split_string(value, ","); // This only happens once
            list_copy(theme->directories, temp);
        }

        if (temp)
            list_destroy(temp);
    } else {
        if (theme->subdirectories->length == 0)
            return NULL;

        struct Subdir *subdir = theme->subdirectories->data[theme->subdirectories->length-1];
        if (strcmp(subdir->name, group) != 0) // Skip
            return NULL;

        if (STRING_EQUAL(key, "Context")) {
            return NULL; // Ignore
        } else if (STRING_EQUAL(key, "Type"))  {
            if (STRING_EQUAL(value, "Fixed")) {
                subdir->type = FIXED;
            } else if (STRING_EQUAL(value, "Scalable")) {
                subdir->type = SCALABLE;
            } else if (STRING_EQUAL(value, "Threshold")) {
                subdir->type = THRESHOLD;
            } else {
                return "invalid value - expected 'Fixed', 'Scalable' or 'Threshold'.";
            }
            return NULL;
        }

        char *end;
        long n = strtol(value, &end, 10);
        if (*end != '\0')
            return "invalid value - expected a number";

        if (STRING_EQUAL(key, "Size")) {
            subdir->size = n;
        } else if (STRING_EQUAL(key, "MaxSize")) {
            subdir->max_size = n;
        } else if (STRING_EQUAL(key, "MinSize")) {
            subdir->min_size = n;
        } else if (STRING_EQUAL(key, "Threshold")) {
            subdir->threshold = n;
        } // Ignore scale.
    }

    return NULL;
}

void get_basedirs(struct List *basedirs) {
    size_t len;
    char *home_icons, *data_home_icons,
         *dir, *path, *data_dirs;
    const char *data_home, *home;

    list_add(basedirs, strdup("/usr/share/pixmaps"));

    // Create and add home data icons dir to array.
    home = getenv("HOME");
    data_home = getenv("XDG_DATA_HOME");
    char *format = "%s/icons";
    if (!(data_home && *data_home)) {
        data_home = home;
        format = "%s/.local/share/icons";
    }
    data_home_icons = string_create(format, data_home);

    add_dir(basedirs, data_home_icons);

    // Create and add data dirs to array.
    data_dirs = getenv("XDG_DATA_DIRS");
    if (!(data_dirs && *data_dirs))
        data_dirs = "/usr/local/share:/usr/share";
    data_dirs = strdup(data_dirs);

    dir = strtok(data_dirs, ":");
    do {
        path = string_create("%s/icons", dir);
        add_dir(basedirs, path);
    } while ((dir = strtok(NULL, ":")));
    free(data_dirs);
}

char *get_fallback_icon(struct List *themes, struct List *basedirs, char *name) {
    char *basedir, *icon = NULL;
    struct Theme *theme;
    struct Subdir *subdir;
    int i;
    // Do it backwards because we only really need to do this for the icon_theme_path of an item.
    for (i = basedirs->length-1; i >= 0; i--) {
        basedir = basedirs->data[i];
        icon = subdir_get_icon(name, basedir, "", "");
        if (icon)
            return icon;
    }

    for (i = 0; i < basedirs->length; i++) {
        basedir = basedirs->data[i];
        for (int n = 0; n < themes->length; n++) {
            theme = themes->data[n];
            for (int x = theme->subdirectories->length-1; x >= 0; x--) {
                subdir = theme->subdirectories->data[x];
                icon = subdir_get_icon(name, basedir, theme->dir, subdir->name);
                if (icon)
                    return icon;
            }
        }
    }

    return NULL;
}

char *get_icon(struct List *themes, struct List *basedirs, char *name, int size, const char *theme) {
    if (!themes || !basedirs || !name || !theme)
        return NULL;

    char *icon = NULL;
    if (theme) {
        icon = theme_get_icon(themes, basedirs, name, size, theme);
    }
    if (!icon && !(theme && STRING_EQUAL(theme, "Hicolor"))) {
        icon = theme_get_icon(themes, basedirs, name, size, "Hicolor");
    }
    if (!icon) {
        icon = get_fallback_icon(themes, basedirs, name);
    }

    return icon;
}

// Return error as string.
char *group_handler(char *last_group, char *new_group, struct Theme *theme) {
    if (!last_group)
        return new_group && STRING_EQUAL(new_group, "Icon Theme") ? NULL
            : "first group must be 'Icon Theme'";

    if (STRING_EQUAL(last_group, "Icon Theme")) {
        if (!theme->name) {
            return "missing required key 'Name'";
        } else if (!theme->comment) {
            return "missing required key 'Comment'";
        } else if (theme->directories->length == 0) {
            return "missing required key 'Directories'";
        } else {
            for (char *c = theme->name; *c; c++)
                if (*c == ',' || *c == ' ')
                    return "malformed theme name";
        }
    } else {
        if (theme->subdirectories->length == 0)
            return NULL;

        struct Subdir *subdir = theme->subdirectories->data[theme->subdirectories->length-1];
        if (!subdir->size)
            return "missing required key 'Size'";

        switch (subdir->type) {
            case FIXED:
                subdir->max_size = subdir->min_size = subdir->size;
                break;

            case SCALABLE:
                if (!subdir->max_size) subdir->max_size = subdir->size;
                if (!subdir->min_size) subdir->min_size = subdir->size;
                break;

            case THRESHOLD:
                subdir->max_size = subdir->size + subdir->threshold;
                subdir->min_size = subdir->size - subdir->threshold;
        }
    }

    if (!new_group)
        return NULL;

    if (list_cmp_find(theme->directories, new_group, cmp_id) != -1) {
        struct Subdir *subdir = list_add(theme->subdirectories, ecalloc(1, sizeof(*subdir)));
        subdir->name = strdup(new_group);
        subdir->threshold = 2;
    }

    return NULL;
}

void load_themes(struct List *themes, char *basedir) {
    DIR *dir;
    if (!(dir = opendir(basedir)))
        return;

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        struct Theme *theme = read_theme_file(basedir, entry->d_name);
        if (theme)
            list_add(themes, theme);
    }

    struct Theme *theme;
    struct Subdir *subdir;
    for (int i = 0; i < themes->length; i++) {
        theme = themes->data[i];
        if (!theme) continue;
        for (int n = 0; n < theme->subdirectories->length; n++) {
            subdir = theme->subdirectories->data[n];
            if (!subdir) continue;
        }
    }

    closedir(dir);
}

struct Theme *read_theme_file(char *dir, char *name) {
    char *path = string_create("%s/%s/index.theme", dir, name);
    FILE *index_theme = fopen(path, "r");
    free(path);
    if (!index_theme)
        return NULL;

    struct Theme *theme = ecalloc(1, sizeof(*theme));
    struct List *groups = list_create(0); // char*
    theme->inherits = list_create(0);
    theme->directories = list_create(0);
    theme->subdirectories = list_create(0);

    const char *error = NULL;
    char *full_line = NULL;
    int line_no = 0;
    size_t full_len = 0;
    ssize_t nread;
    while ((nread = getline(&full_line, &full_len, index_theme)) > 0) {
        line_no++;

        char *line = full_line - 1;
        while (isspace(*++line)); // Remove whitespace
        if (!*line || line[0] == '#') continue; // Ignore blank or comments

        int len = nread - (line - full_line);
        while (isspace(line[--len])); // Remove whitespace
        line[++len] = '\0';

        if (line[0] == '[') { // group header
            int i = 1;
            for (; !iscntrl(line[i]) && line[i] != '[' && line[i] != ']'; i++);
            if (i != --len || line[i] != ']') {
                error = "malformed group header";
                goto error;
            }

            line[len] = '\0';

            // check for duplicate groups
            if (list_cmp_find(groups, &line[1], cmp_id) != -1) {
                error = "duplicate group";
                goto error;
            }

            // Handler
            char *last_group = groups->length != 0 ? groups->data[groups->length-1] : NULL;
            error = group_handler(last_group, &line[1], theme);
            if (error)
                goto error;

            list_add(groups, strdup(&line[1]));

        } else {
            if (!groups->length) { // If empty
                error = "unexpected content before first header";
                goto error;
            }

            int eok = 0;
            for (; isalnum(line[eok]) || line[eok] == '-'; eok++);


            int i = eok - 1;
            while (isspace(line[++i]));
            if (line[i] != '=') {
                error = "malformed key-value pair";
                goto error;
            }

            line[eok] = '\0';
            char *value = &line[i];
            while(isspace(*++value));

            error = entry_handler(groups->data[groups->length-1], line, value, theme);
            if (error)
                goto error;
        }
    }

    if (groups->length) { // Not empty
        error = group_handler(groups->data[groups->length-1], NULL, theme);
    } else {
        error = "empty file";
    }

    if (!error)
        theme->dir = strdup(name);
error:

    if (error) {
        theme_destroy(theme);
        theme = NULL;
    }

    list_elements_destroy(groups, free);
    free(full_line);
    fclose(index_theme);
    return theme;
}

struct List *split_string(const char *str, const char *delim) {
    struct List *list = list_create(0);
    char *copy = strdup(str),
         *token = strtok(copy, delim);

    while (token) {
        list_add(list, strdup(token));
        token = strtok(NULL, delim);
    }
    free(copy);
    return list;
}

// Only support png cause I don't want to add another dependency lol.
char *subdir_get_icon(char *name, char *basedir, char *theme, char *subdir) {
    // So that we don't include a / if the theme or subdir doesn't contain anything.
    // So the icon path is usable.
    theme = string_create(strlen(theme) ? "%s/" : "%s", theme);
    subdir = string_create(strlen(subdir) ? "%s/" : "%s", subdir);

    char *path = string_create("%s/%s%s%s.png", basedir, theme, subdir, name);

    free(theme);
    free(subdir);

    if (access(path, R_OK) == 0)
        return path;

    free(path);
    return NULL;
}

void themes_create(struct List *themes, struct List *basedirs) {
    if (!themes || !basedirs)
        return;

    get_basedirs(basedirs); // char**

    char *basedir;
    for (int i = 0; i < basedirs->length; i++) {
        basedir = basedirs->data[i];
        load_themes(themes, basedir); // struct Theme**
    }
}

void theme_destroy(struct Theme *theme) {
    if (!theme) return;

    free(theme->name);
    free(theme->comment);
    free(theme->dir);

    list_elements_destroy(theme->inherits, free);
    list_elements_destroy(theme->directories, free);
    struct Subdir *subdir;
    for (int i = 0; i < theme->subdirectories->length; i++) {
        subdir = theme->subdirectories->data[i];
        free(subdir->name);
        free(subdir);
    }
    list_destroy(theme->subdirectories);

    free(theme);
}

void theme_element_destroy(void *ptr) {
    theme_destroy((struct Theme *)ptr);
}

void themes_destroy(struct List *themes, struct List *basedirs) {
    if (!themes || !basedirs)
        return;

    list_elements_destroy(basedirs, free);
    list_elements_destroy(themes, theme_element_destroy);
}

char *theme_get_icon(struct List *themes, struct List *basedirs, char *name, int size, const char *theme_name) {
    // Find theme
    int pos = 0;
    struct Theme *theme;
    if ((pos = list_cmp_find(themes, theme_name, theme_name_compare)) == -1) { // The desired theme isn't available.
        return NULL;
    } else {
        theme = themes->data[pos];
    }

    char *icon = NULL, *basedir;
    for (int i = 0; i < basedirs->length; i++) {
        basedir = basedirs->data[i];
        struct Subdir *subdir;
        for (int n = theme->subdirectories->length-1; n >= 0; n--) {
            subdir = theme->subdirectories->data[n];
            if (size < subdir->min_size || size > subdir->max_size ||
                    !(icon = subdir_get_icon(name, basedir, theme->dir, subdir->name)))
                continue;

            return icon;
        }
    }
    // find an inexact but close match.
    uint smallest_error = -1; // The max num uint can store
    for (int i = 0; i < basedirs->length; i++) {
        basedir = basedirs->data[i];
        if (!basedir_has_theme(theme->dir, basedir))
            continue;

        struct Subdir *subdir;
        for (int n = theme->subdirectories->length-1; n >= 0; n--) {
            subdir = theme->subdirectories->data[n];
            uint error = (size > subdir->max_size ? size - subdir->max_size : 0)
                + (size < subdir->min_size ? subdir->min_size - size : 0);
            if (error < smallest_error) {
                char *test = subdir_get_icon(name, basedir, theme->dir, subdir->name);
                if (!test)
                    continue;
                icon = test;
                smallest_error = error;
            }
        }
    }

    if (icon || theme->inherits->length == 0)
        return icon;

    // If we get here then we couldn't find an icon, so check the inherited themes. If there are any.
    char *inherit;
    for (int i = 0; i < theme->inherits->length; i++) {
        inherit = theme->inherits->data[i];
        icon = theme_get_icon(themes, basedirs, name, size, inherit);
        if (icon)
            break;
    }

    return icon;
}

int theme_name_compare(const void *left, const void *right) {
    const char *theme_name = left;
    const struct Theme *theme = right;

    return strcmp(theme_name, theme->name);
}
