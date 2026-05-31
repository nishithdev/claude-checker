#!/bin/bash
# Double-click this file on macOS to compile the firmware and embed it into the web flasher.
# Requires Arduino IDE 2.x to be installed.

set -e
cd "$(dirname "$0")"
SKETCH="firmware/love_slides_cyd/love_slides_cyd.ino"
WEB_FLASHER="web-flasher/index.html"
DOCS="../docs/index.html"
BUILD_DIR="/tmp/love_slides_build"

echo "================================================"
echo "  Love Slides — Build & Embed"
echo "================================================"

# ── Find arduino-cli ──────────────────────────────────────────────────────────
CLI=""
CANDIDATES=(
  "$HOME/Library/Arduino15/arduino-cli"
  "/Applications/Arduino IDE.app/Contents/Resources/app/node_modules/arduino-ide-extension/build/arduino-cli"
  "$(which arduino-cli 2>/dev/null)"
  "/usr/local/bin/arduino-cli"
)
for c in "${CANDIDATES[@]}"; do
  if [ -x "$c" ]; then CLI="$c"; break; fi
done

# Arduino IDE 2.x bundles arduino-cli inside the app package
if [ -z "$CLI" ]; then
  APP_CLI=$(find /Applications -name "arduino-cli" -type f 2>/dev/null | head -1)
  [ -n "$APP_CLI" ] && CLI="$APP_CLI"
fi

if [ -z "$CLI" ]; then
  echo ""
  echo "ERROR: arduino-cli not found."
  echo "Install it with:  brew install arduino-cli"
  echo "Or install Arduino IDE 2.x from https://www.arduino.cc/en/software"
  read -p "Press Enter to exit..."
  exit 1
fi

echo "Using arduino-cli: $CLI"
echo "$($CLI version)"

# ── Ensure ESP32 core is installed ───────────────────────────────────────────
if ! "$CLI" core list 2>/dev/null | grep -q "esp32:esp32"; then
  echo ""
  echo "Installing ESP32 core (first time — may take a few minutes)..."
  "$CLI" config add board_manager.additional_urls \
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json 2>/dev/null || true
  "$CLI" core update-index
  "$CLI" core install esp32:esp32
fi

# ── Ensure TFT_eSPI is installed ─────────────────────────────────────────────
if ! "$CLI" lib list 2>/dev/null | grep -q "TFT_eSPI"; then
  echo "Installing TFT_eSPI library..."
  "$CLI" lib install "TFT_eSPI"
fi

# ── Patch User_Setup.h ────────────────────────────────────────────────────────
LIB_DIR=$("$CLI" config dump --format json 2>/dev/null \
  | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('directories',{}).get('user',''))" 2>/dev/null)
if [ -z "$LIB_DIR" ]; then
  LIB_DIR="$HOME/Documents/Arduino"
fi
TFTESPI_SETUP="$LIB_DIR/libraries/TFT_eSPI/User_Setup.h"
if [ -f "$TFTESPI_SETUP" ]; then
  cp "firmware/love_slides_cyd/User_Setup.h" "$TFTESPI_SETUP"
  echo "Patched User_Setup.h → $TFTESPI_SETUP"
fi

# ── Compile ───────────────────────────────────────────────────────────────────
echo ""
echo "Compiling firmware..."
rm -rf "$BUILD_DIR"
"$CLI" compile \
  --fqbn esp32:esp32:esp32 \
  --output-dir "$BUILD_DIR" \
  "$SKETCH"

# ── Find the merged binary ────────────────────────────────────────────────────
BIN=$(ls "$BUILD_DIR"/*.merged.bin 2>/dev/null | head -1)
if [ -z "$BIN" ]; then
  BIN=$(ls "$BUILD_DIR"/*.bin 2>/dev/null | grep -v bootloader | grep -v partition | head -1)
fi
if [ -z "$BIN" ]; then
  echo "ERROR: No .bin found in $BUILD_DIR"
  ls "$BUILD_DIR"
  read -p "Press Enter to exit..."
  exit 1
fi

echo "Binary: $BIN ($(wc -c < "$BIN") bytes)"

# ── Embed into HTML ───────────────────────────────────────────────────────────
# Use Python to avoid shell argument-length limits on the large base64 string
echo "Embedding binary into HTML..."
python3 - "$BIN" "$WEB_FLASHER" "$DOCS" <<'PYEOF'
import sys, re, base64, pathlib

bin_path = sys.argv[1]
b64 = base64.b64encode(open(bin_path, 'rb').read()).decode()
print(f"Base64 length: {len(b64)}")

for path in sys.argv[2:]:
    p = pathlib.Path(path)
    if not p.exists():
        continue
    content = p.read_text()
    new_content = re.sub(r'const FIRMWARE_B64 = "[^"]*";', f'const FIRMWARE_B64 = "{b64}";', content)
    p.write_text(new_content)
    print(f"✓ Embedded into {path}")
PYEOF

echo ""
echo "================================================"
echo "  Done! Binary embedded."
echo "  Double-click start.command to launch the flasher."
echo "================================================"
read -p "Press Enter to close..."
