/**
 * test_power.cpp —— 省电状态机的本机测试（Windows g++ 运行，不依赖硬件）
 *
 * 只测外部行为：给定事件序列，状态机的迁移和睡眠决策正确；不测内部实现。
 * 运行方式：bash scripts/test_power.sh
 */

#include "power.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;

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

#define CHECK_STREQ(actual, expected)                                      \
    do {                                                                   \
        if (std::strcmp((actual), (expected)) != 0) {                      \
            std::printf("FAIL %s:%d  %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", \
                        __FILE__, __LINE__, #actual " == " #expected,      \
                        (actual), (expected));                             \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using power::Cycle;
using power::Event;
using power::State;

// 帮手下：喂一串事件，返回最终状态
static State feed(Cycle &c, std::initializer_list<Event> events)
{
    State s = c.state();
    for (Event e : events) {
        s = c.on(e);
    }
    return s;
}

static void testHappyPath()
{
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsFix,
                       Event::UploadOk, Event::NetworkOk});
    CHECK(s == State::Sleep);
    CHECK(c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK(d.sleepSeconds == 900u);
    CHECK_STREQ(d.reason, "normal");
}

static void testNoFixSkips()
{
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsTimeout});
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK(d.sleepSeconds == 900u);
    CHECK_STREQ(d.reason, "no GPS fix, skip this report");
}

static void testUploadFailedThenNetworkOk()
{
    // 上传失败 → 查网络发现正常 → 睡，周期不算成功
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsFix,
                       Event::UploadFailed, Event::NetworkOk});
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded()); // 上传没成，即使网络正常也不算成功

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "upload failed");
}

static void testUploadFailedThenReconnectFailed()
{
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsFix,
                       Event::UploadFailed, Event::ReconnectFailed});
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "upload failed, reconnect failed");
}

static void testNetworkLostThenReconnected()
{
    // 上传成功但发现断网 → 重连成功 → 周期算成功（上传确实送达了）
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsFix,
                       Event::UploadOk, Event::NetworkReconnected});
    CHECK(s == State::Sleep);
    CHECK(c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "reconnected after network loss");
}

static void testModemDeadAtStart()
{
    Cycle c;
    State s = c.on(Event::ModemDead);
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "modem dead, retry next cycle");
}

static void testModemDeadMidCycle()
{
    Cycle c;
    State s = feed(c, {Event::ModemReady, Event::GpsReady, Event::ModemDead});
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "modem dead while waiting GPS");
}

static void testSleepDoneStartsNewCycle()
{
    Cycle c;
    feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsFix,
             Event::UploadOk, Event::NetworkOk});
    CHECK(c.succeeded());

    // 睡醒 → 回到新周期的起点，成败标记清零
    State s = c.on(Event::SleepDone);
    CHECK(s == State::WakeModem);
    CHECK(!c.succeeded());

    // 新周期能正常走完
    feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsTimeout});
    CHECK(c.state() == State::Sleep);
}

static void testUnexpectedEventFailsafe()
{
    // 在 WakeModem 收到 GpsFix（翻译错误等）→ 兜底进 Sleep，绝不卡死
    Cycle c;
    State s = c.on(Event::GpsFix);
    CHECK(s == State::Sleep);
    CHECK(!c.succeeded());

    power::SleepDecision d = c.sleepPlan(power::Params());
    CHECK_STREQ(d.reason, "unexpected event, sleep and restart cycle");
}

static void testUploadRetryPolicy()
{
    CHECK(power::shouldRetryUpload(1) == true);  // 第 1 次失败 → 重试
    CHECK(power::shouldRetryUpload(2) == false); // 第 2 次失败 → 放弃
}

static void testCustomParams()
{
    power::Params p;
    p.reportIntervalS = 1800; // 30 分钟
    Cycle c;
    feed(c, {Event::ModemReady, Event::GpsReady, Event::GpsTimeout});
    power::SleepDecision d = c.sleepPlan(p);
    CHECK(d.sleepSeconds == 1800u);
}

static void testDefaultParams()
{
    power::Params p;
    CHECK(p.reportIntervalS == 900u); // #20 调研推荐默认 15 分钟
    CHECK(p.gpsFixTimeoutS == 120u);
}

int main()
{
    testHappyPath();
    testNoFixSkips();
    testUploadFailedThenNetworkOk();
    testUploadFailedThenReconnectFailed();
    testNetworkLostThenReconnected();
    testModemDeadAtStart();
    testModemDeadMidCycle();
    testSleepDoneStartsNewCycle();
    testUnexpectedEventFailsafe();
    testUploadRetryPolicy();
    testCustomParams();
    testDefaultParams();

    if (g_failures == 0) {
        std::printf("All tests passed (12 cases)\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
