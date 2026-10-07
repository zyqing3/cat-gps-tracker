#!/bin/bash
# 烧录 GPS 项圈固件到 T-SIM7670G-S3 开发板（先编译到 firmware/build，再烧录）
# 用法：
#   bash scripts/flash.sh          ← 自动找 ESP32 串口
#   bash scripts/flash.sh COM7     ← 手动指定串口
# 前提：板子用 USB 线连电脑（板子参数与编译脚本 compile.sh 一致）

set -e
cd "$(dirname "$0")/.."

AC="/c/Program Files/Arduino CLI/arduino-cli.exe"
[ -x "$AC" ] || AC="arduino-cli"

FQBN="esp32:esp32:esp32s3:PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio"

PORT="${1:-}"
if [ -z "$PORT" ]; then
  # 自动找：board list 里 ESP32 的口
  PORT=$("$AC" board list | grep -i "esp32" | grep -oE "COM[0-9]+" | head -1)
  if [ -z "$PORT" ]; then
    echo "❌ 找不到 ESP32 串口，请把板子插上 USB，或手动指定：bash scripts/flash.sh COM7"
    exit 1
  fi
fi

echo "① 编译固件 ..."
"$AC" compile \
  --fqbn "$FQBN" \
  --build-property "compiler.cpp.extra_flags=-DLILYGO_SIM7670G_S3_STAN" \
  --libraries vendor/LilyGo-Modem-Series/lib \
  --output-dir firmware/build \
  firmware/gps_collar

echo "② 烧录到串口 $PORT ..."
"$AC" upload \
  --fqbn "$FQBN" \
  --input-dir firmware/build \
  --port "$PORT"

echo "✅ 烧录成功（看日志：arduino-cli monitor -p $PORT -c baudrate=115200）"
