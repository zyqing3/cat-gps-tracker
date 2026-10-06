/**
 * GPS 项圈固件 —— 低功耗版（600mAh 电池）
 *
 * 功能：睡 15 分钟（可调）→ 醒来定位 → 4G 上传 → 再睡。
 *       一个周期由省电状态机（power 模块）驱动，决策逻辑本机可测。
 * 板子：LilyGO T-SIM7670G-S3（ESP32-S3 + SIM7670G，内置 GPS）
 *
 * 与常开版（main 分支）的区别：
 *   - ESP32 深睡眠，RTC 定时唤醒（醒来=重启，重新走一个周期）
 *   - 4G 模块睡眠不关机（DTR 控制，网络保持附着，醒来直接发）
 *   - GPS 天线 + GNSS 电源随定位开关，睡时全关
 *   - 每次醒来发热启动指令（星历还在就几秒定位，否则自动冷启动）
 *
 * 依据：docs/design/low-power-strategy.md（#20 调研）、官方 Traccar 示例、
 *       状态机 docs/design/low-power-strategy.md §6-7
 */

// 接收缓冲区要够大（默认 256 太小，官方示例用 1024）
#define TINY_GSM_RX_BUFFER 1024

#include "utilities.h"          // 板子引脚定义（RX=10/TX=11/PWRKEY=18/DTR=9，以这份为准）
#include <TinyGsmClient.h>      // LilyGo 分支版 TinyGSM（原版编译不过）
#include <ArduinoHttpClient.h>  // 纯 HTTP 客户端（服务器 8081 是明文 HTTP）
#include "config.h"
#include "payload.h"
#include "power.h"

// 4G 模块对象：通过 Serial1 和 SIM7670G 通信
TinyGsm modem(SerialAT);
// 基于 4G 模块的网络客户端
TinyGsmClient gsmClient(modem);
// 上报配置（从 config.h 填入）
payload::Config cfg;
// 省电参数（从 config.h 填入）
power::Params params;

// 打印带运行秒数的日志行
void logStep(const char *msg)
{
    Serial.printf("[%lu] %s\n", millis() / 1000, msg);
}


// ---------- 状态机各步骤（把硬件结果翻译成事件喂给状态机） ----------

// 唤醒 4G 模块：DTR 拉低 → 等 AT 就绪 → 必要时 PWRKEY 脉冲重开 → 确保网络在线
power::Event stepWakeModem()
{
    // DTR 拉低唤醒模块（官方示例同款，醒来后网络附着还在）
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
    delay(500);

    // 等 AT 就绪（最多 10 秒）
    bool ready = false;
    for (int i = 0; i < 10 && !ready; ++i) {
        ready = modem.testAT(1000);
    }

    if (!ready) {
        // 模块彻底没反应：PWRKEY 脉冲重开（第一次上电也会走这里）
        logStep("modem not responding, send PWRKEY pulse");
        pinMode(BOARD_PWRKEY_PIN, OUTPUT);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);
        delay(100);
        digitalWrite(BOARD_PWRKEY_PIN, HIGH);
        delay(MODEM_POWERON_PULSE_WIDTH_MS);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);

        // 重开后最多再等 30 秒
        for (int i = 0; i < 30 && !ready; ++i) {
            ready = modem.testAT(1000);
        }
    }

    if (!ready) {
        logStep("modem still dead, give up this cycle");
        return power::Event::ModemDead;
    }

    // 确保网络在线：睡眠唤醒后附着通常还在；被 PWRKEY 重开过则要重新注册
    RegStatus status = modem.getRegistrationStatus();
    if (status != REG_OK_HOME && status != REG_OK_ROAMING) {
        logStep("network lost, waiting for registration...");
        unsigned long t0 = millis();
        while (millis() - t0 < 60000UL) {
            status = modem.getRegistrationStatus();
            if (status == REG_OK_HOME || status == REG_OK_ROAMING) {
                break;
            }
            delay(1000);
        }
        logStep("activating network (GPRS)");
        modem.setNetworkActive(APN);
    }
    return power::Event::ModemReady;
}

// 开 GNSS：天线供电 + GNSS 总电源（库内 CGDRT=4,1 + CGSETV=4,1 + CGNSSPWR=1）
power::Event stepEnableGps()
{
    if (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) {
        return power::Event::ModemDead;
    }
    delay(2000); // GNSS 上电初始化

    modem.setGPSBaud(115200);
#if defined(TINY_GSM_MODEM_SIM7670G)
    // SIM7670G 支持 GPS/GLONASS/GALILEO/北斗四系统
    modem.setGPSMode(GNSS_MODE_GPS_GLONASS_GALILEO_BDS);
#endif

    // 热启动"免费彩票"：星历还在 → 几秒定位；不在 → 自动按冷启动搜，无害
    modem.gpsHotStart();
    logStep("GNSS powered on, waiting for fix");
    return power::Event::GpsReady;
}

// 等定位：轮询到成功或超时（GPS_TIMEOUT_S）
power::Event stepWaitFix(GPSInfo &info)
{
    unsigned long t0 = millis();
    while (millis() - t0 < params.gpsFixTimeoutS * 1000UL) {
        if (modem.getGPS_Ex(info)) {
            Serial.printf("[%lu] GPS fix: lat=%.6f lon=%.6f sats=%d\n",
                          millis() / 1000,
                          info.latitude, info.longitude, info.gps_satellite_num);
            return power::Event::GpsFix;
        }
        delay(1000);
    }
    logStep("GPS timeout, no fix this cycle");
    return power::Event::GpsTimeout;
}

