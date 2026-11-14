#!/usr/bin/env bash
trap '' PIPE
# Build tutorials/index.html (static page: videos, captions, transcripts).
#
#   tutorials/build-site.sh            only regenerate index.html
#   tutorials/build-site.sh -b Dino8   also (re)record every video first
#
# Output: tutorials/index.html next to tutorials/out/<id>/ (videos + captions).
# The page itself needs no server-side code; serve the tutorials/ directory
# with any static file server, or open index.html from disk.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN=""
while getopts "b:" opt; do
  case "$opt" in
    b) BIN="$OPTARG" ;;
    *) sed -n 3,7p "$0"; exit 2 ;;
  esac
done
if [ -n "$BIN" ]; then bash "$HERE/record.sh" -b "$BIN"; fi
python3 "$HERE/build_site.py"
