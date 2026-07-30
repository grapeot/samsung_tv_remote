#!/bin/bash
# 编译 IR Copier 固件
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
SKETCH="$PROJECT_ROOT/src/samsung_tv_remote/samsung_tv_remote.ino"

FQBN="m5stack:esp32:m5stack_sticks3"
PORT="${PORT:-/dev/cu.usbmodem101}"

mkdir -p "$BUILD_DIR"

echo "=== Compiling IR Copier for M5StickS3 ==="
echo "FQBN: $FQBN"
echo "Sketch: $SKETCH"
echo ""

arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_DIR" \
  "$SKETCH"

echo ""
echo "=== Build complete ==="
echo "Output: $BUILD_DIR/ir_copier.ino.bin"