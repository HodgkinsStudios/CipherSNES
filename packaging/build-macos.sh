#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="$root/build-mac/CipherSNES-GTK.app"
[[ -d "$app" ]] || { echo 'Missing macOS app: build the GTK target first' >&2; exit 1; }
res="$app/Contents/Resources"
macos="$app/Contents/MacOS"
frameworks="$app/Contents/Frameworks"
mkdir -p "$res/share/glib-2.0/schemas" "$res/share/icons" "$res/lib/gdk-pixbuf-2.0/2.10.0/loaders" "$frameworks"
brew_prefix="$(brew --prefix)"
cp -r "$brew_prefix/share/glib-2.0/schemas/." "$res/share/glib-2.0/schemas/"
for name in Adwaita hicolor; do
    [[ -d "$brew_prefix/share/icons/$name" ]] && cp -r "$brew_prefix/share/icons/$name" "$res/share/icons/"
done
pixbuf_prefix="$(brew --prefix gdk-pixbuf)"
for file in "$pixbuf_prefix"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so; do
    [[ -f "$file" ]] && cp "$file" "$res/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
done
cp "$root/LICENSE" "$root/README.md" "$res/"
cp "$root/assets/CipherSNES_icon.png" "$root/assets/CipherSNES.icns" "$res/"
main_binary="$macos/CipherSNES-GTK"
# dylibbundler clears/rebuilds its destination on separate invocations.
# Bundle every Mach-O target together so optional pixbuf loaders cannot replace
# the main executable's GTK/SDL dependencies.
bundle_args=(-od -b -x "$main_binary")
for plugin in "$res"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so; do
    [[ -f "$plugin" ]] && bundle_args+=(-x "$plugin")
done
dylibbundler "${bundle_args[@]}" -d "$frameworks" -p @executable_path/../Frameworks
for required in libgtk-3.0.dylib libgdk-3.0.dylib libSDL2-2.0.0.dylib; do
    if [[ ! -f "$frameworks/$required" ]]; then
        echo "Missing bundled macOS dependency: $required" >&2
        exit 1
    fi
done
dylib_count="$(find "$frameworks" -maxdepth 1 -name '*.dylib' -type f | wc -l | tr -d ' ')"
if (( dylib_count < 25 )); then
    echo "Incomplete macOS bundle ($dylib_count dylibs); refusing release" >&2
    exit 1
fi
echo "Bundled $dylib_count macOS dynamic libraries"
mv "$main_binary" "$macos/CipherSNES-bin"
cat > "$main_binary" <<'SH'
#!/bin/bash
bundle="$(cd "$(dirname "$0")/.." && pwd)"
export DYLD_LIBRARY_PATH="$bundle/Frameworks${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export XDG_DATA_DIRS="$bundle/Resources/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export GSETTINGS_SCHEMA_DIR="$bundle/Resources/share/glib-2.0/schemas"
export GDK_PIXBUF_MODULEDIR="$bundle/Resources/lib/gdk-pixbuf-2.0/2.10.0/loaders"
exec "$bundle/MacOS/CipherSNES-bin" "$@"
SH
chmod +x "$main_binary"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName CipherSNES' "$app/Contents/Info.plist" || true
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 0.1.0' "$app/Contents/Info.plist" || true
/usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile CipherSNES.icns' "$app/Contents/Info.plist" || \
    /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string CipherSNES.icns' "$app/Contents/Info.plist"
test -s "$res/CipherSNES.icns"
codesign --force --deep --sign - "$app" || true
arch="$(uname -m)"
out="$root/release/CipherSNES-v0.1-macOS-$arch.zip"
mkdir -p "$root/release"
rm -f "$out"
ditto -c -k --sequesterRsrc --keepParent "$app" "$out"
ls -lh "$out"
