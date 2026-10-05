/**
 * GPS 项圈固件 —— 第一阶段 demo（常开模式）
 *
 * 功能：每 60 秒把 GPS 定位通过 4G 上传到 Traccar 服务器，
 *       服务器网页地图上能看到猫的最新位置。
 * 板子：LilyGO T-SIM7670G-S3（ESP32-S3 + SIM7670G，内置 GPS）
 * 供电：USB/充电宝，常开模式（4G 不断网、GPS 不断电、不休眠）
 *
 * 所有参数在 config.h，改参数不用动本文件。
 * 上报报文由 payload 模块组装（与本机测试共用同一份代码）。
 *
 * 依据：官方 Traccar 示例（vendor/LilyGo-Modem-Series/examples/Traccar/）
 *       设计文档 docs/design/firmware-architecture.md
 */

// 接收缓冲区要够大（默认 256 太小，官方示例用 1024）
#define TINY_GSM_RX_BUFFER 1024

#include "utilities.h"          // 板子引脚定义（RX=10/TX=11/PWRKEY=18，以这份为准，勿信 wiki）
#include <TinyGsmClient.h>      // LilyGo 分支版 TinyGSM（原版编译不过）
#include <ArduinoHttpClient.h>  // 纯 HTTP 客户端（服务器 8081 是明文 HTTP，不用 https）
#include "config.h"
#include "payload.h"

// 4G 模块对象：通过 Serial1 和 SIM7670G 通信
TinyGsm modem(SerialAT);
// 基于 4G 模块的网络客户端
TinyGsmClient gsmClient(modem);
// 上报配置（从 config.h 填入）
payload::Config cfg;

// 打印带运行秒数的日志行（GPS 时间拿到前用开机秒数标注；上传行内含 UTC 时间）
void logStep(const char *msg)
{
    Serial.printf("[%lu] %s\n", millis() / 1000, msg);
}


// ---------- 启动流程四步 ----------

// 第 1 步：串口初始化（USB 串口看日志 + Serial1 连 4G 模块）
void initSerial()
{
    // USB 串口：插电脑在串口监视器看日志
    Serial.begin(115200);
    // 模块串口：RX=10 / TX=11 / 115200
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
}

// 第 2 步：模块开机（先复位模块，再 PWRKEY=18 脉冲唤醒，等 AT 就绪）
bool modemPowerOn()
{
#ifdef MODEM_RESET_PIN
    // 复位模块，确保干净状态
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL); delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    gpio_hold_en((gpio_num_t)MODEM_RESET_PIN);
    gpio_deep_sleep_hold_en();
#endif

    // DTR 拉低 = 模块保持唤醒（常开模式，永不睡眠）
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);

    // PWRKEY 脉冲开机
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    // 等 AT 就绪；约 30 秒没响应就重新发脉冲（对应设计文档错误处理策略）
    Serial.print("Waiting for modem");
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (retry++ > 30) {
            Serial.println("\nModem no response, re-send PWRKEY pulse");
            digitalWrite(BOARD_PWRKEY_PIN, LOW);
            delay(100);
            digitalWrite(BOARD_PWRKEY_PIN, HIGH);
            delay(MODEM_POWERON_PULSE_WIDTH_MS);
            digitalWrite(BOARD_PWRKEY_PIN, LOW);
            retry = 0;
        }
    }
    Serial.println(" OK");

    // 打印模块型号，方便排查问题
    Serial.print("Modem: ");
    Serial.println(modem.getModemName());
    return true;
}

// 第 3 步：联网（查 SIM 卡 → 等注册到运营商网络 → 按 config.h 的 APN 连 GPRS）
bool connectNetwork()
{
    // 1) 等 SIM 卡就绪
    Serial.print("Checking SIM");
    SimStatus sim = SIM_ERROR;
    while (sim != SIM_READY) {
        sim = modem.getSimStatus();
        if (sim == SIM_LOCKED) {
            Serial.println("\nSIM card is locked (needs PIN)");
        }
        Serial.print(".");
        delay(1000);
    }
    Serial.println(" OK");

#ifdef TINY_GSM_MODEM_HAS_NETWORK_MODE
    modem.setNetworkMode(MODEM_NETWORK_AUTO);
#endif

    // 2) 等注册到运营商网络
    Serial.print("Registering to network");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        status = modem.getRegistrationStatus();
        if (status == REG_DENIED) {
            Serial.println("\nNetwork registration denied, check SIM and APN");
            return false;
        }
        Serial.print(".");
        delay(1000);
    }
    Serial.printf(" OK (status %d)\n", status);

    // 3) 连 GPRS（APN 见 config.h，普通卡留空自动识别）
    Serial.print("Connecting GPRS, APN=");
    Serial.println(strlen(APN) ? APN : "(auto)");
    bool ok = false;
    int retry = 3;
    while (retry--) {
        if (modem.setNetworkActive(APN)) {
            ok = true;
            break;
        }
        Serial.println("Enable network failed, retry after 3s...");
        delay(3000);
    }
    if (!ok) {
        Serial.println("Failed to enable network");
        return false;
    }

    Serial.print("Local IP: ");
    Serial.println(modem.getLocalIP());
    return true;
}

