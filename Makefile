##
# dwl-bar
#
# @file
# @version 1.0
.POSIX:
.SUFFIXES:

VERSION    = 1.0
PKG_CONFIG = pkg-config

#paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man

# Compile flags
CC 		  = gcc
PKGS      = wayland-client wayland-cursor pangocairo
BARCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(CFLAGS)
BARLIBS   = `$(PKG_CONFIG) --libs $(PKGS)` $(LIBS)

# Wayland-scanner
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

srcdir := src

all: dwl-bar
dwl-bar: $(srcdir)/xdg-shell-protocol.o $(srcdir)/xdg-output-unstable-v1-protocol.o $(srcdir)/wlr-layer-shell-unstable-v1-protocol.o $(srcdir)/main.c $(srcdir)/bar.c $(srcdir)/shm.c $(srcdir)/config.h
	$(CC) $^ $(BARLIBS) $(BARCFLAGS) -o $@
$(srcdir)/xdg-shell-protocol.o: $(srcdir)/xdg-shell-protocol.c $(srcdir)/xdg-shell-protocol.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@
$(srcdir)/xdg-output-unstable-v1-protocol.o: $(srcdir)/xdg-output-unstable-v1-protocol.c $(srcdir)/xdg-output-unstable-v1-protocol.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@
$(srcdir)/wlr-layer-shell-unstable-v1-protocol.o: $(srcdir)/wlr-layer-shell-unstable-v1-protocol.c $(srcdir)/wlr-layer-shell-unstable-v1-protocol.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@

$(srcdir)/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
$(srcdir)/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

$(srcdir)/xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
$(srcdir)/xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

$(srcdir)/wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
$(srcdir)/wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/wlr-layer-shell-unstable-v1.xml $@

$(srcdir)/config.h:
	cp config.def.h $@

clean:
	rm -f dwl-bar src/*.o src/*-protocol.*

dist: clean
	mkdir -p dwl-bar-$(VERSION)
	cp -R LICENSE Makefile README.md src patches protocols \
		dwl-bar-$(VERSION)
	tar -caf dwl-bar-$(VERSION).tar.gz dwl-bar-$(VERSION)
	rm -rf dwl-bar-$(VERSION)

install: dwl-bar
	mkdir -p $(PREFIX)/bin
	cp -f dwl-bar $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/dwl-bar
	mkdir -p $(PREFIX)/man1
	cp -f dwl-bar.1 $(MANDIR)/man1
	chmod 644 $(MANDIR)/man1/dwl-bar.1

uninstall:
	rm -f $(PREFIX)/bin/dwl-bar $(MANDIR)/man1/dwl-bar.1

# end
