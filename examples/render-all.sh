#!/usr/bin/env bash
# Regenerate every docs/images/* file from the examples (each example writes images/<name>.png,
# and images/<name>.gif when it animates and ffmpeg is available).
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/build:$PATH"
compiler="${LMSCRIPT_BIN:-$(scripts/select-compiler.sh)}"
mkdir -p docs/images
command -v ffmpeg >/dev/null 2>&1 || echo "SKIP animations: ffmpeg unavailable (PNGs still regenerate)"
for program in examples/*.lms; do
  name="$(basename "$program" .lms)"
  "$compiler" "$program"
  cp "images/$name.png" "docs/images/$name.png"
  if [ -f "images/$name.gif" ]; then cp "images/$name.gif" "docs/images/$name.gif"; fi
done
python3 scripts/check-gallery.py --generated
