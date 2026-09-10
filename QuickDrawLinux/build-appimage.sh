#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== QuickDraw portable AppImage builder ==="
echo ""
echo "Building inside Ubuntu 24.04 container (glibc 2.38 baseline)"
echo "so the AppImage runs on most modern distros (glibc >= 2.38)."
echo ""

# Check docker
if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker is required for portable AppImage builds."
    echo "       (Building on this distro would bundle its glibc and fail on older systems)"
    exit 1
fi

DIR="$SCRIPT_DIR/.appimage-container"
rm -rf "$DIR"
mkdir -p "$DIR"

cat > "$DIR/Dockerfile" << 'DOCKERFILE'
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc pkg-config wget ca-certificates libgtk-3-dev libwebkit2gtk-4.1-dev libjson-glib-dev \
    file binutils locales \
    && locale-gen en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV APPIMAGE_EXTRACT_AND_RUN=1

WORKDIR /build
DOCKERFILE

if ! docker image inspect quickdraw-builder:24.04 >/dev/null 2>&1; then
    echo "Building container image (first run only)..."
    docker build -t quickdraw-builder:24.04 "$DIR"
fi

rm -f "$PROJECT_DIR/QuickDraw-x86_64.AppImage"

echo "Building AppImage..."
docker run --rm \
    -v "$PROJECT_DIR":/project \
    -e APPIMAGE_EXTRACT_AND_RUN=1 \
    quickdraw-builder:24.04 \
    bash -c '
        set -e
        cd /project/QuickDrawLinux
        make clean && make

        APPDIR=/tmp/AppDir
        rm -rf $APPDIR
        mkdir -p $APPDIR/usr/bin $APPDIR/usr/share/quickdraw $APPDIR/usr/share/applications

        cp quickdraw $APPDIR/usr/bin/quickdraw
        cp -r /project/WebSrc $APPDIR/usr/share/quickdraw/WebSrc
        cp share/applications/quickdraw.desktop $APPDIR/usr/share/applications/quickdraw.desktop
        cp share/icons/hicolor/256x256/apps/quickdraw.png $APPDIR/quickdraw.png
        cp share/applications/quickdraw.desktop $APPDIR/quickdraw.desktop
        cp -r share/icons/hicolor $APPDIR/usr/share/icons/

        cat > $APPDIR/AppRun << EOF
#!/bin/bash
SELF=\$(readlink -f "\$0")
HERE=\${SELF%/*}
export PATH="\${HERE}/usr/bin/:\${PATH}"
export LD_LIBRARY_PATH="\${HERE}/usr/lib/:\${LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="\${HERE}/usr/share/:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "\${HERE}/usr/bin/quickdraw" "\$@"
EOF
        chmod +x $APPDIR/AppRun

        echo "Downloading linuxdeploy..."
        wget -q -O /tmp/linuxdeploy \
            https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
        chmod +x /tmp/linuxdeploy

        echo "Bundling dependencies..."
        /tmp/linuxdeploy \
            --appdir $APPDIR \
            --desktop-file $APPDIR/quickdraw.desktop \
            --icon-file $APPDIR/quickdraw.png \
            --output appimage
    '

rm -rf "$DIR"

if [ -f "$PROJECT_DIR/QuickDrawLinux/QuickDraw-x86_64.AppImage" ]; then
    echo ""
    echo "=== Done ==="
    ls -lh "$PROJECT_DIR/QuickDrawLinux/QuickDraw-x86_64.AppImage"
else
    echo "ERROR: AppImage not produced."
    exit 1
fi