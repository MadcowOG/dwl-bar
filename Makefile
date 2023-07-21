##
# dwl-bar
#
# @file
# @version 0.0
VERSION    = 0.0
PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = ${PREFIX}/share/man
SRC = src

PKGS   = wayland-client wayland-server wayland-cursor fcft pixman-1
OBJS   = ${addprefix ${SRC}/, \
		 xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o dwl-bar.o log.o\
		 }

## Compile Flags
CC      = gcc
CFLAGS += `${PKG_CONFIG} --cflags ${PKGS}` -I/usr/include/pixman-1 -Wall -g
LIBS   += `${PKG_CONFIG} --libs ${PKGS}`

WAYLAND_SCANNER   = `${PKG_CONFIG} --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `${PKG_CONFIG} --variable=pkgdatadir wayland-protocols`

all: dwl-bar
dwl-bar: ${OBJS}
	${CC} $^ ${LIBS} ${CFLAGS} -o $@
${SRC}/dwl-bar.o: ${SRC}/config.h
${SRC}/log.o: ${SRC}/log.h
${SRC}/%-protocol.o: ${SRC}/%-protocol.c ${SRC}/%-protocol.h
	${CC} -c $< -o $@

${SRC}/xdg-shell-protocol.h:
	${WAYLAND_SCANNER} client-header \
		${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml $@
${SRC}/xdg-shell-protocol.c:
	${WAYLAND_SCANNER} private-code \
		${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml $@

${SRC}/wlr-layer-shell-unstable-v1-protocol.h:
	${WAYLAND_SCANNER} client-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
${SRC}/wlr-layer-shell-unstable-v1-protocol.c:
	${WAYLAND_SCANNER} private-code \
		protocols/wlr-layer-shell-unstable-v1.xml $@

${SRC}/config.h:
	cp src/config.def.h $@

dev: clean ${SRC}/config.h ${OBJS}

clean:
	rm -f dwl-bar dwl-bar-${VERSION}.tar.gz src/config.h src/*.o src/*-protocol.*

dist: clean
	mkdir -p dwl-bar-${VERSION}
	cp -R LICENSE Makefile README.md dwl-bar.1 src protocols \
		dwl-bar-${VERSION}
	tar -caf dwl-bar-${VERSION}.tar.gz dwl-bar-${VERSION}
	rm -rf dwl-bar-${VERSION}

install: dwl-bar
	mkdir -p ${PREFIX}/bin
	cp -f dwl-bar ${PREFIX}/bin
	chmod 755 ${PREFIX}/bin/dwl-bar
	mkdir -p ${MANDIR}/man1
	cp -f dwl-bar.1 ${MANDIR}/man1
	chmod 644 ${MANDIR}/man1/dwl-bar.1

uninstall:
	rm -f ${PREFIX}/bin/dwl-bar ${MANDIR}/man1/dwl-bar.1

# end
