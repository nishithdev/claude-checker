#!/bin/bash
# Usage: ./embed_firmware.sh path/to/love_slides_cyd.ino.bin
# Encodes the compiled binary as base64 and injects it into both
# web-flasher/index.html and ../../docs/index.html

set -e

BIN="${1}"
if [ -z "$BIN" ] || [ ! -f "$BIN" ]; then
  echo "Usage: $0 path/to/love_slides_cyd.ino.bin"
  exit 1
fi

B64=$(base64 -i "$BIN" | tr -d '\n')
echo "Binary: $BIN ($(wc -c < "$BIN") bytes, base64 length: ${#B64})"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FILES=(
  "$SCRIPT_DIR/web-flasher/index.html"
  "$SCRIPT_DIR/../../docs/index.html"
)

for FILE in "${FILES[@]}"; do
  if [ ! -f "$FILE" ]; then
    echo "Skipping (not found): $FILE"
    continue
  fi
  # Replace the FIRMWARE_B64 = "..." line (handles both empty and previously-set values)
  sed -i.bak "s|const FIRMWARE_B64 = \"[^\"]*\";|const FIRMWARE_B64 = \"$B64\";|" "$FILE"
  rm -f "$FILE.bak"
  echo "✓ Updated: $FILE"
done

echo "Done. Commit and push — GitHub Pages will serve the updated flasher."
