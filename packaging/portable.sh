#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'Usage: APPIMAGE_RUNTIME=<runtime> %s <build-dir> <package-name> <SDL3-library>\n' "$0" >&2
    printf 'To build all packages, run ./packaging/build.sh from the repository root.\n' >&2
    exit 2
fi

build=$1
name=$2
sdl=$3
runtime=${APPIMAGE_RUNTIME:?Set APPIMAGE_RUNTIME to the x86_64 AppImage type-2 runtime}
assets=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
app="$work/$name"

cmake --install "$build" --prefix "$app/usr"
install -Dm755 "$sdl" "$app/usr/lib/libSDL3.so.0"
install -m644 "$assets/licenses/SDL3.txt" "$app/usr/share/licenses/agario3d/"
ln -s usr/share/applications/agario3d.desktop "$app/agario3d.desktop"
ln -s usr/share/icons/hicolor/scalable/apps/agario3d.svg "$app/agario3d.svg"
ln -s agario3d.svg "$app/.DirIcon"
cat > "$app/AppRun" <<'LAUNCH'
#!/bin/sh
appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
export LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$appdir/usr/bin/agario3d" "$@"
LAUNCH
chmod 755 "$app/AppRun"

(cd "$work" && zip -qry "$name.zip" "$name")
mksquashfs "$app" "$work/payload" -noappend -all-root -no-xattrs -comp zstd -processors 2 >/dev/null
cat "$runtime" "$work/payload" > "$work/$name.AppImage"
chmod 755 "$work/$name.AppImage"
(cd "$work" && "./$name.AppImage" --appimage-extract >/dev/null && test -x squashfs-root/AppRun)
mkdir -p "$build/packages"
mv "$work/$name.zip" "$work/$name.AppImage" "$build/packages/"
