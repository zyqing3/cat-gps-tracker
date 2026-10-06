#!/bin/bash
# 省电状态机的本机测试（Windows，不需要硬件）
# 用法：bash scripts/test_power.sh
# 前提：本机有 g++（本机已确认：Qt 自带 MinGW，路径 C:/Qt/Tools/mingw1310_64/bin/g++.exe）

set -e
cd "$(dirname "$0")/.."

GXX="/c/Qt/Tools/mingw1310_64/bin/g++.exe"
[ -x "$GXX" ] || GXX="g++"

"$GXX" -std=c++17 -Wall -Wextra \
  -I firmware/gps_collar \
  tests/test_power.cpp firmware/gps_collar/power.cpp \
  -o tests/test_power.exe

./tests/test_power.exe
echo "✅ 本机测试全部通过"
