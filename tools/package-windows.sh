#!/bin/sh
# Build a self-contained Windows folder (dist/PharmacyMS) from an MSYS2 shell.
# Usage (from the project root, inside MSYS2 UCRT64 or MINGW64):
#     make dist
# Result: dist/PharmacyMS/pharmacy.exe + every DLL and resource GTK needs.
# Zip that folder, or feed it to an installer builder (Inno Setup, NSIS).
set -e

PREFIX="${MINGW_PREFIX:-/ucrt64}"
DIST="dist/PharmacyMS"
EXE="pharmacy.exe"

[ -f "$EXE" ] || { echo "Build $EXE first (make)"; exit 1; }
[ -d "$PREFIX/bin" ] || { echo "Run this from an MSYS2 UCRT64/MINGW64 shell"; exit 1; }

rm -rf "$DIST"
mkdir -p "$DIST/lib" "$DIST/share/icons" "$DIST/share/glib-2.0/schemas" "$DIST/etc/gtk-3.0"
cp "$EXE" "$DIST/"

# Copy every DLL from the MSYS2 prefix that a given binary depends on.
copy_deps() {
    ldd "$1" | awk '{print $3}' | grep -i "^$PREFIX/bin/" | while read -r dll; do
        cp -u "$dll" "$DIST/"
    done
}
copy_deps "$EXE"

# Image loaders (PNG/SVG icons) and their own DLL dependencies.
cp -r "$PREFIX/lib/gdk-pixbuf-2.0" "$DIST/lib/"
for loader in "$DIST"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do
    [ -f "$loader" ] && copy_deps "$loader"
done

# Icon themes used by dialogs and buttons.
cp -r "$PREFIX/share/icons/Adwaita" "$DIST/share/icons/"
cp -r "$PREFIX/share/icons/hicolor" "$DIST/share/icons/"

# Compiled GSettings schemas (required by the file chooser used for export).
cp "$PREFIX/share/glib-2.0/schemas/gschemas.compiled" "$DIST/share/glib-2.0/schemas/"

# Default Adwaita theme (bundled with GTK) with the Windows UI font.
cat > "$DIST/etc/gtk-3.0/settings.ini" <<'EOF'
[Settings]
gtk-font-name=Segoe UI 10
EOF

cp README.md "$DIST/" 2>/dev/null || true
echo "Done: $DIST ($(du -sh "$DIST" | cut -f1)). Zip it or build an installer from it."
