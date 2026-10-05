/**
 * make_payload.cpp —— 模拟上报小工具（本机运行，不需要硬件）
 *
 * 用与固件完全相同的 payload 模块生成一条真实上报，
 * 输出两行：第一行 URL，第二行上报正文。配合 curl 使用：
 *
 *   bash scripts/simulate_report.sh [纬度] [经度]
 *
 * 默认定位在北京天安门（39.9042, 116.4074），时间取当前 UTC 时间。
 */

#include "payload.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

int main(int argc, char **argv)
{
    payload::Config cfg;
    cfg.server   = "traccar.atoo.top";
    cfg.port     = 8081;
    cfg.deviceId = "cat-collar-001";
    cfg.batt     = 100;

    payload::Location loc;
    loc.hasFix  = true;
    loc.lat     = (argc > 1) ? std::atof(argv[1]) : 39.9042;  // 默认：北京天安门
    loc.lon     = (argc > 2) ? std::atof(argv[2]) : 116.4074;
    loc.speed   = 0.0;
    loc.altitude = 44.0;

    // 时间用当前 UTC 时间（GPS 给的时间就是 UTC）
    std::time_t now = std::time(nullptr);
    std::tm *utc = std::gmtime(&now);
    loc.year   = utc->tm_year + 1900;
    loc.month  = utc->tm_mon + 1;
    loc.day    = utc->tm_mday;
    loc.hour   = utc->tm_hour;
    loc.minute = utc->tm_min;
    loc.second = utc->tm_sec;

    std::printf("%s\n%s\n",
                payload::buildUrl(cfg).c_str(),
                payload::buildBody(cfg, loc).c_str());
    return 0;
}
