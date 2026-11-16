#!/usr/bin/env bash
set -euo pipefail
[[ "${MSYSTEM:-}" == UCRT64 ]] || { echo 'MSYS2 UCRT64 required' >&2; exit 1; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix=/ucrt64
out="$root/release/CipherSNES-v0.1-Windows-x64"
rm -rf "$out"
mkdir -p "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders" "$out/share/glib-2.0/schemas" "$out/etc/gtk-3.0" "$out/assets"
cp "$root/build-win/CipherSNES-GTK.exe" "$out/CipherSNES.exe"
cp "$root/LICENSE" "$root/README.md" "$out/"
cp "$root/assets/CipherSNES_icon.png" "$out/assets/"
cp "$root/assets/CipherSNES.ico" "$out/assets/"
shopt -s nullglob
for module in "$prefix"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do cp "$module" "$out/lib/gdk-pixbuf-2.0/2.10.0/loaders/"; done
cp "$prefix/bin/gdk-pixbuf-query-loaders.exe" "$out/"
copy_deps() {
  local input="$1" name
  while IFS= read -r name; do
    if [[ -f "$prefix/bin/$name" ]]; then cp -n "$prefix/bin/$name" "$out/$name"; fi
  done < <(ntldd -R "$input" 2>/dev/null | sed -nE 's/^[[:space:]]*([^[:space:]]+\.[dD][lL][lL])[[:space:]]+=>.*/\1/p' | sort -fu)
  return 0
}
copy_deps "$out/CipherSNES.exe"
copy_deps "$out/gdk-pixbuf-query-loaders.exe"
for dll in "$out"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do copy_deps "$dll"; done
for dll in "$out"/*.dll; do copy_deps "$dll"; done
[[ -f "$out/libgtk-3-0.dll" && -f "$out/SDL2.dll" ]] || { echo 'Required GTK/SDL DLLs missing' >&2; exit 1; }
cp "$prefix"/share/glib-2.0/schemas/*.xml "$out/share/glib-2.0/schemas/"
"$prefix/bin/glib-compile-schemas.exe" "$out/share/glib-2.0/schemas/"
for theme in Adwaita hicolor; do
  if [[ -d "$prefix/share/icons/$theme" ]]; then
    mkdir -p "$out/share/icons"
    cp -r "$prefix/share/icons/$theme" "$out/share/icons/"
  fi
done
cat > "$out/etc/gtk-3.0/settings.ini" <<'INI'
[Settings]
gtk-theme-name=Adwaita
INI
cat > "$out/Run-CipherSNES.bat" <<'BAT'
@echo off
setlocal
set "APPDIR=%~dp0"
set "PATH=%APPDIR%;%PATH%"
set "GSETTINGS_SCHEMA_DIR=%APPDIR%share\glib-2.0\schemas"
set "XDG_DATA_DIRS=%APPDIR%share"
set "GTK_DATA_PREFIX=%APPDIR%"
set "GDK_PIXBUF_MODULEDIR=%APPDIR%lib\gdk-pixbuf-2.0\2.10.0\loaders"
set "GDK_PIXBUF_MODULE_FILE=%TEMP%\CipherSNES-%USERNAME%-pixbuf-loaders.cache"
"%APPDIR%gdk-pixbuf-query-loaders.exe" > "%GDK_PIXBUF_MODULE_FILE%"
start "" "%APPDIR%CipherSNES.exe" %*
BAT
cat > "$out/START_HERE.txt" <<'TXT'
CipherSNES v0.1 — Windows x64
Extract the entire folder and double-click Run-CipherSNES.bat.
The launcher sets GTK runtime paths. No games or Nintendo firmware are provided.
TXT
count=$(find "$out" -maxdepth 1 -iname '*.dll' | wc -l)
echo "Windows bundle has $count DLLs"
(( count > 10 ))
