#!/usr/bin/env bash
trap '' PIPE
# Record Dino 8 tutorial videos from the real app.
#
#   tutorials/record.sh [-b path/to/Dino8] [-o outdir] [-f mp4|webm|gif|apng] [ID ...]
#
# With no ID, every tutorials/<id>/tutorial.txt is recorded.  Output goes to
# <outdir>/<id>/ (default tutorials/out/, git-ignored).  See docs/TUTORIALS.md.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/../build/Dino8"
OUT="$HERE/out"
FMT=mp4
while getopts "b:o:f:h" opt; do
  case "$opt" in
    b) BIN="$OPTARG" ;;
    o) OUT="$OPTARG" ;;
    f) FMT="$OPTARG" ;;
    *) sed -n 3,9p "$0"; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
command -v python3 >/dev/null || { echo "record.sh: python3 not found" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "record.sh: ffmpeg not found (apt-get install ffmpeg)" >&2; exit 1; }
command -v xvfb-run >/dev/null || echo "record.sh: warning: xvfb-run not found, using the current display" >&2
if [ "$#" -gt 0 ]; then IDS=("$@"); else IDS=(); for d in "$HERE"/[0-9][0-9]-*/; do IDS+=("$(basename "$d")"); done; fi
for id in "${IDS[@]}"; do
  python3 "$HERE/record.py" --bin "$BIN" --id "$id" --out "$OUT" --format "$FMT"
done