// 真正发 HTTP POST（15 秒没响应就放弃）
int httpPost(const std::string &body)
{
    HttpClient http(gsmClient, cfg.server.c_str(), cfg.port);
    http.setHttpResponseTimeout(15000);
    int code = http.post("/", "application/x-www-form-urlencoded", String(body.c_str()));
    http.stop();
    return code;
}

// 上传：组装报文 → POST，失败按状态机策略重试 1 次
power::Event stepUpload(const GPSInfo &info)
{
    // 模块的 GPSInfo → 报文模块的 Location
    payload::Location loc;
    loc.hasFix   = (info.isFix == 2 || info.isFix == 3);
    loc.lat      = info.latitude;
    loc.lon      = info.longitude;
    loc.speed    = info.speed;     // 节（1 节 ≈ 1.85 km/h）
    loc.altitude = info.altitude;  // 米
    loc.year   = info.year;   // GPS 时间，UTC
    loc.month  = info.month;
    loc.day    = info.day;
    loc.hour   = info.hour;
    loc.minute = info.minute;
    loc.second = info.second;

    std::string body = payload::buildBody(cfg, loc);
    Serial.printf("[%lu] Upload: %s\n", millis() / 1000, body.c_str());

    for (int attempt = 1; ; ++attempt) {
        int code = httpPost(body);
        if (code == 200) {
            Serial.printf("[%lu] Upload OK (attempt %d)\n", millis() / 1000, attempt);
            return power::Event::UploadOk;
        }
        Serial.printf("[%lu] Upload failed (attempt %d, HTTP %d)\n", millis() / 1000, attempt, code);
        if (!power::shouldRetryUpload(attempt)) {
            return power::Event::UploadFailed;
        }
    }
}

// 查网络：断了就重连 GPRS
power::Event stepCheckNetwork()
{
    RegStatus status = modem.getRegistrationStatus();
    if (status == REG_OK_HOME || status == REG_OK_ROAMING) {
        return power::Event::NetworkOk;
    }
    logStep("network lost, reconnecting...");
    if (modem.setNetworkActive(APN)) {
        return power::Event::NetworkReconnected;
    }
    return power::Event::ReconnectFailed;
}

// 周期收尾：关 GPS → 让模块睡觉 → ESP32 深睡到下一个周期
void goToSleep(const power::SleepDecision &d)
{
    Serial.printf("[%lu] Cycle end: %s, sleep %lu s\n",
                  millis() / 1000, d.reason, (unsigned long)d.sleepSeconds);

    // 关天线 + GNSS 总电源（库内 CGDRT=4,0 + CGSETV=4,0 + CGNSSPWR=0）
    modem.disableGPS(MODEM_GPS_ENABLE_GPIO, 0);

    // 让模块睡觉：CSCLK=1，配合 DTR 拉高（官方示例同款）
    modem.sleepEnable(true);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, HIGH);
    // 尽量让 DTR 电平保持到深睡期间（待硬件实测验证；
    // 若保持不住，模块只是提前醒来多耗一点电，不影响功能）
    gpio_hold_en((gpio_num_t)MODEM_DTR_PIN);
    delay(100);
    Serial.flush();

    // ESP32 深睡，定时唤醒（醒来 = 重启，重新走一个周期）
    esp_sleep_enable_timer_wakeup((uint64_t)d.sleepSeconds * 1000000ULL);
    esp_deep_sleep_start();
    // 深睡成功的话到不了这里
}


// ---------- 一个完整周期 ----------

void runOneCycle()
{
    power::Cycle cycle;
    GPSInfo info;

    cycle.on(stepWakeModem());
    if (cycle.state() == power::State::EnableGps) {
        cycle.on(stepEnableGps());
    }
    if (cycle.state() == power::State::WaitFix) {
        cycle.on(stepWaitFix(info));
    }
    if (cycle.state() == power::State::Upload) {
        cycle.on(stepUpload(info));
    }
    if (cycle.state() == power::State::CheckNetwork) {
        cycle.on(stepCheckNetwork());
    }

    // 状态机保证到这里必在 Sleep 态（意外事件也会兜底进 Sleep）
    goToSleep(cycle.sleepPlan(params));
}


void setup()
{
    // 串口初始化：USB 串口看日志 + Serial1 连 4G 模块（RX=10/TX=11/115200）
    Serial.begin(115200);
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(100);

    Serial.println();
    Serial.println("=== GPS collar firmware (low-power) ===");
    Serial.println("Log lines show [seconds since boot]; upload lines include UTC time");

    // 从 config.h 填入配置
    cfg.server   = TRACCAR_SERVER;
    cfg.port     = TRACCAR_PORT;
    cfg.deviceId = DEVICE_ID;
    cfg.batt     = BATTERY_PERCENT;

    params.reportIntervalS = REPORT_INTERVAL_S;
    params.gpsFixTimeoutS  = GPS_TIMEOUT_S;

    // 走一个周期：唤醒 → 定位 → 上传 → 再睡（深睡在 goToSleep 末尾）
    runOneCycle();
}

void loop()
{
    // 深睡模式下每次唤醒都是重启，永远到不了这里；
    // 留着以防深睡被跳过时卡死——理论上不会执行。
    delay(1000);
}

// 必须使用 LilyGo 分支版 TinyGSM，否则编译时报错提示
#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/main/lib) to the arduino libraries directory, See README"
#endif
