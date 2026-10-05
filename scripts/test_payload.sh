#!/bin/bash
# 上报报文模块的本机测试（Windows，不需要硬件）
# 用法：bash scripts/test_payload.sh
# 前提：本机有 g++（本机已确认：Qt 自带 MinGW，路径 C:/Qt/Tools/mingw1310_64/bin/g++.exe）

set -e
cd "$(dirname "$0")/.."

GXX="/c/Qt/Tools/mingw1310_64/bin/g++.exe"
[ -x "$GXX" ] || GXX="g++"

"$GXX" -std=c++17 -Wall -Wextra \
  -I firmware/gps_collar \
  tests/test_payload.cpp firmware/gps_collar/payload.cpp \
  -o tests/test_payload.exe

./tests/test_payload.exe
echo "✅ 本机测试全部通过"
