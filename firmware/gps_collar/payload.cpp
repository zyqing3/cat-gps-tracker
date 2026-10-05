/**
 * payload.cpp —— 上报报文模块实现
 *
 * 只用标准库，固件（Arduino ESP32）和本机（Windows g++）都能编译。
 */

#include "payload.h"

#include <cstdio>

namespace payload {

std::string buildBody(const Config &cfg, const Location &loc)
{
    if (!loc.hasFix) {
        // 搜不到星：跳过本次上报
        return "";
    }

    // 200 字节足够：字段全满时约 150 字节
    char buf[200];

    int n = std::snprintf(buf, sizeof(buf),
                          "deviceid=%s&lat=%.7f&lon=%.7f",
                          cfg.deviceId.c_str(), loc.lat, loc.lon);

    // 拿到有效时间（GPS 时间，UTC）才带 timestamp；
    // 没有就省略，Traccar 会按服务器收到的时间记录
    if (loc.year >= 2000) {
        n += std::snprintf(buf + n, sizeof(buf) - n,
                           "&timestamp=%04d-%02d-%02dT%02d:%02d:%02dZ",
                           loc.year, loc.month, loc.day,
                           loc.hour, loc.minute, loc.second);
    }

    n += std::snprintf(buf + n, sizeof(buf) - n,
                       "&speed=%.2f&altitude=%.2f&batt=%d",
                       loc.speed, loc.altitude, cfg.batt);

    return std::string(buf);
}

std::string buildUrl(const Config &cfg)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "http://%s:%d/", cfg.server.c_str(), cfg.port);
    return std::string(buf);
}

} // namespace payload
