# dwl-bar
dwm-like bar for dwl

I believe dwl-bar provides a more dwm-like experience out of the box than other bars like somebar.

## Dependencies
I'm not sure what the package names will be for your distrobution, so just make sure these are generally what you have.
 + make
 + pango
 + cairo
 + wayland
 + wayland-protocols

## Compile
Compile with `make`, install with `make install`, uninstall `make uninstall`.

## Configuration
Like most suckless-like software, configuration is done through `src/config.def.h` modify it to your heart's content. dwl-bar is compatible with [someblocks](https://sr.ht/~raphi/someblocks/) for status.

If you want dwl-bar to control dwl (via mouse usage primarily) you may want to apply the `patches/dwl-bar-ipc-1.0.diff` patch. 
**However do note that you will also need to apply the [bar-ipc](https://github.com/djpohly/dwl/wiki/bar-ipc) patch for dwl.**

For more information about patching see the [hacking](https://suckless.org/hacking/) article from suckless.

## Thanks
Thanks to raphi for somebar this project is largely just somebar but in C and a few tweaks to make it similar to dwm. The ipc protocol is also just the ipc patch in somebar's `contrib/`.
