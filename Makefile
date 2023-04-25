##
# dwl-bar
#
# @file
# @version 0.0
VERSION    = 0.0
PKG_CONFIG = pkg-config
PKG_EXISTS = $(PKG_CONFIG) --exists
SD_BUS     = $(shell { $(PKG_EXISTS) libsystemd && echo "libsystemd"; } || { $(PKG_EXISTS) libelogind && echo "libelogind"; } || { $(PKG_EXISTS) basu && echo "basu"; } || exit 1; )

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
SRCDIR = src

PKGS   = wayland-client wayland-cursor pangocairo
PKGS  += $(SD_BUS)
FILES  = $(SRCDIR)/main.c $(SRCDIR)/main.h $(SRCDIR)/log.c $(SRCDIR)/log.h \
		 $(SRCDIR)/render.c $(SRCDIR)/render.h $(SRCDIR)/event.c $(SRCDIR)/event.h \
		 $(SRCDIR)/util.c $(SRCDIR)/util.h $(SRCDIR)/shm.c $(SRCDIR)/shm.h \
		 $(SRCDIR)/input.c $(SRCDIR)/input.h $(SRCDIR)/user.c $(SRCDIR)/user.h \
		 $(SRCDIR)/bar.c $(SRCDIR)/bar.h $(SRCDIR)/config.h
FILES += $(SRCDIR)/lib.h $(SRCDIR)/icon.c $(SRCDIR)/icon.h $(SRCDIR)/item.c $(SRCDIR)/item.h \
		 $(SRCDIR)/host.c $(SRCDIR)/host.h $(SRCDIR)/watcher.c $(SRCDIR)/watcher.h \
		 $(SRCDIR)/tray.c $(SRCDIR)/tray.h
OBJS   = $(SRCDIR)/xdg-output-unstable-v1-protocol.o $(SRCDIR)/xdg-shell-protocol.o \
		 $(SRCDIR)/wlr-layer-shell-unstable-v1-protocol.o

## Compile Flags
CC        = gcc
BARCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(CFLAGS)
BARLIBS   = `$(PKG_CONFIG) --libs $(PKGS)` $(LIBS)

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`


all: dwl-bar
dwl-bar: $(FILES) $(OBJS)
	$(CC) $^ $(BARLIBS) $(BARCFLAGS) -o $@
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/%.h
	$(CC) -c $< $(BARLIBS) $(BARCFLAGS) -o $@

$(SRCDIR)/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
$(SRCDIR)/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

$(SRCDIR)/xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
$(SRCDIR)/xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

$(SRCDIR)/wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
$(SRCDIR)/wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/wlr-layer-shell-unstable-v1.xml $@

$(SRCDIR)/lib.h:
	touch $(SRCDIR)/lib.h
	echo -e "#ifndef LIB_H_\n#define LIB_H_\n" > $(SRCDIR)/lib.h
	{ $(PKG_EXISTS) libsystemd && echo -e "#define SYSTEMD 1\n" | tee -a $(SRCDIR)/lib.h; } || echo -e "#define SYSTEMD 0\n" | tee -a $(SRCDIR)/lib.h;
	{ $(PKG_EXISTS) libelogind && echo -e "#define ELOGIND 1\n" | tee -a $(SRCDIR)/lib.h; } || echo -e "#define ELOGIND 0\n" | tee -a $(SRCDIR)/lib.h;
	{ $(PKG_EXISTS) basu 	   && echo -e "#define BASU 1\n"    | tee -a $(SRCDIR)/lib.h; } || echo -e "#define BASU 0\n" 	 | tee -a $(SRCDIR)/lib.h;
	echo "#endif // LIB_H_" | tee -a $(SRCDIR)/lib.h

$(SRCDIR)/config.h:
	cp src/config.def.h $@

dev: clean $(SRCDIR)/lib.h $(SRCDIR)/config.h $(OBJS)

clean:
	rm -f dwl-bar src/config.h src/lib.h src/*.o src/*-protocol.*

dist: clean
	mkdir -p dwl-bar-$(VERSION)
	cp -R LICENSE Makefile README.md dwl-bar.1 src protocols \
		dwl-bar-$(VERSION)
	tar -caf dwl-bar-$(VERSION).tar.gz dwl-bar-$(VERSION)
	rm -rf dwl-bar-$(VERSION)

install: dwl-bar
	mkdir -p $(PREFIX)/bin
	cp -f dwl-bar $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/dwl-bar
	mkdir -p $(MANDIR)/man1
	cp -f dwl-bar.1 $(MANDIR)/man1
	chmod 644 $(MANDIR)/man1/dwl-bar.1

uninstall:
	rm -f $(PREFIX)/bin/dwl-bar $(MANDIR)/man1/dwl-bar.1

# end
