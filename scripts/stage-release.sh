#!/usr/bin/env bash
# Stage downloaded CI artifacts into one flat release directory and write
# SHA-256 checksum files for every file in it.
#
#   scripts/stage-release.sh <artifacts-dir> <release-dir>
#
# <artifacts-dir> is what actions/download-artifact produced (one sub-directory
# per uploaded artifact). Every installer/package (.exe .zip .deb .tar.gz .dmg)
# found anywhere beneath it is copied into <release-dir> (flat). Then:
#   <file>.sha256   one per artifact, "<hash>  <name>" (sha256sum -c format)
#   SHA256SUMS      one file covering every artifact
# No network access, no secrets. Fails if two artifacts share a file name
# (they would overwrite each other as release assets) or if nothing was found.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <artifacts-dir> <release-dir>" >&2
  exit 2
fi
src=$1
dest=$2
[ -d "$src" ] || { echo "stage-release: no such directory: $src" >&2; exit 2; }

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else openssl dgst -sha256 -r "$1" | cut -d' ' -f1
  fi
}

mkdir -p "$dest"
count=0
while IFS= read -r -d '' f; do
  name=$(basename "$f")
  if [ -e "$dest/$name" ]; then
    echo "stage-release: duplicate artifact name '$name' (second copy at $f)" >&2
    exit 1
  fi
  cp "$f" "$dest/$name"
  count=$((count + 1))
done < <(find "$src" -type f \( -name '*.exe' -o -name '*.zip' -o -name '*.deb' -o -name '*.tar.gz' -o -name '*.dmg' \) -print0 | sort -z)

if [ "$count" -eq 0 ]; then
  echo "stage-release: no installers or packages found under $src" >&2
  exit 1
fi

# Checksums are computed over the staged copies, i.e. exactly the bytes that
# get published.
: > "$dest/SHA256SUMS"
while IFS= read -r name; do
  h=$(sha256_of "$dest/$name")
  printf '%s  %s\n' "$h" "$name" > "$dest/$name.sha256"
  printf '%s  %s\n' "$h" "$name" >> "$dest/SHA256SUMS"
done < <(cd "$dest" && ls -1 | grep -v -e '\.sha256$' -e '^SHA256SUMS$' | LC_ALL=C sort)

echo "stage-release: staged $count artifact(s) into $dest"
cat "$dest/SHA256SUMS"
