<p align="center">
  <img src="https://raw.githubusercontent.com/blendermf/QuickDraw/master/docs/img/Logo.png" alt="QuickDraw">
</p>

<h1 align="center">QuickDraw</h1>
<h3 align="center">Gesture Drawing App</h3>

Allows you to select folders on your computer to pull reference images from, and then shows you a slideshow of random images. 

Perfect for gesture drawing, or quick studies.

## Screenshots

<p align="center">
<img src="https://raw.githubusercontent.com/blendermf/QuickDraw/master/docs/img/Screenshot1.png" alt="Screenshot1">
<br/><br/>
<img src="https://raw.githubusercontent.com/blendermf/QuickDraw/master/docs/img/Screenshot2.png" alt="Screenshot2">
</p>

## Download

Go to the [Releases](https://github.com/Zu5h/QuickDraw-Linux-wibecoded/releases) page and download the latest version.

---

## Linux Port

Native Linux port with Wayland/X11 support, built with **GTK3** + **WebKitGTK**. The UI layer (HTML/CSS/JS) is shared with the original macOS and Windows builds.

### What was done

- Replaced WPF/WebView2 (Windows) and SwiftUI/WKWebView (macOS) wrappers with a **GTK3 + WebKitGTK** native wrapper (`main.c`)
- Implemented a custom `qd://` URI scheme in WebKitGTK to serve images from the local filesystem (replaces fake `file://` paths)
- Built a **JS-to-native bridge** using WebKit's `script-message-received` signal and `CustomEvent('qd-message')` — same protocol as the original macOS bridge
- Added `GtkFileChooserDialog` for folder selection, `xdg-open` for file manager integration
- Injected JS bridge initialization via `WebKitUserScript` (persists across navigations)
- Added recursive subfolder scanning for images
- Timer display for slideshow countdown
- Added Linux-specific font stack (`Cantarell`/`Noto Sans`) and Unicode icon glyphs

### Building from source

**Dependencies:**
- `gtk3`
- `webkit2gtk-4.1`
- `json-glib`
- `pkg-config`, `gcc`

On Arch-based distros:
```bash
sudo pacman -S gtk3 webkit2gtk-4.1 json-glib pkgconf gcc
```

On Debian/Ubuntu:
```bash
sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev libjson-glib-dev pkg-config gcc
```

Build:
```bash
cd QuickDrawLinux
make
./quickdraw
```

### AppImage (portable)

A portable AppImage is available in Releases — no installation required. Just download and run:
```bash
chmod +x QuickDraw-x86_64.AppImage
./QuickDraw-x86_64.AppImage
```

### Native packages (.deb / .rpm)

`.deb` and `.rpm` packages are available in Releases for Debian/Ubuntu and Fedora respectively. They pull dependencies from the distro repos (small download, native install):

```bash
# Debian / Ubuntu / Mint
sudo apt install ./QuickDraw-1.0.0.deb

# Fedora
sudo dnf install ./QuickDraw-1.0.0.rpm
```

Note: the AppImage is built on Arch (glibc 2.44), so it only runs on distros with glibc >= 2.44. For older distros use the native `.deb`/`.rpm` packages instead.

Build both yourself:
```bash
cd QuickDrawLinux
./build-packages.sh
```

### Distribution

To share the app, send the entire folder structure:
```
QuickDrawLinux/
├── quickdraw              # binary
├── QuickDraw.ico          # app icon
└── ../WebSrc/             # shared UI (HTML/CSS/JS)
    ├── index.html
    ├── slideshow.html
    ├── css/style.css
    └── js/
```

The target system needs `gtk3`, `webkit2gtk-4.1` and `json-glib` installed.

### Architecture

```
main.c          — GTK3 window, WebKitGTK view, qd:// scheme handler,
                   JS↔native bridge, folder scanning, file dialogs
Makefile        — build with pkg-config for gtk+-3.0, webkit2gtk-4.1, json-glib-1.0
build-appimage.sh — automated AppImage packaging (downloads linuxdeploy)

WebSrc/js/script.js     — main page JS bridge (platform detection for Linux)
WebSrc/js/slideshow.js  — slideshow logic with timer
WebSrc/css/style.css    — Linux-specific styles
```

## License

MIT — see [LICENSE](LICENSE)

---

*Original project by [MF Digital Media (blendermf)](https://github.com/blendermf/QuickDraw)*
