CC     = gcc
PKGS   = gtk+-3.0 gtk-layer-shell-0
CFLAGS = $(shell pkg-config --cflags $(PKGS)) -O2 -Wall -Wextra
LIBS   = $(shell pkg-config --libs $(PKGS)) -lm

crosshair: src/crosshair.c
	$(CC) -o crosshair src/crosshair.c $(CFLAGS) $(LIBS)

clean:
	rm -f crosshair
