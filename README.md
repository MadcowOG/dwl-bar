# dwl-bar
dwm-like bar for dwl

Still in development. But should compile.

## Compile
Compile with this command `gcc ./src/*.c -o ./bar $(pkg-config --cflags --libs wayland-client wayland-cursor pangocairo)`.

## Dependencies
I'm not sure what the package names will be for your distrobution, so just make sure these are generally what you have.
 + pango
 + cairo
 + wayland
 + wayland-protocols
