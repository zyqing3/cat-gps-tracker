/**
 * test_payload.cpp —— 上报报文模块的本机测试（Windows g++ 运行，不依赖硬件）
 *
 * 只测外部行为：给定输入，产出正确的上报串；不测内部实现细节。
 * 运行方式：bash scripts/test_payload.sh
 */

#include "payload.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

// 简单的测试断言，不依赖任何测试框架
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected)                                         \
    do {                                                                   \
        std::string a_ = (actual);                                         \
        std::string e_ = (expected);                                       \
        if (a_ != e_) {                                                    \
            std::printf("FAIL %s:%d  %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", \
                        __FILE__, __LINE__, #actual " == " #expected,      \
                        a_.c_str(), e_.c_str());                           \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// 默认配置：与 config.h 的默认值一致
static payload::Config defaultConfig()
{
    payload::Config cfg;
    cfg.server   = "traccar.atoo.top";
    cfg.port     = 8081;
    cfg.deviceId = "cat-collar-001";
    cfg.batt     = 100;
    return cfg;
}

// 一个正常的定位（上海人民广场附近），时间是 UTC
static payload::Location fixedLocation()
{
    payload::Location loc;
    loc.lat     = 31.2304;
    loc.lon     = 121.4737;
    loc.speed   = 0.0;
    loc.altitude = 4.2;
    loc.year    = 2026;
    loc.month   = 10;
    loc.day     = 5;
    loc.hour    = 8;
    loc.minute  = 0;
    loc.second  = 0;
    loc.hasFix  = true;
    return loc;
}

static void testFullBody()
{
    // 与 docs/design/payload-fields.md 第 1 节的示例完全一致
    CHECK_EQ(payload::buildBody(defaultConfig(), fixedLocation()),
             "deviceid=cat-collar-001&lat=31.2304000&lon=121.4737000"
             "&timestamp=2026-10-05T08:00:00Z&speed=0.00&altitude=4.20&batt=100");
}

static void testNoFixSkips()
{
    payload::Location loc = fixedLocation();
    loc.hasFix = false;
    // 搜不到星：返回空串，表示跳过本次上报
    CHECK_EQ(payload::buildBody(defaultConfig(), loc), "");
}

static void testMissingTimeOmitsTimestamp()
{
    payload::Location loc = fixedLocation();
    loc.year = 0; // 还没拿到有效时间
    CHECK_EQ(payload::buildBody(defaultConfig(), loc),
             "deviceid=cat-collar-001&lat=31.2304000&lon=121.4737000"
             "&speed=0.00&altitude=4.20&batt=100");
}

static void testNegativeCoordinates()
{
    // 南半球/西半球：负号要正确保留
    payload::Location loc = fixedLocation();
    loc.lat = -33.8688;
    loc.lon = -151.2093;
    CHECK_EQ(payload::buildBody(defaultConfig(), loc),
             "deviceid=cat-collar-001&lat=-33.8688000&lon=-151.2093000"
             "&timestamp=2026-10-05T08:00:00Z&speed=0.00&altitude=4.20&batt=100");
}

static void testLatLonPrecision()
{
    // lat/lon 必须保留 7 位小数（小数点后不足补 0）
    payload::Location loc = fixedLocation();
    loc.lat = 0.1;
    loc.lon = 2.0;
    CHECK_EQ(payload::buildBody(defaultConfig(), loc),
             "deviceid=cat-collar-001&lat=0.1000000&lon=2.0000000"
             "&timestamp=2026-10-05T08:00:00Z&speed=0.00&altitude=4.20&batt=100");
}

static void testUrl()
{
    CHECK_EQ(payload::buildUrl(defaultConfig()), "http://traccar.atoo.top:8081/");
}

static void testConfigValuesAreUsed()
{
    // 换了设备号和电量，报文要跟着变
    payload::Config cfg = defaultConfig();
    cfg.deviceId = "cat-collar-002";
    cfg.batt = 57;
    CHECK_EQ(payload::buildBody(cfg, fixedLocation()),
             "deviceid=cat-collar-002&lat=31.2304000&lon=121.4737000"
             "&timestamp=2026-10-05T08:00:00Z&speed=0.00&altitude=4.20&batt=57");
}

int main()
{
    testFullBody();
    testNoFixSkips();
    testMissingTimeOmitsTimestamp();
    testNegativeCoordinates();
    testLatLonPrecision();
    testUrl();
    testConfigValuesAreUsed();

    if (g_failures == 0) {
        std::printf("All tests passed (7 cases)\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
