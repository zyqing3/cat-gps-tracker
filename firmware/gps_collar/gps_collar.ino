/**
 * GPS 项圈固件 —— 第一阶段 demo（常开模式）
 *
 * 功能：每 60 秒把 GPS 定位通过 4G 上传到 Traccar 服务器，
 *       服务器网页地图上能看到猫的最新位置。
 * 板子：LilyGO T-SIM7670G-S3（ESP32-S3 + SIM7670G，内置 GPS）
 * 供电：USB/充电宝，常开模式（4G 不断网、GPS 不断电、不休眠）
 *
 * 这是【骨架版】：启动流程按顺序搭好，函数还是空的，由后续工单填充。
 * 所有参数在 config.h，改参数不用动本文件。
 *
 * 依据：官方 Traccar 示例（vendor/LilyGo-Modem-Series/examples/Traccar/）
 *       设计文档 docs/design/firmware-architecture.md
 */

#include "utilities.h"        // 板子引脚定义（RX=10/TX=11/PWRKEY=18，以这份为准，勿信 wiki）
#include <TinyGsmClient.h>    // LilyGo 分支版 TinyGSM（原版编译不过）
#include "config.h"

// 4G 模块对象：通过 Serial1 和 SIM7670G 通信
TinyGsm modem(SerialAT);


// ---------- 启动流程四步 ----------

// 第 1 步：串口初始化（USB 串口看日志 + Serial1 连 4G 模块）
void initSerial()
{
    // USB 串口：插电脑在串口监视器看日志
    Serial.begin(115200);
    // 模块串口：RX=10 / TX=11 / 115200
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
}

// 第 2 步：模块开机（PWRKEY=18 脉冲唤醒 SIM7670G，然后等 AT 就绪）
void modemPowerOn()
{
    // TODO（完整固件工单）：PWRKEY 脉冲 → testAT 循环等待就绪
    //                       超时没响应就重新发脉冲
}

// 第 3 步：联网（查 SIM 卡 → 等注册到运营商网络 → 按 config.h 的 APN 连 GPRS）
void connectNetwork()
{
    // TODO（完整固件工单）：SIM 卡检测 → 等网络注册 → setNetworkActive(APN)
}

// 第 4 步：开 GPS（enableGPS(4,1) 给有源天线供电——漏掉永远搜不到星）
void startGps()
{
    // TODO（完整固件工单）：enableGPS → setGPSBaud → setGPSMode
    //                       等首次定位，GPS_TIMEOUT_S 超时
}


// ---------- 主循环 ----------

// 读 GPS 定位：成功返回 true；搜不到星返回 false
bool readGps(GPSInfo &info)
{
    // TODO（完整固件工单）：modem.getGPS_Ex(info)，解析经纬度/时间/速度
    return false;
}

// 上传定位到 Traccar：组装 OsmAnd 上报串（7 字段）→ HTTP POST，失败重试 1 次
bool uploadLocation(const GPSInfo &info)
{
    // TODO（完整固件工单）：组装上报串 → POST → 检查返回码
    return false;
}

// 检查网络：断了就重连 GPRS
void checkAndReconnect()
{
    // TODO（完整固件工单）：查注册状态，丢失则重新联网
}


void setup()
{
    // 启动流程四步，按顺序执行
    initSerial();
    Serial.println("GPS collar firmware starting (skeleton)");

    modemPowerOn();     // 2. 模块开机
    connectNetwork();   // 3. 联网
    startGps();         // 4. 开 GPS 并等首次定位

    Serial.println("Startup done, entering main loop");
}

void loop()
{
    GPSInfo info;

    if (readGps(info)) {
        uploadLocation(info);
    } else {
        Serial.println("Searching satellites, skip this report");
    }

    checkAndReconnect();

    // 常开模式：简单 delay 等下一个周期（4G 不断网、GPS 不断电、不进入休眠）
    delay(REPORT_INTERVAL_S * 1000UL);
}

// 必须使用 LilyGo 分支版 TinyGSM，否则编译时报错提示
#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGO-Modem-Series/tree/main/lib) to the arduino libraries directory, See README"
#endif
