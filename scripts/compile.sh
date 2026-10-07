#!/bin/bash
# 编译 GPS 项圈固件（T-SIM7670G-S3 板子参数）
# 用法：
#   bash scripts/compile.sh <草图文件>    例如 bash scripts/compile.sh firmware/gps_collar
# 前提：
#   - 已安装 Arduino CLI（winget install ArduinoSA.CLI）
#   - 已克隆 vendor/LilyGo-Modem-Series（git clone --depth 1 --filter=blob:none --sparse 后 sparse-checkout lib examples）
#   - 网络受限时所有 ESP32 核心文件已缓存到 Arduino15/staging（见 docs/research 报告与记忆）

set -e
cd "$(dirname "$0")/.."

AC="/c/Program Files/Arduino CLI/arduino-cli.exe"
[ -x "$AC" ] || AC="arduino-cli"   # 重启 Claude Code 后 PATH 里已有 arduino-cli 时用这个

SKETCH="${1:-vendor/LilyGo-Modem-Series/examples/GPS_BuiltIn}"

"$AC" compile \
  --fqbn "esp32:esp32:esp32s3:PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio" \
  --build-property "compiler.cpp.extra_flags=-DLILYGO_SIM7670G_S3_STAN" \
  --libraries vendor/LilyGo-Modem-Series/lib \
  "$SKETCH"

echo "✅ 编译成功"
