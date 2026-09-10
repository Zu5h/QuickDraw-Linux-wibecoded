#!/bin/bash
set -e

cd "$(dirname "$0")"
gcc -O2 -o quickdraw main.c \
    $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1 json-glib-1.0)

DEST="${DESTDIR:-/app}"
install -Dm755 quickdraw "$DEST/bin/quickdraw"
install -Dm644 quickdraw.desktop "$DEST/share/applications/quickdraw.desktop"
mkdir -p "$DEST/share/quickdraw"
cp -r WebSrc "$DEST/share/quickdraw/WebSrc"
for size in 16 32 48 64 128 256; do
    install -Dm644 "share/icons/hicolor/${size}x${size}/apps/quickdraw.png" \
        "$DEST/share/icons/hicolor/${size}x${size}/apps/quickdraw.png"
done