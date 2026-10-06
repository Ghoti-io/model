#!/bin/sh
# Fetch the pinned 3ds Max MTL files named in tools/corpus/pins.
#
# Nothing here is committed. third_party/ is gitignored. A file whose sha256
# already matches is left alone, so a later make test does not use the
# network. An interrupted download stays named .partial and is not surveyed.
#
# suite/install.sh runs every tools/*/fetch.sh before the tests.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
pins=$root/tools/corpus/pins
dest=$root/third_party/mtl-exporters
mkdir -p "$dest"

command -v curl >/dev/null 2>&1 || {
  printf 'missing curl, which tools/corpus/fetch.sh needs\n' >&2
  exit 1
}

fetched=0
while read -r name hash url; do
  case $name in
    ''|'#'*) continue ;;
  esac
  out=$dest/$name.mtl
  # Binary digest, in Python, so CRLF bytes stay CRLF and macOS needs no sha256sum.
  if [ -f "$out" ] && python3 "$root/tools/corpus/survey.py" --matches "$name" "$out"; then
    continue
  fi
  rm -f "$out.partial"
  curl -fsSL -o "$out.partial" "$url"
  python3 "$root/tools/corpus/survey.py" --matches "$name" "$out.partial" \
    || { printf '%s is not the pinned file\n' "$name" >&2; exit 1; }
  mv -f "$out.partial" "$out"
  printf 'fetch   third_party/mtl-exporters/%s.mtl\n' "$name"
  fetched=$((fetched + 1))
done < "$pins"

if [ "$fetched" -eq 0 ]; then
  printf 'have    third_party/mtl-exporters\n'
fi
