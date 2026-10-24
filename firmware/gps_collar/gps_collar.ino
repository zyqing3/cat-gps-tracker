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

// ---------- 实时时钟（毫秒级日志时间戳） ----------
// 权威时钟源：GPS 卫星 UTC 时间（定位成功后校准）；定位前用模组 AT+CCLK? 过渡。
// 毫秒部分用 ESP32 micros() 细分；未同步成功前退回 [开机秒数] 格式。
uint64_t wallEpochMs = 0;   // 同步时刻的墙上时间（显示时区，毫秒）
uint32_t wallMicros  = 0;   // 同步时刻的 micros() 读数
bool wallTimeValid   = false;

#define LOG_TZ_HOURS 8  // 日志显示时区（北京时间）

// 年/月/日 → 从 1970-01-01 起的天数（Howard Hinnant 算法）
long daysFromCivil(int y, int mo, int d)
{
    y -= mo <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// 从 1970-01-01 起的天数 → 年/月/日（civil_from_days 反算）
void civilFromDays(long z, int &y, int &m, int &d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = z - era * 146097;
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp + (mp < 10 ? 3 : -9));
    y += (m <= 2);
}

// 用一组 UTC 日期时间校准墙钟（显示时区 = UTC + LOG_TZ_HOURS）
void setWallClockFromUtc(int year, int month, int day, int hour, int minute, int second)
{
    long utcSec = daysFromCivil(year, month, day) * 86400L
                + hour * 3600L + minute * 60L + second;
    wallEpochMs = (uint64_t)(utcSec + LOG_TZ_HOURS * 3600L) * 1000ULL;
    wallMicros  = micros();
    wallTimeValid = true;
}

// 从运营商网络同步时间（AT+CCLK? 格式 "yy/mm/dd,hh:mm:ss+zz"，zz=四分之一小时）
// 只在还没同步过时问一次（GPS 定位后墙钟更准，不许它再覆盖）。
void syncWallClock()
{
    if (wallTimeValid) {
        return;  // 已同步过（尤其被 GPS 校准过），不再覆盖
    }
    modem.sendAT("+CCLK?");
    if (modem.waitResponse(3000L, "+CCLK:") != 1) {
        return;  // 问不到就保持旧值（或 [秒数] 格式）
    }
    String s = modem.stream.readStringUntil('\n');
    modem.waitResponse();
    s.trim();
    Serial.printf("[raw CCLK] %s\n", s.c_str());  // 调试用：看模组到底报什么格式

    int yy, mo, dd, hh, mi, ss, zz;
    if (sscanf(s.c_str(), "\"%d/%d/%d,%d:%d:%d%d\"",
               &yy, &mo, &dd, &hh, &mi, &ss, &zz) != 7) {
        return;  // 格式不对，放弃这次同步
    }
    // 实测本板模组（SIM7670G-MNGV）CCLK 的正文就是 UTC：
    // 时区标签 zz 报 +32（+8 小时）但时钟本身没加偏移。
    // 所以直接把读数当 UTC 用（换算北京时间在 setWallClockFromUtc 里 +8）。
    // 换卡/换运营商后若行为变了，看 [raw CCLK] 调试打印再调整。
    setWallClockFromUtc(2000 + yy, mo, dd, hh, mi, ss);
}

// 打印当前时间戳（HH:MM:SS.mmm）；没同步到时间时退回 [开机秒数]
void logTime()
{
    if (wallTimeValid) {
        uint64_t ms = wallEpochMs + (uint64_t)((micros() - wallMicros) / 1000U);
        long secs = (long)(ms / 1000ULL);
        int msec = (int)(ms % 1000ULL);
        int h = (secs / 3600) % 24;
        int m = (secs / 60) % 60;
        int s = secs % 60;
        Serial.printf("%02d:%02d:%02d.%03d ", h, m, s, msec);
    } else {
        Serial.printf("[%lu] ", millis() / 1000);
    }
}

// 打印带实时时间戳的日志行
void logStep(const char *msg)
{
    logTime();
    Serial.println(msg);
}

// 查询模组睡眠/唤醒状态（AT+LPSTATUS 手册 8.2.8：DTR 电平/USB/CSCLK 模式/GPS 状态）
// 用来证明模组确实睡了/醒了（#26），配合电流表交叉验证
void printLpStatus(const char *label)
{
    modem.sendAT("+LPSTATUS");
    if (modem.waitResponse(3000L, "+LPSTATUS:") != 1) {
        return;
    }
    // 回应是多行的：SLEEP: / DTR_LEVEL=x, / USB_PHY=x, / CSCLK=x, / GPS_STATUS=x.
    String lines[6];
    for (int i = 0; i < 6; ++i) {
        lines[i] = modem.stream.readStringUntil('\n');
        lines[i].trim();
    }
    modem.waitResponse();
    Serial.printf("%s: %s %s %s\n", label, lines[2].c_str(), lines[4].c_str(), lines[5].c_str());
}

