#!/bin/bash
# 上传 IR Copier 固件到 M5StickS3
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

FQBN="m5stack:esp32:m5stack_sticks3"
PORT="${PORT:-/dev/cu.usbmodem101}"

if [ ! -f "$BUILD_DIR/ir_copier.ino.bin" ]; then
  echo "Firmware not found. Running build first..."
  "$SCRIPT_DIR/build.sh"
fi

echo "=== Uploading to M5StickS3 ==="
echo "Port: $PORT"
echo ""

# 检查端口是否存在
if [ ! -e "$PORT" ]; then
  echo "ERROR: Port $PORT not found."
  echo "If the device is running firmware, hold BOOT and re-plug USB to enter download mode."
  exit 1
fi

arduino-cli upload \
  --fqbn "$FQBN" \
  --port "$PORT" \
  --build-path "$BUILD_DIR" \
  "$PROJECT_ROOT/src/ir_copier/ir_copier.ino"

echo ""
echo "=== Upload complete ==="