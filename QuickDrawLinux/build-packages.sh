#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
STAGE="/tmp/opencode/quickdraw-pkg/root"
VERSION="1.0.0"
ARCH="x86_64"

echo "=== QuickDraw package builder (.deb + .rpm) ==="

rm -rf "/tmp/opencode/quickdraw-pkg"
mkdir -p "$STAGE/usr/bin" \
         "$STAGE/usr/share/quickdraw" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/256x256/apps"

# Build release binary
cd "$SCRIPT_DIR"
make clean && make

cp "$SCRIPT_DIR/quickdraw" "$STAGE/usr/bin/quickdraw"
cp -r "$PROJECT_DIR/WebSrc" "$STAGE/usr/share/quickdraw/WebSrc"
cp "$SCRIPT_DIR/share/applications/quickdraw.desktop" "$STAGE/usr/share/applications/quickdraw.desktop"
for size in 16 32 48 64 128 256; do
    mkdir -p "$STAGE/usr/share/icons/hicolor/${size}x${size}/apps"
    cp "$SCRIPT_DIR/share/icons/hicolor/${size}x${size}/apps/quickdraw.png" \
       "$STAGE/usr/share/icons/hicolor/${size}x${size}/apps/quickdraw.png"
done

echo "--- Building .deb ---"
DEB_DIR="/tmp/opencode/quickdraw-pkg/deb/quickdraw_${VERSION}_amd64"
mkdir -p "$DEB_DIR/DEBIAN"
cp -r "$STAGE/." "$DEB_DIR/"

cat > "$DEB_DIR/DEBIAN/control" << EOF
Package: quickdraw
Version: $VERSION
Section: graphics
Priority: optional
Architecture: amd64
Depends: libgtk-3-0 (>= 3.22), libwebkit2gtk-4.1-0 (>= 2.40), libjson-glib-1.0-0
Maintainer: Zu5h <zush@users.noreply.github.com>
Description: Gesture Drawing App
 Allows you to select folders on your computer to pull reference
 images from, and then shows you a slideshow of random images.
 Perfect for gesture drawing, or quick studies.
EOF

dpkg-deb --build --root-owner-group "$DEB_DIR" > /dev/null
mv "${DEB_DIR}.deb" "$PROJECT_DIR/QuickDraw-${VERSION}.deb"
echo "deb: $PROJECT_DIR/QuickDraw-${VERSION}.deb ($(du -h "$PROJECT_DIR/QuickDraw-${VERSION}.deb" | cut -f1))"

echo "--- Building .rpm ---"
SPEC="/tmp/opencode/quickdraw-pkg/quickdraw.spec"
cat > "$SPEC" << EOF
Name:       quickdraw
Version:    $VERSION
Release:    1
Summary:    Gesture Drawing App
License:    MIT
URL:        https://github.com/Zu5h/QuickDraw-Linux-wibecoded
BuildArch:  x86_64
Requires:   gtk3 >= 3.22, webkit2gtk4.1 >= 2.40, json-glib >= 1.6

%description
Allows you to select folders on your computer to pull reference images
from, and then shows you a slideshow of random images. Perfect for
gesture drawing, or quick studies.

%install
cp -r "$STAGE/." %{buildroot}/

%files
/usr/bin/quickdraw
/usr/share/applications/quickdraw.desktop
/usr/share/quickdraw/WebSrc
/usr/share/icons/hicolor/*
EOF

mkdir -p "$HOME/rpmbuild/BUILD" "$HOME/rpmbuild/RPMS" "$HOME/rpmbuild/SOURCES" "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/SRPMS"
cp "$SPEC" "$HOME/rpmbuild/SPECS/quickdraw.spec"
rpmbuild --define "_topdir $HOME/rpmbuild" -bb "$SPEC" > /dev/null

RPM_FILE=$(find "$HOME/rpmbuild/RPMS" -name "quickdraw*.rpm" | head -1)
if [ -n "$RPM_FILE" ]; then
    cp "$RPM_FILE" "$PROJECT_DIR/QuickDraw-${VERSION}.rpm"
    echo "rpm: $PROJECT_DIR/QuickDraw-${VERSION}.rpm ($(du -h "$PROJECT_DIR/QuickDraw-${VERSION}.rpm" | cut -f1))"
fi

echo ""
echo "=== Done ==="