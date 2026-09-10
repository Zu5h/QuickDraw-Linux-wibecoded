CC = gcc
PKG_DEPS = gtk+-3.0 webkit2gtk-4.1 json-glib-1.0
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKG_DEPS))
LIBS = $(shell pkg-config --libs $(PKG_DEPS))
TARGET = quickdraw

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/bin/$(TARGET)
	install -Dm644 quickdraw.desktop /usr/share/applications/quickdraw.desktop

uninstall:
	rm -f /usr/bin/$(TARGET)
	rm -f /usr/share/applications/quickdraw.desktop

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
