#ifndef CONFIG_H_
#define CONFIG_H_

static const bool bar_top = true;          /* If not top then bottom */
static const bool status_on_active = true; /* Display the status on active monitor only. If not then on all. */
static const char *fonts[] = {"monospace:size=10"};
static const char *terminal[] = { "alacritty", NULL };

/*
 * Colors:
 * Colors are in rgba format.
 * The color scheming format is the same as dwm.
 * We use an enum as a index for our scheme types.
 *
 * cyan  - used in an active background
 * grey3 - used in active text and urgent background
 * grey1 - used in an inactive background
 * grey2 - used in inactive text
 */
static const pixman_color_t cyan  = { .red = 0,   .green = 85,  .blue = 119, .alpha = 255 };
static const pixman_color_t grey1 = { .red = 34,  .green = 34,  .blue = 34,  .alpha = 255 };
static const pixman_color_t grey2 = { .red = 187, .green = 187, .blue = 187, .alpha = 255 };
static const pixman_color_t grey3 = { .red = 238, .green = 238, .blue = 238, .alpha = 255 };

static const pixman_color_t schemes[3][2] = {
    /* Scheme Type       fg,    bg */
    [inactive_scheme] = {grey2, grey1},
    [active_scheme]   = {grey3, cyan},
    [urgent_scheme]   = {grey1, grey3},
};

/*
 * Tags
 * Must not exceed 31 tags and amount must match dwl's tagcount.
 */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/*
 * Buttons
 * See user.h for details on relevant structures.
 */
static const struct binding bindings[] = {
    /* Click Location,    button,        callback,     bypass,    arguments */
    {  click_status,      BTN_MIDDLE,    spawn,        false,     {.v = terminal } },
};

#endif // CONFIG_H_
