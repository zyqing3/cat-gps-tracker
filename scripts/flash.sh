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
# 为什么不用 arduino-cli upload：
#   1) 它默认把波特率提到 921600，本板 + 这根 USB 线在高波特率下断流（No serial data received）；
#      460800 多数时候可用，但板子带载（模组开着）时也可能断——最稳是 --no-stub 慢速直写。
#   2) 它的 merged 整包在编译缓存命中时不会重新生成（可能烧进去旧固件！），
#      而各独立 bin（bootloader/partitions/app）每次都会刷新。
# 所以用 esptool 直接烧 4 个独立文件（分区布局：app3M_fat9M_16MB），走 no-stub。
ESPT=$(ls -d "$LOCALAPPDATA"/Arduino15/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
[ -x "$ESPT" ] || ESPT=$(ls -d "$HOME/.arduino15/packages/esp32/tools/esptool_py"/*/esptool.exe 2>/dev/null | head -1)
if [ ! -x "$ESPT" ]; then
  echo "❌ 找不到 esptool，请先编译过（arduino-cli 会自动装）"
  exit 1
fi

# 低功耗固件深睡时串口会消失：烧录要趁板子醒着，或按住 BOOT 点一下 RST 进下载模式
"$ESPT" --port "$PORT" --no-stub write_flash \
  0x0     firmware/build/gps_collar.ino.bootloader.bin \
  0x8000  firmware/build/gps_collar.ino.partitions.bin \
  0xe000  firmware/build/boot_app0_flashed.bin \
  0x10000 firmware/build/gps_collar.ino.bin

echo "✅ 烧录成功（看日志：python scripts/live_monitor.py 或 tail -f logs/live.log）"
