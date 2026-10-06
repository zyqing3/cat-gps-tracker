/**
 * power.h —— 省电状态机（纯逻辑，不依赖硬件、不依赖 Arduino）
 *
 * 决定"一个上报周期怎么走"：睡 → 醒 → 定位 → 上传 → 再睡，
 * 每条路径（上传失败/搜不到星/断网/模块死机）下一步去哪都定义清楚。
 * 固件把硬件结果翻译成事件喂给它，它只做决策——所以本机就能测。
 * 依据：docs/design/low-power-strategy.md（#20 调研结论）。
 */

#pragma once

#include <cstdint>

namespace power {

// 周期状态：固件按这个顺序干活，走到 Sleep 就深睡，醒来重新从 WakeModem 开始
enum class State {
    WakeModem,     // 唤醒 4G 模块（DTR 拉低等就绪，必要时重发 PWRKEY 脉冲）
    EnableGps,     // 开 GNSS 总电源 → 开天线供电 → 发热启动指令
    WaitFix,       // 轮询等定位（超时 GPS_TIMEOUT_S）
    Upload,        // 组装报文 + HTTP 上传（失败重试 1 次）
    CheckNetwork,  // 查网络，断了重连
    Sleep,         // 决定睡多久，然后深睡
};

// 事件：固件把硬件/网络的结果翻译成事件，喂给状态机
enum class Event {
    ModemReady,         // 模块唤醒成功
    ModemDead,          // 模块无响应（重发脉冲后仍不行）
    GpsReady,           // GNSS 初始化完成，开始等定位
    GpsFix,             // 定位成功
    GpsTimeout,         // 等定位超时
    UploadOk,           // 上传成功（HTTP 200）
    UploadFailed,       // 上传失败（重试 1 次后仍失败）
    NetworkOk,          // 网络正常
    NetworkReconnected, // 断网后重连成功
    ReconnectFailed,    // 断网且重连失败
    SleepDone,          // 睡眠结束（深睡唤醒=重启，开始新周期）
};

// 参数（对应 config.h 的低功耗参数，#20 调研结论）
struct Params {
    uint32_t reportIntervalS = 900; // 上报间隔（默认 15 分钟）
    uint32_t gpsFixTimeoutS = 120;  // 等定位的最长时间
};

// 上传重试策略：第 1 次失败重试 1 次，共 2 次尝试
bool shouldRetryUpload(int attempt);

// 睡眠决策：这个周期结束后睡多久、为什么
struct SleepDecision {
    uint32_t sleepSeconds;
    const char *reason;
};

// 一个周期的状态机。用法：
//   Cycle c;                       // 新周期从 WakeModem 开始
//   c.on(ModemReady); c.on(...);   // 固件边干活边喂事件
//   到达 Sleep 后：c.sleepPlan(p)  // 拿到睡眠时长，深睡，醒来喂 SleepDone
class Cycle {
public:
    Cycle() : state_(State::WakeModem), succeeded_(false), uploaded_(false), lastResult_("") {}

    State state() const { return state_; }
    bool succeeded() const { return succeeded_; }

    // 处理一个事件，返回新状态。
    // 遇到"没预想到的事件"会兜底进 Sleep（绝不卡死在某个状态）。
    State on(Event e);

    // 仅在 Sleep 态调用：本周期怎么睡
    SleepDecision sleepPlan(const Params &p) const;

private:
    State state_;
    bool succeeded_;      // 本周期是否完整成功（定位+上传都 OK）
    bool uploaded_;       // 本周期上传是否成功（决定 succeeded_）
    const char *lastResult_; // 进入 Sleep 的原因（日志用）
};

} // namespace power
