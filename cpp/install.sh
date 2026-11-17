#!/bin/sh
# Installs Changeling Studio (C++) for this user on Linux: ~/.local/share/changeling-studio and a menu entry.
set -e
cd "$(dirname "$0")"
DEST="$HOME/.local/share/changeling-studio"
mkdir -p "$DEST" "$HOME/.local/share/applications"
cp -r build/ChangelingStudio assets lang NOTICE "$DEST/"
printf '[Desktop Entry]\nType=Application\nName=Atelier - Changeling Studio\nExec=%s/ChangelingStudio\nIcon=%s/assets/logo.png\nCategories=Graphics;\n' "$DEST" "$DEST" > "$HOME/.local/share/applications/changeling-studio.desktop"
echo "Installed: $DEST"
