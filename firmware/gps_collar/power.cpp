/**
 * power.cpp —— 省电状态机实现
 *
 * 只用标准库，固件（Arduino ESP32）和本机（Windows g++）都能编译。
 */

#include "power.h"

namespace power {

bool shouldRetryUpload(int attempt)
{
    return attempt == 1; // 第 1 次失败重试 1 次；第 2 次失败就放弃，等下个周期
}

State Cycle::on(Event e)
{
    bool handled = false;

    switch (state_) {
    case State::WakeModem:
        if (e == Event::ModemReady) {
            state_ = State::EnableGps;
            handled = true;
        } else if (e == Event::ModemDead) {
            // 模块没反应：本周期放弃，睡一觉下个周期再试
            // （重发 PWRKEY 脉冲是固件在 WakeModem 步骤里做的事）
            state_ = State::Sleep;
            lastResult_ = "modem dead, retry next cycle";
            handled = true;
        }
        break;

    case State::EnableGps:
        if (e == Event::GpsReady) {
            state_ = State::WaitFix;
            handled = true;
        } else if (e == Event::ModemDead) {
            state_ = State::Sleep;
            lastResult_ = "modem dead while enabling GPS";
            handled = true;
        }
        break;

    case State::WaitFix:
        if (e == Event::GpsFix) {
            state_ = State::Upload;
            handled = true;
        } else if (e == Event::GpsTimeout) {
            // 搜不到星：跳过本次上报，睡满一个周期再试
            // （GPS 冷启动是耗电大头，缩短睡眠重试反而更费电）
            state_ = State::Sleep;
            lastResult_ = "no GPS fix, skip this report";
            handled = true;
        } else if (e == Event::ModemDead) {
            state_ = State::Sleep;
            lastResult_ = "modem dead while waiting GPS";
            handled = true;
        }
        break;

    case State::Upload:
        if (e == Event::UploadOk) {
            uploaded_ = true;
            state_ = State::CheckNetwork;
            handled = true;
        } else if (e == Event::UploadFailed) {
            // 上传失败常常是因为网络断了：先去查网络/重连，再睡
            state_ = State::CheckNetwork;
            handled = true;
        } else if (e == Event::ModemDead) {
            state_ = State::Sleep;
            lastResult_ = "modem dead while uploading";
            handled = true;
        }
        break;

    case State::CheckNetwork:
        if (e == Event::NetworkOk) {
            state_ = State::Sleep;
            succeeded_ = uploaded_; // 只有上传过且网络正常才算完整成功
            lastResult_ = uploaded_ ? "normal" : "upload failed";
            handled = true;
        } else if (e == Event::NetworkReconnected) {
            state_ = State::Sleep;
            succeeded_ = uploaded_;
            lastResult_ = uploaded_ ? "reconnected after network loss"
                                    : "upload failed, network reconnected";
            handled = true;
        } else if (e == Event::ReconnectFailed) {
            state_ = State::Sleep;
            lastResult_ = uploaded_ ? "network lost, reconnect failed"
                                    : "upload failed, reconnect failed";
            handled = true;
        } else if (e == Event::ModemDead) {
            state_ = State::Sleep;
            lastResult_ = "modem dead during network check";
            handled = true;
        }
        break;

    case State::Sleep:
        if (e == Event::SleepDone) {
            // 醒来 = 新周期，从唤醒模块重新开始
            state_ = State::WakeModem;
            succeeded_ = false;
            uploaded_ = false;
            lastResult_ = "";
            handled = true;
        }
        break;
    }

    // 兜底：没预想到的事件组合（例如在 WaitFix 收到 UploadOk）→ 进 Sleep，
    // 睡一觉重新开始，绝不卡死在某个状态
    if (!handled && state_ != State::Sleep) {
        state_ = State::Sleep;
        lastResult_ = "unexpected event, sleep and restart cycle";
    }

    return state_;
}

SleepDecision Cycle::sleepPlan(const Params &p) const
{
    // 成功和失败目前都睡满一个上报间隔：
    // 失败时缩短睡眠能更快重试，但失败期正是耗电最凶的时候，缩睡反而加速耗电；
    // 待 #23 功耗实测后如需差异化（比如失败睡更短/更长），只改这里一处。
    SleepDecision d;
    d.sleepSeconds = p.reportIntervalS;
    d.reason = (lastResult_[0] != '\0') ? lastResult_ : "normal";
    return d;
}

} // namespace power
