#!/bin/sh
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"

bin="$HOME/.local/bin/shotx"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.config/autostart"
install -m755 build/shotx "$bin.new" && mv "$bin.new" "$bin"
sed "s|@BIN@|$bin|" data/shotx.desktop > "$HOME/.local/share/applications/shotx.desktop"
sed "s|@BIN@|$bin|" data/shotx.desktop > "$HOME/.config/autostart/shotx.desktop"
kbuildsycoca6 >/dev/null 2>&1 || true
echo "Installed $bin"
