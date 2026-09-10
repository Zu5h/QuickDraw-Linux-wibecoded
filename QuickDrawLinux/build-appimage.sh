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
    gcc make pkg-config wget ca-certificates python3 libgtk-3-dev libwebkit2gtk-4.1-dev libjson-glib-dev \
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

# Create AppRun on host to avoid heredoc-in-heredoc issues
APPRUN_SRC="$SCRIPT_DIR/.AppRun"
cat > "$APPRUN_SRC" << 'APPRUNEOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
cd "$HERE"
export PATH="${HERE}/usr/bin/:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib/:${LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share/:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
exec "${HERE}/usr/bin/quickdraw" "$@"
APPRUNEOF
chmod +x "$APPRUN_SRC"

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

        # Bundle WebKitGTK helper processes (not tracked via ldd)
        mkdir -p $APPDIR/usr/lib/webkit2gtk-4.1
        for p in WebKitWebProcess WebKitNetworkProcess WebKitGPUProcess; do
            cp /usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/$p \
               $APPDIR/usr/lib/webkit2gtk-4.1/$p
        done
        cp -r /usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/injected-bundle \
              $APPDIR/usr/lib/webkit2gtk-4.1/

        # AppRun created on host
        cp /project/QuickDrawLinux/.AppRun $APPDIR/AppRun
        chmod +x $APPDIR/AppRun

        echo "Downloading linuxdeploy..."
        wget -q -O /tmp/linuxdeploy \
            https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
        chmod +x /tmp/linuxdeploy

        echo "Bundling dependencies..."
        /tmp/linuxdeploy \
            --appdir $APPDIR \
            --desktop-file $APPDIR/quickdraw.desktop \
            --icon-file $APPDIR/quickdraw.png

        # WebKitGTK looks up its helper processes at a build-time-embedded
        # absolute path (WEBKIT_EXEC_PATH is compiled out without developer
        # mode). Rewrite the embedded paths to be relative to $HERE so the
        # bundled helpers inside the AppImage are used.
        python3 - << 'PATCH'
data = bytearray(open("/tmp/AppDir/usr/lib/libwebkit2gtk-4.1.so.0", "rb").read())
pairs = [
    (b"/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/injected-bundle/",
     b"./usr/lib/webkit2gtk-4.1/injected-bundle/"),
    (b"/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1",
     b"./usr/lib/webkit2gtk-4.1"),
]
for old, new in pairs:
    assert len(old) >= len(new)
    start = 0
    hits = 0
    while True:
        i = data.find(old, start)
        if i < 0:
            break
        data[i:i + len(old)] = new + b"\x00" * (len(old) - len(new))
        hits += 1
        start = i + len(old)
    print(f"patched {hits}x {old[:16]}...")
open("/tmp/AppDir/usr/lib/libwebkit2gtk-4.1.so.0", "wb").write(bytes(data))
PATCH

        /tmp/linuxdeploy \
            --appdir $APPDIR \
            --output appimage
    '

rm -rf "$DIR"
rm -f "$APPRUN_SRC"

if [ -f "$PROJECT_DIR/QuickDrawLinux/QuickDraw-x86_64.AppImage" ]; then
    echo ""
    echo "=== Done ==="
    ls -lh "$PROJECT_DIR/QuickDrawLinux/QuickDraw-x86_64.AppImage"
else
    echo "ERROR: AppImage not produced."
    exit 1
fi