// 第 4 步：开 GPS（enableGPS(4,1) 给有源天线供电——漏掉永远搜不到星）
//         等首次定位，GPS_TIMEOUT_S 超时也不阻塞，进主循环继续试
void startGps()
{
    Serial.print("Enabling GPS (with antenna power)");
    while (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) {
        Serial.print(".");
        delay(500);
    }
    Serial.println(" OK");

    modem.setGPSBaud(115200);

#if defined(TINY_GSM_MODEM_SIM7670G)
    // SIM7670G 支持 GPS/GLONASS/GALILEO/北斗四系统
    modem.setGPSMode(GNSS_MODE_GPS_GLONASS_GALILEO_BDS);
#endif

    // 等首次定位：室外开阔处约 30 秒到几分钟
    Serial.print("Waiting for first fix");
    unsigned long t0 = millis();
    GPSInfo info;
    while (millis() - t0 < GPS_TIMEOUT_S * 1000UL) {
        if (modem.getGPS_Ex(info)) {
            Serial.printf(" OK (lat=%.6f lon=%.6f)\n", info.latitude, info.longitude);
            return;
        }
        Serial.print(".");
        delay(1000);
    }
    Serial.println(" timeout, keep going (retry every cycle)");
}


// ---------- 主循环 ----------

// 读 GPS 定位：成功返回 true；搜不到星返回 false
bool readGps(GPSInfo &info)
{
    return modem.getGPS_Ex(info);
}

// 真正发 HTTP POST（失败原因：超时/断网/服务器错误，返回值是 HTTP 状态码）
int httpPost(const std::string &body)
{
    HttpClient http(gsmClient, cfg.server.c_str(), cfg.port);
    http.setHttpResponseTimeout(15000); // 15 秒没响应就放弃，别卡住主循环
    int code = http.post("/", "application/x-www-form-urlencoded", String(body.c_str()));
    http.stop();
    return code;
}

// 上传定位到 Traccar：组装 OsmAnd 上报串（7 字段）→ HTTP POST，失败重试 1 次
bool uploadLocation(const GPSInfo &info)
{
    // 模块的 GPSInfo → 报文模块的 Location
    payload::Location loc;
    loc.hasFix  = (info.isFix == 2 || info.isFix == 3);
    loc.lat     = info.latitude;
    loc.lon     = info.longitude;
    loc.speed   = info.speed;     // 节（1 节 ≈ 1.85 km/h）
    loc.altitude = info.altitude; // 米
    loc.year   = info.year;   // GPS 时间，UTC
    loc.month  = info.month;
    loc.day    = info.day;
    loc.hour   = info.hour;
    loc.minute = info.minute;
    loc.second = info.second;

    std::string body = payload::buildBody(cfg, loc);
    if (body.empty()) {
        return false; // 无有效定位：跳过本次上报
    }

    Serial.printf("[%lu] Upload: %s\n", millis() / 1000, body.c_str());

    // 上传，失败重试 1 次（共 2 次尝试）
    for (int attempt = 1; attempt <= 2; ++attempt) {
        int code = httpPost(body);
        if (code == 200) {
            Serial.printf("[%lu] Upload OK (attempt %d)\n", millis() / 1000, attempt);
            return true;
        }
        Serial.printf("[%lu] Upload failed (attempt %d, HTTP %d)\n", millis() / 1000, attempt, code);
    }
    return false;
}

// 检查网络：断了就重连 GPRS
void checkAndReconnect()
{
    RegStatus status = modem.getRegistrationStatus();
    if (status != REG_OK_HOME && status != REG_OK_ROAMING) {
        Serial.printf("[%lu] Network lost (status %d), reconnecting...\n", millis() / 1000, status);
        if (modem.setNetworkActive(APN)) {
            Serial.printf("[%lu] Reconnected, IP: %s\n", millis() / 1000, modem.getLocalIP().c_str());
        } else {
            Serial.printf("[%lu] Reconnect failed, retry next cycle\n", millis() / 1000);
        }
    }
}


void setup()
{
    // 启动流程四步，按顺序执行
    initSerial();
    Serial.println();
    Serial.println("=== GPS collar firmware (phase-1 demo) ===");
    Serial.println("Log lines show [seconds since boot]; upload lines include UTC time");

    // 从 config.h 填入上报配置
    cfg.server   = TRACCAR_SERVER;
    cfg.port     = TRACCAR_PORT;
    cfg.deviceId = DEVICE_ID;
    cfg.batt     = BATTERY_PERCENT;

    modemPowerOn();     // 2. 模块开机

    if (!connectNetwork()) {   // 3. 联网
        // 联网失败不卡死：进入主循环，每周期 checkAndReconnect 会自动重试
        Serial.println("WARNING: network not ready, will retry every cycle");
    }

    startGps();         // 4. 开 GPS 并等首次定位

    Serial.println("Startup done, entering main loop");
}

void loop()
{
    // 模块无响应（掉电/异常）→ 重启整个设备，最简单可靠
    if (!modem.testAT(3000)) {
        Serial.println("Modem no response, restarting ESP32");
        Serial.flush();
        delay(100);
        esp_restart();
    }

    // 1) 读定位
    GPSInfo info;
    if (readGps(info)) {
        Serial.printf("[%lu] GPS fix: lat=%.6f lon=%.6f speed=%.2fkn alt=%.1fm sats=%d\n",
                      millis() / 1000,
                      info.latitude, info.longitude, info.speed, info.altitude,
                      info.gps_satellite_num);

        // 2) 上传（失败内部已重试 1 次）
        if (!uploadLocation(info)) {
            logStep("Skip this cycle (upload failed), will retry next cycle");
        }
    } else {
        // 搜不到星：不阻塞不崩溃，跳过本次上报
        logStep("Searching satellites, skip this report");
    }

    // 3) 检查网络，断了自动重连
    checkAndReconnect();

    // 常开模式：简单 delay 等下一个周期（4G 不断网、GPS 不断电、不进入休眠）
    delay(REPORT_INTERVAL_S * 1000UL);
}

// 必须使用 LilyGo 分支版 TinyGSM，否则编译时报错提示
#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/main/lib) to the arduino libraries directory, See README"
#endif
