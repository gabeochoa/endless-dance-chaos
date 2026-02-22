#!/bin/bash
set -e

LABEL="${1:-opengl}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TESTS_DIR="$PROJECT_DIR/tests/e2e"
TMP_DIR="$PROJECT_DIR/tests/e2e_screenshot_tmp"
SCREENSHOT_SRC="$PROJECT_DIR/tests/e2e/screenshots"
DEST_DIR="$PROJECT_DIR/screenshots/$LABEL"

echo "=== Screenshot all E2E tests (label: $LABEL) ==="

rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

for f in "$TESTS_DIR"/*.e2e; do
    base="$(basename "$f" .e2e)"
    cp "$f" "$TMP_DIR/$base.e2e"
    # Append a final screenshot so every test gets at least one capture
    printf '\nscreenshot %s_final\nwait_frames 1\n' "$base" >> "$TMP_DIR/$base.e2e"
done

rm -rf "$SCREENSHOT_SRC"
mkdir -p "$SCREENSHOT_SRC"

echo "Running test suite..."
cd "$PROJECT_DIR"
timeout 600 ./output/dance.exe --test-mode --test-dir "$TMP_DIR" 2>&1 | grep -E '\[PASS\]|\[FAIL\]|Summary|Scripts|finished'

mkdir -p "$DEST_DIR"
cp "$SCREENSHOT_SRC"/*.png "$DEST_DIR/" 2>/dev/null || true
COUNT=$(ls -1 "$DEST_DIR"/*.png 2>/dev/null | wc -l | tr -d ' ')
echo "=== Saved $COUNT screenshots to screenshots/$LABEL/ ==="

rm -rf "$TMP_DIR"
