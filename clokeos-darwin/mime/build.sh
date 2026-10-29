#!/bin/bash
# Baut die MIME-Datenbank des Desktops: PureDarwins glib liest nur noch mime.cache (keine Textdateien globs2/magic mehr),
# und das Image hat keine shared-mime-info. Hier: Standarddatenbank (freedesktop.org.xml) + mime/clokeos.xml mit
# update-mime-database (Homebrew: shared-mime-info, GPL-2.0) zu $stage/share/mime.   mime/build.sh <stage>
# Fehlt das Werkzeug, bleibt es bei den handgeschriebenen Dateien (Dateitypen werden dann nicht erkannt).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
stage="$1"
umd="$(command -v update-mime-database || true)"
base=""
for p in "$(dirname "$umd")/../share/mime/packages/freedesktop.org.xml" /opt/homebrew/opt/shared-mime-info/share/mime/packages/freedesktop.org.xml /usr/local/opt/shared-mime-info/share/mime/packages/freedesktop.org.xml /usr/share/mime/packages/freedesktop.org.xml; do
  [ -f "$p" ] && { base="$p"; break; }
done
if [ -z "$umd" ] || [ -z "$base" ]; then
  echo "mime: übersprungen (update-mime-database/shared-mime-info fehlt: brew install shared-mime-info)" >&2
  exit 0
fi
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/packages"
cp "$base" "$here/mime/clokeos.xml" "$t/packages/"
"$umd" "$t" >/dev/null
rm -rf "$stage/share/mime"
mkdir -p "$stage/share/mime"
# die Textdateien braucht der Leser nicht; Cache, Typenliste und Symbole für Gnome-Symbolthemen reichen
cp "$t/mime.cache" "$t/version" "$stage/share/mime/"
cp "$t/globs2" "$t/magic" "$t/subclasses" "$t/aliases" "$t/types" "$stage/share/mime/" 2>/dev/null || true
echo "mime: $(du -sk "$stage/share/mime" | cut -f1) KB" >&2
