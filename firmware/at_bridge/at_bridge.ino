/**
 * AT 直通调试桥 —— USB 串口直接与 SIM7670G 模块交互 AT 命令
 *
 * 用途：烧录本草图后，在电脑终端直接敲 AT 命令与模组对话
 * （arduino-cli monitor -p COM7 -c baudrate=115200 或 PuTTY），
 * 手工探查模组行为（PSM / AGPS / 网络状态等），不用改主固件。
 * 烧回主固件：bash scripts/flash.sh firmware/gps_collar
 *
 * 特殊命令：
 *   ~help    帮助
 *   ~reset   给模组发 PWRKEY 脉冲（模组开关机切换）
 *
 * 板子：LilyGO T-SIM7670G-S3-Standard（引脚定义见 ../gps_collar/utilities.h）
 */

// 接收缓冲区要够大（默认 256 太小，官方示例用 1024）
#define TINY_GSM_RX_BUFFER 1024

// Arduino 编译不支持跨草图目录的相对 include，故在本目录放一份 utilities.h 副本，
// 与 firmware/gps_collar/utilities.h 保持同步。
#include "utilities.h"
#include <TinyGsmClient.h>

TinyGsm modem(SerialAT);

String line;  // USB 输入行缓冲

// 转发模组主动上报（URC）到 USB
void drainModem()
{
    while (SerialAT.available()) {
        Serial.write(SerialAT.read());
    }
}

// 处理一行 USB 输入
void handleLine(String &cmd)
{
    cmd.trim();
    if (cmd.length() == 0) {
        return;
    }

    if (cmd == "~help") {
        Serial.println("AT 直通桥：输入的 AT 命令原样转发给模组，回应实时打印");
        Serial.println("  ~reset   给模组发 PWRKEY 脉冲（开关机切换；关机后再发一次即开机）");
        Serial.println("  ~help    显示本帮助");
        return;
    }
    if (cmd == "~reset") {
        Serial.println(">> PWRKEY pulse (module power toggle)");
        pinMode(BOARD_PWRKEY_PIN, OUTPUT);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);
        delay(100);
        digitalWrite(BOARD_PWRKEY_PIN, HIGH);
        delay(MODEM_POWERON_PULSE_WIDTH_MS);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);
        Serial.println(">> done (若模组原本开机，现在已关机；再发一次 ~reset 即重新开机)");
        return;
    }

    // 普通命令：回显 + 原样转发
    Serial.println("> " + cmd);
    SerialAT.print(cmd);
    SerialAT.print("\r\n");

    // 读取回应：自最后一次收到数据起静默 300ms 视为回应结束（总时长上限 5 秒）
    unsigned long lastData = millis();
    unsigned long t0 = millis();
    while (millis() - t0 < 5000UL) {
        if (SerialAT.available()) {
            while (SerialAT.available()) {
                Serial.write(SerialAT.read());
            }
            lastData = millis();
        }
        if (millis() - lastData > 300) {
            break;
        }
    }
    Serial.println();
}


void setup()
{
    Serial.begin(115200);
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(100);

    Serial.println();
    Serial.println("=== SIM7670G AT bridge ===");
    Serial.println("Type AT commands below; ~help for special commands");

    // DTR 拉低 = 模块保持常开（本桥不省电）
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);

    // 等模块就绪；没反应就发 PWRKEY 脉冲
    Serial.print("Waiting for modem");
    bool ready = false;
    for (int i = 0; i < 5 && !ready; ++i) {
        ready = modem.testAT(1000);
        if (!ready) {
            Serial.print(".");
        }
    }
    if (!ready) {
        Serial.print(" (PWRKEY)");
        pinMode(BOARD_PWRKEY_PIN, OUTPUT);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);
        delay(100);
        digitalWrite(BOARD_PWRKEY_PIN, HIGH);
        delay(MODEM_POWERON_PULSE_WIDTH_MS);
        digitalWrite(BOARD_PWRKEY_PIN, LOW);
        for (int i = 0; i < 30 && !ready; ++i) {
            ready = modem.testAT(1000);
        }
    }
    Serial.println(ready ? " OK" : " FAILED (bridge active anyway)");
    Serial.println();
}

void loop()
{
    // USB → 模组（按行转发）
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            handleLine(line);
            line = "";
        } else if (line.length() < 200) {
            line += c;
        }
    }

    // 模组 → USB（URC 实时转发）
    drainModem();
}

// 必须使用 LilyGo 分支版 TinyGSM，否则编译时报错提示
#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/main/lib) to the arduino libraries directory, See README"
#endif