// ---------- 调试期 AT 控制台（仅 DEBUG_ESP_NO_SLEEP 模式） ----------
// 在假休眠的延时期间开放：USB 输入按行转发给模组、回应/URC 实时打印。
// 只要持续有输入就延长会话（交互式体验）；没输入则按正常睡眠时长走。
// 注意：CSCLK=1 的 DTR 睡眠只能靠"拉低 DTR"唤醒（UART 数据唤不醒），
// 所以第一条命令前先释放 DTR 锁存并拉低唤醒模组，会话结束再让它睡回去。
void serviceAtConsole(unsigned long minMs)
{
    unsigned long t0 = millis();
    unsigned long lastInput = millis();
    String line;
    bool modemAwake = false;
    while (true) {
        // USB → 模组（按行转发）
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                line.trim();
                if (line.length()) {
                    if (!modemAwake) {
                        // 唤醒模组：释放锁存 + 拉低 DTR
                        gpio_hold_dis((gpio_num_t)MODEM_DTR_PIN);
                        pinMode(MODEM_DTR_PIN, OUTPUT);
                        digitalWrite(MODEM_DTR_PIN, LOW);
                        delay(300);
                        modemAwake = true;
                        Serial.println("(modem woken for console)");
                    }
                    Serial.println("> " + line);
                    SerialAT.print(line);
                    SerialAT.print("\r\n");
                    lastInput = millis();
                }
                line = "";
            } else if (line.length() < 200) {
                line += c;
            }
        }
        // 模组回应 / URC → USB
        while (SerialAT.available()) {
            Serial.write(SerialAT.read());
        }
        // 过了最短时间且 5 秒无输入 → 结束会话，继续周期
        if (millis() - t0 >= minMs && millis() - lastInput > 5000) {
            break;
        }
        delay(10);
    }

    // 会话结束：让模组重新入睡（恢复与 goToSleep 一致的 DTR 高 + 锁存）
    if (modemAwake) {
        modem.sleepEnable(true);
        digitalWrite(MODEM_DTR_PIN, HIGH);
        gpio_hold_en((gpio_num_t)MODEM_DTR_PIN);
        gpio_deep_sleep_hold_en();
    }
}

// 打印 GPS 原始诊断串（AT+CGNSSINFO 的原始回应）：
//   mode(0/1=未定位 2=2D 3=3D) + 四大星座卫星数(GPS/北斗/GLONASS/Galileo)
//   + 经纬度 + 时间 + PDOP/HDOP/VDOP
// 没定位成功时也能看到"天线到底收没收到卫星信号"，用于排查天线/信号问题。
void printGpsRaw(const char *label)
{
    String raw = modem.getGPSraw();
    logTime();
    Serial.printf("%s: %s\n", label,
                  raw.length() ? raw.c_str() : "(modem no response)");
}

// ---------- AGPS 辅助注入 ----------
// 上一轮定位（备用：将来若要给 CAGPSTOGPS 注入辅助位置用，5 公里内有效即可）
double lastFixLat = 0;
double lastFixLon = 0;
bool lastFixValid = false;

