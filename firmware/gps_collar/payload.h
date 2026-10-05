/**
 * payload.h —— 上报报文模块（纯逻辑，不依赖硬件、不依赖 Arduino）
 *
 * 把"定位数据"组装成 Traccar OsmAnd 协议的 7 字段上报串。
 * 固件和本机（Windows）测试共用这一份代码，格式改一次两边同步。
 * 字段格式依据 docs/design/payload-fields.md。
 */

#pragma once

#include <string>

namespace payload {

// 定位数据（时间来自 GPS，是 UTC 时间）
struct Location {
    double lat;        // 纬度（十进制度）
    double lon;        // 经度（十进制度）
    double speed;      // 速度（节，1 节 ≈ 1.85 km/h）
    double altitude;   // 海拔（米）
    int year;          // UTC 时间：年（没拿到有效时间时为 0）
    int month;         // 月
    int day;           // 日
    int hour;          // 时
    int minute;        // 分
    int second;        // 秒
    bool hasFix;       // 是否有效定位（搜不到星时为 false）
};

// 上报配置（对应 config.h 里的参数，由主程序填入）
struct Config {
    std::string server;   // 服务器地址，如 "traccar.atoo.top"
    int port;             // 上报端口，如 8081
    std::string deviceId; // 设备号（Traccar 设备的 uniqueId）
    int batt;             // 电量百分比
};

/**
 * 组装上报正文（7 字段 OsmAnd 查询串，不含开头的 "?"）。
 *
 * 返回：
 *   有效定位  → "deviceid=xxx&lat=..&lon=..&timestamp=..&speed=..&altitude=..&batt=.."
 *   没有定位  → 空字符串（表示"跳过本次上报"）
 *   有定位但没拿到有效时间 → 不含 timestamp 字段（服务器按收到的时间记录）
 */
std::string buildBody(const Config &cfg, const Location &loc);

/**
 * 组装上报地址（不含查询串），如 "http://traccar.atoo.top:8081/"
 * 注意服务器是明文 HTTP（8081 没开 https）。
 */
std::string buildUrl(const Config &cfg);

} // namespace payload
