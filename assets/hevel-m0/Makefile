PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

CFLAGS = -O2 -std=c99 -Wall -Wextra
CFLAGS += -I$(PREFIX)/include
CFLAGS += `pkg-config --cflags swc wayland-server libinput pixman-1 xkbcommon libdrm wld libeis-1.0 libsystemd`

LDFLAGS = -L$(PREFIX)/lib -L$(PREFIX)/lib64 -Wl,-rpath,$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib64
LDLIBS += `pkg-config --libs swc wayland-server libinput pixman-1 xkbcommon libdrm libudev xcb xcb-composite xcb-ewmh xcb-icccm wld libeis-1.0 libsystemd`

all: hevel

hevel: hevel.o
	$(CC) $(LDFLAGS) -o hevel hevel.o $(LDLIBS)

hevel.o: src/hevel.c
	$(CC) $(CFLAGS) -c src/hevel.c

clean:
	rm -f hevel hevel.o

install: hevel
	install -D -m 755 hevel $(DESTDIR)$(BINDIR)/hevel

.PHONY: clean install FORCE