// 查 GPS 芯片里 AGPS 数据有效性（AT+CAGPSCHK：四星座的 接收/可用 状态位；
// 手册 21.2.26，GNSS 上电后有效）。返回：是否有任一星座星历可用（US 位=1）。
bool checkAgps()
{
    modem.sendAT("+CAGPSCHK");
    if (modem.waitResponse(3000L, "+CAGPSCHK:") != 1) {
        return false;
    }
    String s = modem.stream.readStringUntil('\n');
    modem.waitResponse();
    s.trim();
    Serial.print("AGPS chip data: ");
    Serial.println(s);
    // 8 个字段：GPS(接收,可用) BDS(接收,可用) GAL(接收,可用) GLO(接收,可用)
    int v[8] = {0};
    if (sscanf(s.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) {
        return false;
    }
    return v[1] == 1 || v[3] == 1 || v[5] == 1 || v[7] == 1;
}

// 轮询等芯片数据就绪：注入完成回应后，模组内部对星历的校验是异步的
// （实测有时要几秒），CHK 查早了是 0、晚一点变 1。
bool waitChipDataReady(int maxTries)
{
    for (int i = 0; i < maxTries; ++i) {
        if (checkAgps()) {
            return true;
        }
        delay(1000);
    }
    return false;
}

// AGPS（QEPO 星历）流程（SIM76XX 官方手册 21.2.23~24）——最终方案：
//   模式 0 直注（CAGPSTOLOCAL=0 预拉 + CAGPS 下载，两步都等完成）。
//   这是 8 种对照实验中唯一稳定加速定位的配方（定位 2~11 秒）。
//   实验结论：手动注入（CAGPSTOGPS 的 RAM/flash 路径、上电前/后、带不带辅助
//   时间位置、热启动与否）全部无效——引擎只吃模组内部直注管线的数据
//   （内部管线用模块自己的网络时钟做 ms 级时间辅助，手动注入提供不了）。
void runAgps()
{
    // 第一步：模式 0 配置 + 预拉（实测必须等它完成，否则后面 CAGPS 白下载）
    modem.sendAT("+CAGPSTOLOCAL=0");
    if (modem.waitResponse(30000L, "+AGPS:") == 1) {
        String r = modem.stream.readStringUntil('\n');
        modem.waitResponse();
        r.trim();
        Serial.printf("AGPS pull(mode0) reply: %s\n", r.c_str());
    }
    checkAgps();

    // 第二步：CAGPS 下载（配合模式 0，数据真正注入芯片）
    if (modem.enableAGPS()) {
        logStep("AGPS data ready");
        checkAgps();
    } else {
        logStep("AGPS unavailable, fall back to autonomous cold start");
    }
}


// ---------- 状态机各步骤（把硬件结果翻译成事件喂给状态机） ----------

// 唤醒 4G 模块：DTR 拉低 → 等 AT 就绪 → 必要时 PWRKEY 脉冲重开 → 确保网络在线
power::Event stepWakeModem()
{
    // 先释放深睡期间锁存的 DTR（gpio_hold 在 S3 上会跨深睡保持，
    // 不解锁的话下面拉低 DTR 不生效，模块永远叫不醒 → 每次都走 PWRKEY 硬重启）
    gpio_hold_dis((gpio_num_t)MODEM_DTR_PIN);

    // DTR 拉低唤醒模块（官方示例同款，醒来后网络附着还在）
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
    delay(500);

    // 唤醒延迟探测（PSM 测试 #27）：记录第一条 AT 得到回应前的耗时。
    // 基线（DTR 睡眠）≈ 1 秒内；若模组进入 PSM 深睡，唤醒延迟会明显变长。
    unsigned long wakeT0 = millis();

    // 等 AT 就绪（最多 10 秒）
    bool ready = false;
    for (int i = 0; i < 10 && !ready; ++i) {
        ready = modem.testAT(1000);
    }
    if (ready) {
        Serial.printf("wake latency: %lu ms\n", millis() - wakeT0);
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
    } else if (!modem.isGprsConnected()) {
        // 长睡眠后 PDP 数据链路可能已掉（注册还在但上不了网），
        // AGPS 下载和 HTTP 上传都依赖它——每轮确保 GPRS 激活
        logStep("PDP lost after sleep, re-activating GPRS");
        modem.setNetworkActive(APN);
    }
    syncWallClock();  // 拿到网络时间，日志从此用实时时间戳

#if PSM_ENABLE
    // PSM：向网络申请"上传完就深睡、保留注册"（睡眠电流可降到微安级；
    // 是否批准由运营商网络决定）。活跃时间请求 4 秒（GPRS Timer2 编码
    // 00000010 = 2 秒 × 2），让模组在 10 秒调试睡眠期内有机会真正进入 PSM，
    // 便于唤醒延迟探测；正式用时要改回合适值。
    modem.sendAT("+CPSMS=1,,,\"01101111\",\"00000010\"");
    if (modem.waitResponse(3000L) == 1) {
        logStep("PSM requested (TAU 70h, active 4s)");
        // 读回请求值确认模组收下了
        modem.sendAT("+CPSMS?");
        if (modem.waitResponse(3000L, "+CPSMS:") == 1) {
            String s = modem.stream.readStringUntil('\n');
            modem.waitResponse();
            s.trim();
            Serial.printf("PSM readback: %s\n", s.c_str());
        }
    }
#if PSM_TEST_MODE
    // PSM 协商发生在入网（ATTACH/TAU）时——参数变更后必须重新入网，
    // 网络才会处理新的 PSM 请求。只做一次（调试模式 RAM 常驻）。
    {
        static bool psmReattachDone = false;
        if (!psmReattachDone) {
            psmReattachDone = true;
            logStep("(psm-test) CFUN re-attach to negotiate PSM");
            modem.sendAT("+CFUN=1,1");
            delay(200);
            for (int i = 0; i < 30; ++i) {
                if (modem.testAT(1000)) {
                    break;
                }
            }
            // 等重新注册
            unsigned long t0 = millis();
            while (millis() - t0 < 60000UL) {
                RegStatus st = modem.getRegistrationStatus();
                if (st == REG_OK_HOME || st == REG_OK_ROAMING) {
                    break;
                }
                delay(1000);
            }
            logStep("(psm-test) re-attach done");
        }
    }
#endif
#endif
#if EDRX_ENABLE
    // eDRX：延长寻呼周期（睡觉时少醒几次听基站喊话），E-UTRAN(4)，请求 2621.44s
    modem.sendAT("+CEDRXS=1,4,\"1101\"");
    if (modem.waitResponse(3000L) == 1) {
        // 读回网络实际批准值（AcT=0 表示当前小区不支持 eDRX）
        modem.sendAT("+CEDRXRDP");
        if (modem.waitResponse(3000L, "+CEDRXRDP:") == 1) {
            String s = modem.stream.readStringUntil('\n');
            modem.waitResponse();
            s.trim();
            Serial.printf("eDRX granted: %s\n", s.c_str());
        }
    }
#endif

    printLpStatus("modem awake status");
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

    // 注意：不要发 gpsHotStart()！实测 GNSS 断电重开后星历已作废，
    // 强制热启动会让引擎拿着坏星历乱搜（120 秒一颗星都没有）；
    // 不干预时引擎按数据有效性自动冷启动，几十秒即有星。
    // （常开版从不热启动，定位正常——此处留作坑位标记）

#if AGPS_ENABLE
    runAgps();
#endif

    // 诊断信息：GNSS 电源状态 + 一次原始状态采样（看天线是否收到卫星）
    Serial.printf("GNSS power: %s\n", modem.isEnableGPS() ? "ON" : "OFF?");
    printGpsRaw("GPS raw after power-on");

    logStep("GNSS powered on, waiting for fix");
    return power::Event::GpsReady;
}

// 等定位：轮询到成功或超时（GPS_TIMEOUT_S）
power::Event stepWaitFix(GPSInfo &info)
{
    unsigned long t0 = millis();
    bool printedBad = false;
    while (millis() - t0 < params.gpsFixTimeoutS * 1000UL) {
        if (modem.getGPS_Ex(info)) {
            // 数据体检：刚上电的 GNSS 偶尔报"假定位"（mode=2/3 但时间乱码、卫星数=0），
            // 这种数据不能上传（服务器会拒收），当作没定位继续等。
            bool dateOk = (info.year >= 2020 && info.year <= 2099 &&
                           info.month >= 1 && info.month <= 12 &&
                           info.day >= 1 && info.day <= 31 &&
                           info.hour >= 0 && info.hour <= 23 &&
                           info.minute >= 0 && info.minute <= 59);
            if (!dateOk) {
                if (!printedBad) {
                    printGpsRaw("GPS bad fix data (ignored, keep waiting)");
                    printedBad = true;
                }
                delay(1000);
                continue;
            }
            // 用 GPS 的 UTC 时间校准墙钟（权威时间源），此后日志为北京时间
            setWallClockFromUtc(info.year, info.month, info.day,
                                info.hour, info.minute, info.second);

            // 记下本轮定位，供下一轮 AGPS 辅助位置注入
            lastFixLat = info.latitude;
            lastFixLon = info.longitude;
            lastFixValid = true;

            // 同步模组时钟为准确的北京时间+32：XTRA 星历文件有有效期，
            // 校验用模组本地时间——网络同步来的时间正文是 UTC 却标 +32（错 8 小时），
            // 会导致刚下载的 XTRA 被判过期白下载。每轮定位后顺手纠正，
            // 下一轮的 XTRA 就能通过校验、真正加速定位。
            {
                char cclk[40];
                snprintf(cclk, sizeof(cclk), "+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d+32\"",
                         info.year % 100, info.month, info.day,
                         (info.hour + LOG_TZ_HOURS) % 24, info.minute, info.second);
                modem.sendAT(cclk);
                modem.waitResponse(1000L);
            }
            logTime();
            Serial.printf("GPS fix: lat=%.6f lon=%.6f sats GPS=%d BDS=%d GLO=%d GAL=%d pdop=%.1f hdop=%.1f\n",
                          info.latitude, info.longitude,
                          info.gps_satellite_num, info.beidou_satellite_num,
                          info.glonass_satellite_num, info.galileo_satellite_num,
                          info.PDOP, info.HDOP);
            return power::Event::GpsFix;
        }
        delay(1000);
    }
    // 超时：打印原始 GNSS 状态，排查天线/信号问题
    printGpsRaw("GPS raw (no fix)");
    logStep("GPS timeout, no fix this cycle");
    return power::Event::GpsTimeout;
}

// 真正发 HTTP POST：用模组内置 HTTP 客户端（官方 Traccar 示例同款）
// 为什么不用 ArduinoHttpClient：实测本板模组固件（SIM7670G-MNGV）下 TinyGSM
// 的 TCP 读通道收不到服务器回应（HTTP code 恒为 0）；模组内置 HTTP 栈
// 请求和回应都在模组内部处理，绕开该问题。服务器是明文 HTTP，URL 用 http://。
int httpPost(const std::string &body)
{
    if (!modem.https_begin()) {
        Serial.println("http_begin failed");
        return -1;
    }
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/", cfg.server.c_str(), cfg.port);
    if (!modem.https_set_url(String(url))) {
        Serial.println("https_set_url failed");
        modem.https_end();
        return -2;
    }
    modem.https_set_user_agent("TinyGSM/LilyGo");
    modem.https_set_content_type("application/x-www-form-urlencoded");
    int code = modem.https_post(String(body.c_str()));
    modem.https_end();
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
    logTime();
    Serial.printf("Upload: %s\n", body.c_str());

    for (int attempt = 1; ; ++attempt) {
        int code = httpPost(body);
        if (code == 200) {
            logTime();
            Serial.printf("Upload OK (attempt %d)\n", attempt);
            return power::Event::UploadOk;
        }
        logTime();
        Serial.printf("Upload failed (attempt %d, HTTP %d)\n", attempt, code);
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
    logTime();
    Serial.printf("Cycle end: %s, sleep %lu s\n",
                  d.reason, (unsigned long)d.sleepSeconds);

    // 关天线 + GNSS 总电源（库内 CGDRT=4,0 + CGSETV=4,0 + CGNSSPWR=0）
    modem.disableGPS(MODEM_GPS_ENABLE_GPIO, 0);

#if PSM_TEST_MODE
    // PSM 实验：不干预模组（不设 CSCLK 睡眠、DTR 保持低电平），
    // 让它按"活跃时间"（请求值 4 秒）结束后自己进入 PSM 深睡；
    // 下一轮醒来用 AT 戳醒并测唤醒延迟。
    logStep("(psm-test) leave modem alone, expect PSM entry");
    printLpStatus("modem idle status");
#else
    // 让模块睡觉：CSCLK=1，配合 DTR 拉高（官方示例同款）
    modem.sleepEnable(true);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, HIGH);
    // 锁住 DTR 高电平，让模块在深睡期间安心睡觉：
    // gpio_hold_en 锁引脚电平，S3 上还要 gpio_deep_sleep_hold_en 才跨深睡生效
    gpio_hold_en((gpio_num_t)MODEM_DTR_PIN);
    gpio_deep_sleep_hold_en();
    delay(100);

    printLpStatus("modem sleep status");  // 证明模组确实睡了（#26）
#endif

    Serial.flush();

#if DEBUG_ESP_NO_SLEEP
    // 调试模式：ESP32 保持清醒只延时（USB 串口不断线，方便盯模组状态）；
    // 模组此刻照常在睡觉（DTR 被 hold 锁在高电平 + CSCLK=1）。
    // 延时期间开放 AT 控制台：直接敲 AT 命令与模组对话（命令会临时唤醒它）。
    logTime();
    Serial.printf("(debug) delaying %lu s; AT console open — type AT commands to talk to the modem\n",
                  (unsigned long)d.sleepSeconds);
    serviceAtConsole(d.sleepSeconds * 1000UL);
#else
    // ESP32 深睡，定时唤醒（醒来 = 重启，重新走一个周期）
    esp_sleep_enable_timer_wakeup((uint64_t)d.sleepSeconds * 1000000ULL);
    esp_deep_sleep_start();
    // 深睡成功的话到不了这里
#endif
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
#if DEBUG_ESP_NO_SLEEP
    // 调试模式：一个周期接一个周期跑（正式模式在 setup 里就深睡了，到不了这里）
    runOneCycle();
#else
    // 深睡模式下每次唤醒都是重启，永远到不了这里；
    // 留着以防深睡被跳过时卡死——理论上不会执行。
    delay(1000);
#endif
}

// 必须使用 LilyGo 分支版 TinyGSM，否则编译时报错提示
#ifndef TINY_GSM_FORK_LIBRARY
#error "No correct definition detected, Please copy all the [lib directories](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/main/lib) to the arduino libraries directory, See README"
#endif
