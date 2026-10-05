#!/bin/bash
# 模拟上报：用与固件相同的报文代码生成上报串，POST 到真实 Traccar 服务器
# 用法：bash scripts/simulate_report.sh [纬度] [经度]
#   不带参数默认北京天安门 (39.9042, 116.4074)
# 验证：打开 traccar.atoo.top:8081 网页，地图上 cat-collar-001 出现一个点

set -e
cd "$(dirname "$0")/.."

GXX="/c/Qt/Tools/mingw1310_64/bin/g++.exe"
[ -x "$GXX" ] || GXX="g++"

"$GXX" -std=c++17 -I firmware/gps_collar \
  tests/make_payload.cpp firmware/gps_collar/payload.cpp \
  -o tests/make_payload.exe

# 输出两行：URL 和 上报正文
OUT=$(./tests/make_payload.exe "$@")
URL=$(echo "$OUT" | sed -n '1p')
BODY=$(echo "$OUT" | sed -n '2p')

echo "URL : $URL"
echo "BODY: $BODY"
echo "---"
curl -s -o /dev/null -w "HTTP status: %{http_code}\n" -X POST --data "$BODY" "$URL"
echo "✅ 模拟上报已发送（地图上应出现 cat-collar-001 的位置点）"
