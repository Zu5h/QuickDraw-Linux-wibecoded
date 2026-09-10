#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"
APPDIR="$BUILD_DIR/AppDir"
ARCH="$(uname -m)"

echo "=== QuickDraw AppImage Builder ==="
echo "Architecture: $ARCH"

rm -rf "$BUILD_DIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/quickdraw" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

echo "--- Building quickdraw ---"
cd "$SCRIPT_DIR"
make clean && make

cp "$SCRIPT_DIR/quickdraw" "$APPDIR/usr/bin/quickdraw"
cp -r "$PROJECT_DIR/WebSrc" "$APPDIR/usr/share/quickdraw/WebSrc"
cp "$SCRIPT_DIR/share/applications/quickdraw.desktop" "$APPDIR/usr/share/applications/quickdraw.desktop"

for size in 16 32 48 64 128 256; do
    mkdir -p "$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps"
    cp "$SCRIPT_DIR/share/icons/hicolor/${size}x${size}/apps/quickdraw.png" \
       "$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps/quickdraw.png" 2>/dev/null || true
done

cp "$SCRIPT_DIR/share/icons/hicolor/256x256/apps/quickdraw.png" "$APPDIR/quickdraw.png"
cp "$SCRIPT_DIR/share/applications/quickdraw.desktop" "$APPDIR/quickdraw.desktop"

cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin/:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib/:${LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share/:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GDK_BACKEND=x11,wayland
exec "${HERE}/usr/bin/quickdraw" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

echo "--- Bundling dependencies with linuxdeploy ---"

LINUXDEPLOY="$BUILD_DIR/linuxdeploy"
if [ ! -f "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -L -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

# Bundle GTK shared resources manually
echo "--- Bundling GTK resources ---"
for GDIR in /usr/lib/gtk-3.0 /usr/share/gtk-3.0 /usr/lib/gdk-pixbuf-2.0 /usr/share/glib-2.0/schemas /usr/share/icons/hicolor; do
    if [ -d "$GDIR" ]; then
        DEST="$APPDIR$GDIR"
        mkdir -p "$(dirname "$DEST")"
        cp -rL "$GDIR" "$DEST" 2>/dev/null || true
    fi
done
for LDIR in /usr/lib/gdk-birling-3.0 /usr/lib/gio/modules; do
    if [ -d "$LDIR" ]; then
        DEST="$APPDIR$LDIR"
        mkdir -p "$(dirname "$DEST")"
        cp -rL "$LDIR" "$DEST" 2>/dev/null || true
    fi
done

# Regenerate pixbuf loaders cache
if [ -f "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" ]; then
    sed -i "s|$APPDIR||g" "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" 2>/dev/null || true
fi

# Run linuxdeploy WITHOUT --plugin gtk (we did it manually)
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/quickdraw.desktop" \
    --icon-file "$APPDIR/quickdraw.png" \
    --output appimage

APPIMAGE_NAME=$(ls -1 "$BUILD_DIR"/QuickDraw-*.AppImage 2>/dev/null | head -1)
if [ -n "$APPIMAGE_NAME" ]; then
    mv "$APPIMAGE_NAME" "$PROJECT_DIR/QuickDraw-$ARCH.AppImage"
    echo ""
    echo "=== Done! ==="
    echo "AppImage: $PROJECT_DIR/QuickDraw-$ARCH.AppImage"
    ls -lh "$PROJECT_DIR/QuickDraw-$ARCH.AppImage"
else
    echo "ERROR: AppImage not found in $BUILD_DIR"
    ls -la "$BUILD_DIR"
fi
