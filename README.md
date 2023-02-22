# dwl-bar
dwm-like bar for dwl
I believe dwl-bar provides a more dwm-like experience out of the box than other bars like somebar.

Still in development. But should compile.

## Dependencies
I'm not sure what the package names will be for your distrobution, so just make sure these are generally what you have.
 + pango
 + cairo
 + wayland
 + wayland-protocols

## Compile
Compile with this command `gcc ./src/*.c -o ./bar $(pkg-config --cflags --libs wayland-client wayland-cursor pangocairo)`.

## Configuration
Like most suckless-like software, configuration is done through `src/config.def.h` modify it to your heart's content.

## Thanks
Thanks to raphi for somebar this project is largely just somebar but in C and a few tweaks to make it similar to dwm. The ipc protocol is also just the ipc patch in somebar's `contrib/`.
