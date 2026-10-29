#!/bin/bash
# Packt den GTK-Build für die VM: webkit/stage.sh <builddir> <ausgabe.tgz>  (entpackt ergibt es opt/webkit/{bin,lib})
set -euo pipefail
b="$1"; out="$2"; st="$(mktemp -d)"
mkdir -p "$st/opt/webkit/bin" "$st/opt/webkit/lib"
for f in libwebkit2gtk-4.1.dylib libjavascriptcoregtk-4.1.dylib; do
  strip -x -o "$st/opt/webkit/lib/$f" "$b/lib/$f"; install_name_tool -id "/opt/webkit/lib/$f" "$st/opt/webkit/lib/$f"
done
for f in MiniBrowser WebKitWebProcess WebKitNetworkProcess; do cp "$b/bin/$f" "$st/opt/webkit/bin/"; done
for f in "$st"/opt/webkit/bin/* "$st"/opt/webkit/lib/*; do
  for d in libwebkit2gtk-4.1.dylib libjavascriptcoregtk-4.1.dylib; do install_name_tool -change "lib/$d" "/opt/webkit/lib/$d" "$f" 2>/dev/null || true; done
done
codesign -s - --force "$st"/opt/webkit/bin/* "$st"/opt/webkit/lib/* 2>/dev/null
(cd "$st" && tar czf "$out" opt); rm -rf "$st"; ls -la "$out"
