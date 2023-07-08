#ifndef ICON_H_
#define ICON_H_

#include "tray.h"

struct Subdir {
    char *name;
    int size, max_size, min_size, threshold;

    enum {
        THRESHOLD,
        SCALABLE,
        FIXED
    } type;
};

struct Theme {
    char *name, *comment, *dir;
    struct List *inherits /* char* */,
                *directories /* char* */,
                *subdirectories /* struct Subdir* */;
};

char *get_icon(struct List *themes, struct List *basedirs,
        char *name, int size, const char *theme);
void themes_create(struct List *themes, struct List *basedirs);
void themes_destroy(struct List *themes, struct List *basedirs);

#endif // ICON_H_
