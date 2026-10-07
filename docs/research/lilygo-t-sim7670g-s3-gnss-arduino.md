# 在 Arduino 下用 LilyGO T-SIM7670G-S3 读 GPS（经纬度 + 时间）

> 调研日期：2026-10-05
> 调研范围：全部结论都尽量追到"官方源头"（LilyGO 官方仓库源码、LilyGO 官方文档、SIMCom 官方 AT 指令手册）。文末有完整来源清单。

---

## 0. 一句话结论

这块板子的 GPS **不是**一颗单独的 GPS 模块，而是**藏在 SIM7670G 这个 4G 模块内部的**。
所以 ESP32-S3 不能直接读 GPS，只能通过**串口**给 4G 模块发 **AT 指令**，让模块把定位数据吐出来。

官方推荐的最小路径是：

1. 用 LilyGO 官方仓库里的 **TinyGSM 分支版**库（`modem.enableGPS()` → `modem.getGPS()`）；
2. 想自己解析 NMEA 就用 **TinyGPSPlus**（`gps.encode()` 一个字符一个字符喂）；
3. **最容易被忽略、也最要命的一步**：开 GPS 之前必须先给 GPS 天线供电——
   发 `AT+CGDRT=4,1` 和 `AT+CGSETV=4,1`。不做这一步，`AT+CGNSSPWR=1` 会回 `OK`，但**永远一颗卫星都搜不到**。

---

## 1. 背景：这块板子的 GNSS 到底是怎么连的

### 1.1 硬件结构（谁跟谁说话）

```
   ┌─────────────────────┐        ┌──────────────────────────────┐
   │   ESP32-S3 主控      │        │   SIM7670G 4G 模块            │
   │                     │        │                              │
   │  GPIO10 (RX) ◄──────┼────────┼── 模块 TX                    │
   │  GPIO11 (TX) ──────►┼────────┼─► 模块 RX                    │
   │  GPIO18 (PWRKEY) ──►┼────────┼─► 开机按键                   │
   │  GPIO17 (RESET) ───►┼────────┼─► 复位                       │
   │  GPIO9  (DTR)   ───►┼────────┼─► 让模块别睡觉               │
   └─────────────────────┘        │                              │
                                  │   ┌──────────────────────┐   │
     （GPS 天线接口） ◄─────────────┼───┤ 内部的 GPS 芯片 AG3352│   │
                                  │   │ 供电开关 = 模块 GPIO4 │   │
                                  │   └──────────────────────┘   │
                                  └──────────────────────────────┘
```

三个关键点：

1. **GPS 芯片在 4G 模块内部**。模块型号 SIM7670G，内部的 GNSS（卫星定位）芯片是 **AG3352**——
   证据：LilyGO 官方 TinyGSM 分支源码里写着注释 `// AG3352 GPS MODEL`（见 `lib/TinyGSM/src/TinyGsmClientSIM7672.h` 第 54 行）。
2. **ESP32 只有一条串口通向模块**（ESP32 的 GPIO10 / GPIO11）。GPS 数据、4G 上网数据、AT 指令，**全部挤在这一条串口上**。
   所以这块板子**没有"第二个串口给 GPS 用"**，不需要 EspSoftwareSerial 之类的软串口。
3. **GPS 天线的供电开关在模块自己身上**（模块的 GPIO4），ESP32 碰不到它，只能用 AT 指令远程控制。

### 1.2 真实引脚号（来自官方 `utilities.h`）

LilyGO 所有示例都靠一个叫 `utilities.h` 的文件来区分板型。在 `LILYGO_T_SIM7670G_S3` 这一段里写着：

| 名字 | ESP32-S3 引脚号 | 代码里的宏 | 说明 |
|---|---|---|---|
| 模块串口 RX（ESP32 收） | **GPIO 10** | `MODEM_RX_PIN` | ESP32 从这里**收**模块发来的数据 |
| 模块串口 TX（ESP32 发） | **GPIO 11** | `MODEM_TX_PIN` | ESP32 从这里**发**数据给模块 |
| 模块开机键 | GPIO 18 | `BOARD_PWRKEY_PIN` | 要按一下（拉高 100 毫秒）模块才开机 |
| 模块复位 | GPIO 17 | `MODEM_RESET_PIN` | 复位电平是 LOW（`MODEM_RESET_LEVEL = LOW`） |
| 模块 DTR | GPIO 9 | `MODEM_DTR_PIN` | 拉低，防止模块睡过去 |
| 板载 LED | GPIO 12 | `BOARD_LED_PIN` | |
| 电池电压检测 | GPIO 4 | `BOARD_BAT_ADC_PIN` | **这是 ESP32 的 GPIO4，和下面的模块 GPIO4 不是一回事！** |
| 太阳能电压检测 | GPIO 5 | `BOARD_SOLAR_ADC_PIN` | |

串口波特率：**115200**（`MODEM_BAUDRATE = 115200`），串口对象是 `Serial1`（`#define SerialAT Serial1`）。

> **别被 "GPIO4" 绕晕**：这块板子上有两个 "GPIO4"。
> - **ESP32 的 GPIO4** = 电池电压检测（`BOARD_BAT_ADC_PIN`）
> - **4G 模块内部的 GPIO4** = GPS 天线供电开关（`MODEM_GPS_ENABLE_GPIO`）
>
> LilyGO 作者本人（lewisxhe）在 GitHub Issue #375 里明确回答过：
> *"MODEM_GPS_ENABLE_GPIO is for the modem, and GPIO4 is BOARD_BAT_ADC_PIN which is for ESP. They are not the same thing."*

### 1.3 最容易踩的坑：GPS 天线要单独供电 ⚠️

LilyGO 官方板子文档 `docs/en/esp32s3/sim7670g-s3/README.MD` 里有一张表：

| Name | GPIO NUM | Enable level | AT Command |
|---|---|---|---|
| GPS Ant Power Enable | 4 | High | `AT+CGDRT=4,1 ; AT+CGSETV=4,1` |

并且用了一个醒目的警告框写着：

> 🚨 **Before using GPS/GNSS, you must configure modem GPIO4 as an output and drive it HIGH by sending `AT+CGDRT=4,1; AT+CGSETV=4,1`; otherwise, the modem cannot obtain a position fix.**
> （用 GPS 之前，必须把模块的 GPIO4 设成输出并拉高；否则模块拿不到定位。）

这条警告在 GitHub 上被反复验证过：

- Issue **#394**「T-SIM7670G-S3 GNSS NOT WORKING」：用户说 GPS 怎么都不出数据。作者回复：
  *"You haven't enabled GPS power supply."*（你没开 GPS 供电。）并给了这两条 AT 指令。用户回 *"working! thanks."*
- Issue **#477**：用户最后的结论是 *"The `CGDRT=4,1` / `CGSETV=4,1` pair was the key one missing... without it the GNSS engine starts and the AT commands respond OK, but the antenna LNA has no power, so zero satellites are ever seen."*

**道理很简单**：板子上的 GPS 天线是有源天线（里面有个小放大器 LNA，需要供电）。不给它供电，天线就等于没插。

### 1.4 术语小抄

| 词 | 通俗解释 |
|---|---|
| GNSS | 所有卫星定位系统的总称（GPS 是美国的，北斗是中国的，GLONASS 是俄罗斯的，Galileo 是欧洲的） |
| AT 指令 | 用文字命令跟手机模块说话的方式，比如发 `AT+CGNSSPWR=1` 回车，模块回 `OK` |
| NMEA | GPS 数据的国际标准文本格式，长得像 `$GNGGA,023900.00,3113.33,N,...` |
| 冷启动 / TTFF | 冷启动 = 卫星数据全部忘光重新找；TTFF = 从开机到能定位要花的时间 |
| 有源天线 | 自带放大器的天线，需要供电 |
| UART / 串口 | 两根线（TX 发、RX 收）的通信方式 |

---

## 2. 三条路线对比

| | ① AT 指令直连 | ② TinyGPSPlus 解析 NMEA | ③ LilyGO 官方示例（TinyGSM 封装） |
|---|---|---|---|
| **你要写什么** | 自己发 `AT+CGNSSINFO`，自己用逗号切字符串 | 让模块持续吐 NMEA，用 `gps.encode()` 喂给 TinyGPSPlus | 直接调 `modem.getGPS()` |
| **用哪个库** | 不用库，直接用 `Serial1.print()` / `Serial1.readString()` | TinyGPSPlus（官方仓库自带 v1.0.3） | TinyGSM（**必须用 LilyGO 的分支版**） |
| **代码量** | 少（发一条、读一条、切分） | 中 | 最少 |
| **拿到的数据** | 经纬度、时间、日期、高度、速度、卫星数 | 经纬度、时间、日期、高度、速度、HDOP、卫星数 | 经纬度、时间、日期、高度、速度、可见卫星数 |
| **好处** | 最透明，出问题一眼看出卡在哪一步 | 标准做法；解析健壮（有校验和检查）；换任何 GPS 都能用 | 一条指令拿到结构化的 float / int，不用自己解析 |
| **坏处** | 得自己处理"数据还没准备好"（字段全是空）的情况；自己切字符串容易写错 | NMEA 会**一直**往串口灌，会和你发的 AT 指令回复**混在一起** | 依赖 LilyGO 的修改版库；有个已知的小 bug（见下） |
| **官方支不支持这块板** | 只能算"能跑"，没有专门示例 | ✅ 支持。`examples/GPS_NMEA_Parse` 和 `GPS_NMEA_Output` 都没有 `.skip.T-SIM7670G` 标记 | ✅ 支持。`examples/GPS_BuiltIn` 和 `GPS_BuiltInEx` 都没有 `.skip.T-SIM7670G` 标记 |
| **推荐度** | 调试 / 学习用 | 想要标准 NMEA 数据时 | ⭐ **首选**，最省事 |

### 关于 `.skip` 标记（怎么知道官方支不支持）

LilyGO 用 GitHub Actions 自动编译测试所有示例。`.github/workflows/platformio.yml` 里的逻辑是：

```bash
if [ -f "${{ matrix.examples }}/.skip."${{ matrix.envs }} ];then
  echo "Skip" ${{ matrix.examples }}
else
  pio run -e ${{ matrix.envs }}
fi
```

翻成人话：**如果 `examples/某个例子/` 目录下存在 `.skip.T-SIM7670G` 这个文件，就说明这个例子**不**支持 T-SIM7670G。**

对这块板子，**被官方标记为"不支持/不测试"**的 GPS 相关例子有：

| 例子 | 是否支持 T-SIM7670G | 原因 |
|---|---|---|
| `GPS_BuiltIn` | ✅ 支持 | 用内置 GNSS |
| `GPS_BuiltInEx` | ✅ 支持 | 用内置 GNSS，信息更全 |
| `GPS_NMEA_Parse` | ✅ 支持 | NMEA + TinyGPSPlus |
| `GPS_NMEA_Output` | ✅ 支持 | NMEA 原样转发到电脑串口 |
| `ModemGpsStream` | ❌ 跳过 | 这是给"外接 GPS 模块"的板子用的 |
| `ExternalGPS_A7670G_Only` | ❌ 跳过 | 同上，给没有内置 GPS 的 A7670G 版 |
| `GPS_Acceleration` | ❌ 跳过 | |
| `SendLocationFromSMS_Use_TinyGPS` | ❌ 跳过 | 发短信的例子，本板不支持（SIM7670G 不支持打电话，SMS 也受限） |

> 提醒：`SendLocationFromSMS_Use_TinyGPS` 虽然名字里有 TinyGPS，但它被跳过了。**不代表** TinyGPSPlus 路线不行——`GPS_NMEA_Parse` 就是官方在测的 TinyGPSPlus 例子。

---

## 3. 推荐的最小代码路径

### 3.1 第一步：装库

LilyGO 官方文档 `docs/en/esp32s3/sim7670g-s3/README.MD` 的 "Arduino IDE quick start" 明确写了：

1. 下载 `https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series`
2. **把仓库 `lib` 目录下的所有文件夹，整个复制到 Arduino 的 libraries 目录**
   - Windows: `C:\Users\%USERNAME%\Documents\Arduino\libraries`
3. 打开 `examples` 里的例子（`.ino` 文件）

`lib` 目录里有：

- `TinyGSM` —— **LilyGO 修改过的分支版**（不是 Arduino 库管理器里那个原版！）
- `TinyGPSPlus` —— 就是 mikalhart 的官方 TinyGPSPlus v1.0.3（`library.properties` 里写的 `version=1.0.3`）
- `RadioLib` —— 这次用不到

> ⚠️ **为什么必须用 LilyGO 的分支版 TinyGSM**：
> 每个例子的结尾都有这段检查：
> ```cpp
> #ifndef TINY_GSM_FORK_LIBRARY
> #error "No correct definition detected, Please copy all the [lib directories]..."
> #endif
> ```
> 而 `TINY_GSM_FORK_LIBRARY` 这个宏**只**定义在 LilyGO 分支版的 `TinyGsmClient.h` 第 13 行。
> 也就是说：如果从 Arduino 库管理器装了原版 TinyGSM，编译会直接报错停下。
> 另外官方文档还提醒：**Arduino IDE 提示"有新库可更新"时，不要点更新**，否则可能把 TinyGSM 覆盖掉。

### 3.2 第二步：Arduino IDE 的板子设置

官方文档给的表格（照抄）：

| 项目 | 值 |
|---|---|
| Board | **ESP32S3 Dev Module** |
| Port | 你板子的串口号 |
| USB CDC On Boot | **Enable** |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80Mhz |
| Flash Size | **16MB (128Mb)** |
| USB Firmware MSC On Boot | Disable |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | **OPI PSRAM** |
| Upload Speed | 921600 |
| Programmer | **Esptool** |

然后：**打开 `utilities.h`，把 `// #define LILYGO_T_SIM7670G_S3` 这一行前面的 `//` 删掉，保存。**

（这一行在 `utilities.h` 第 33 行。不删的话，代码不知道你用的是哪块板，引脚号就全是错的。）

### 3.3 第三步：最小可编译示例

下面这份代码是把官方 `examples/GPS_BuiltIn/GPS_BuiltIn.ino` 精简到了"只要经纬度和时间"。
**放在 `GPS_BuiltIn/` 目录旁边编译**（因为它 `#include "utilities.h"`），或者把 `utilities.h` 复制到你自己的 sketch 文件夹里。

```cpp
/**
 * 最小 GPS 定位示例 —— LilyGO T-SIM7670G-S3
 * 需要：Arduino ESP32 core + LilyGO-Modem-Series 仓库 lib/ 下的 TinyGSM(分支版)
 * 使用前：在本目录的 utilities.h 里取消注释 #define LILYGO_T_SIM7670G_S3
 */
#include "utilities.h"          // 里面定义了引脚号，必须和 .ino 放同一个文件夹

#define TINY_GSM_RX_BUFFER 1024 // 串口接收缓存放到 1KB，必须写在 include <TinyGsmClient.h> 之前

#include <TinyGsmClient.h>

TinyGsm modem(SerialAT);        // SerialAT = Serial1（在 utilities.h 里定义）

void setup() {
    Serial.begin(115200);       // 电脑串口，用来看打印
    delay(3000);                // 等串口监视器连上

    // 1) DTR 拉低，让模块不要睡觉
#ifdef MODEM_DTR_PIN
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
#endif

    // 2) 复位一下模块（MODEM_RESET_LEVEL 是 LOW，所以下面这个顺序是：高100ms→低2600ms→高）
#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL); delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);  delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

    // 3) 按一下开机键（PWRKEY = GPIO18）：低100ms → 高100ms → 低
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);  delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH); delay(MODEM_POWERON_PULSE_WIDTH_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    // 4) 打开与模块通信的串口：115200，8N1，RX=GPIO10，TX=GPIO11
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    Serial.println("Start modem...");
    delay(3000);

    // 5) 等模块回应 AT
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (retry++ > 30) {     // 太久没反应，重新按一次开机键
            digitalWrite(BOARD_PWRKEY_PIN, LOW);  delay(100);
            digitalWrite(BOARD_PWRKEY_PIN, HIGH); delay(MODEM_POWERON_PULSE_WIDTH_MS);
            digitalWrite(BOARD_PWRKEY_PIN, LOW);
            retry = 0;
        }
    }
    Serial.println("\nModem OK");

    // 6) 打开 GPS —— 这一步会先给天线供电，再启动 GNSS
    //    MODEM_GPS_ENABLE_GPIO = 4，MODEM_GPS_ENABLE_LEVEL = 1（都是"模块的"GPIO4）
    Serial.println("Enabling GPS...");
    while (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) {
        Serial.print(".");
    }
    Serial.println("\nGPS Enabled");

    // 7) 把模块和 GPS 芯片之间的串口速率设成 115200
    modem.setGPSBaud(115200);
}

void loop() {
    uint8_t fixMode = 0;
    float lat = 0, lon = 0, speed = 0, alt = 0, accuracy = 0;
    int   vsat = 0, usat = 0;
    int   year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    Serial.println("Requesting GPS location...");
    if (modem.getGPS(&fixMode, &lat, &lon, &speed, &alt,
                     &vsat, &usat, &accuracy,
                     &year, &month, &day, &hour, &minute, &second)) {
        Serial.print("Fix mode (2=2D, 3=3D): "); Serial.println(fixMode);
        Serial.print("Latitude : "); Serial.println(lat, 6);
        Serial.print("Longitude: "); Serial.println(lon, 6);
        Serial.print("Altitude : "); Serial.println(alt);
        Serial.print("Visible satellites: "); Serial.println(vsat);
        // 时间是 UTC（世界时），北京时间要 +8 小时
        Serial.printf("UTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        delay(5000);
    } else {
        Serial.println("No fix yet, retry in 15s");
        delay(15000);           // 冷启动时这里会循环很多次，正常
    }
}
```

### 3.4 程序跑起来后，模块内部实际发生了什么

`modem.enableGPS(4, 1)` 在 LilyGO 的 TinyGSM 分支里展开成这几条 AT 指令
（源码：`lib/TinyGSM/src/TinyGsmClientSIM7672.h`，`enableGPSImpl()`）：

```
AT+CGDRT=4,1        ← 把"模块的"GPIO4 设成输出
AT+CGSETV=4,1       ← 把"模块的"GPIO4 拉高 = 给 GPS 天线供电
AT+CGNSSPWR?        ← 问一下 GPS 是不是已经开着
AT+CGNSSPWR=1       ← 打开 GNSS（等模块回应 OK，最多等 10 秒）
```

`modem.getGPS(...)` 展开成：

```
AT+CGNSSINFO        ← 要一次定位信息
```

SIMCom 官方手册《SIM767XX Series_AT Command Manual_V1.02》21.2.21 节给出的回复格式是：

```
+CGNSSINFO:[<mode>],[<GPS-SVs>],[<GLONASS-SVs>],[<GALILEO-SVs>],[<BEIDOU-SVs>],
           [<lat>],[<N/S>],[<log>],[<E/W>],[<date>],[<UTC-time>],
           [<alt>],[<speed>],[<course>],[<PDOP>],[HDOP],[VDOP],[<NoSV>]
```

字段含义（手册原文翻译）：

| 字段 | 含义 |
|---|---|
| `<mode>` | 定位模式，2 = 2D 定位，3 = 3D 定位（其他值 = 还没定上位） |
| `<GPS-SVs>` 等 | 各卫星系统"看得见"的卫星数 |
| `<lat>` | 纬度（十进制度，如 34.123456） |
| `<N/S>` | N = 北纬，S = 南纬 |
| `<log>` | 经度（十进制度，如 138.123456） |
| `<E/W>` | E = 东经，W = 西经 |
| `<date>` | 日期，格式 ddmmyy（日日月月年年） |
| `<UTC-time>` | UTC 时间，格式 hhmmss.ss |
| `<alt>` | 海拔（米） |
| `<speed>` | 地面速度（节，1 节 ≈ 1.852 公里/小时） |
| `<course>` | 航向（度） |
| `<PDOP>/<HDOP>/<VDOP>` | 精度因子，越小越准 |
| `<NoSV>` | 参与定位的卫星总数 |

真实设备（T-SIM7670G-S3）吐出来的一行长这样（来自 GitHub Issue #133 的日志，位置已打码）：

```
+CGNSSINFO: 3,10,00,,,34.xxxxxx,S,138.xxxxxx,E,020924,041428.000,25.6,0.08,249.97,2.32,2.16,0.85,5
```

**没定上位的时候，字段全是空的**：

```
+CGNSSINFO: ,,,,,,,,,,,,,,,,,
```

这就是"搜星中"的正常表现，不是坏了。

### 3.5 路线 ②：TinyGPSPlus 怎么用（要点）

如果你想要标准 NMEA（比如以后想换别的 GPS、或者想用 TinyGPSPlus 算两个点之间的距离），
官方 `examples/GPS_NMEA_Parse/GPS_NMEA_Parse.ino` 的流程是：

```cpp
// 1) 前面开机的步骤和 3.3 完全一样（DTR / 复位 / PWRKEY / SerialAT.begin）

// 2) 开 GPS
while (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) { Serial.print("."); }
modem.setGPSBaud(115200);

// 3) 选星座（SIM7670G 专用：15 = GPS+GLONASS+Galileo+北斗，最全）
modem.setGPSMode(GNSS_MODE_GPS_GLONASS_GALILEO_BDS);

// 4) 选要输出哪些 NMEA 句子
modem.configNMEASentence(NMEA_GPGGA | NMEA_GPGSA | NMEA_GPGSV | NMEA_GPRMC);

// 5) 每秒输出一次
modem.setGPSOutputRate(1);

// 6) 打开 NMEA 输出（内部发 AT+CGNSSTST=1 和 AT+CGNSSPORTSWITCH=0,1）
modem.enableNMEA();

// 7) loop 里把串口收到的每个字符喂给 TinyGPSPlus
TinyGPSPlus gps;                 // 放在文件开头

void loop() {
    while (SerialAT.available()) {
        gps.encode(SerialAT.read());   // ← 核心就这一行
    }
    if (gps.location.isValid()) {
        Serial.println(gps.location.lat(), 6);
        Serial.println(gps.location.lng(), 6);
    }
    if (gps.date.isValid() && gps.time.isValid()) {
        Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                      gps.date.year(), gps.date.month(), gps.date.day(),
                      gps.time.hour(), gps.time.minute(), gps.time.second());
    }
}
```

TinyGPSPlus 的官方 API（来自 `mikalhart/TinyGPSPlus` 的 `src/TinyGPS++.h`）：

- `bool encode(char c)` —— "process one character received from GPS"，喂一个字符
- `gps.location.lat()` / `.lng()` / `.isValid()`
- `gps.date.year()/month()/day()`，`gps.time.hour()/minute()/second()/centisecond()`
- `gps.satellites.value()`、`gps.hdop.hdop()`、`gps.altitude.meters()`、`gps.speed.kmph()`、`gps.course.deg()`
- `gps.charsProcessed()`、`gps.failedChecksum()` —— 用来判断"到底有没有数据进来"

**这条路线的两个注意点**：

1. NMEA 会**不停地**往串口灌。你之后发的每一条 AT 指令的回复，都可能和 NMEA 混在一起。
   Issue #477 里总结的应对办法：AT 指令的接收缓存要 ≥1024 字节（`#define TINY_GSM_RX_BUFFER 1024`，必须在 `#include <TinyGsmClient.h>` **之前**写）；不用 GPS 时先 `AT+CGNSSTST=0` 再 `AT+CGNSSPWR=0` 关掉，让 4G 上网的初始化干净一点。
2. 官方示例里的 `smartDelay()` 就是"在等待的同时不停喂字符给 gps 对象"——**不能直接 `delay()`**，否则串口缓存会溢出、丢数据。

### 3.6 路线 ①：AT 指令直连（不用库，纯调试用）

如果你想亲眼看懂每一步，可以只发 AT 指令。下面这段不依赖 TinyGSM：

```cpp
// 只演示 GPS 相关的 AT 指令，前面的开机流程（PWRKEY / 复位）和 3.3 一样
void gpsByAT() {
    // 1) 给 GPS 天线供电（模块的 GPIO4）
    SerialAT.println("AT+CGDRT=4,1");
    delay(200);
    SerialAT.println("AT+CGSETV=4,1");
    delay(200);

    // 2) 打开 GNSS
    SerialAT.println("AT+CGNSSPWR=1");
    delay(2000);

    // 3) 选星座：15 = GPS+GLONASS+Galileo+北斗
    SerialAT.println("AT+CGNSSMODE=15");
    delay(200);

    // 4) 每隔几秒问一次定位
    while (true) {
        SerialAT.println("AT+CGNSSINFO");
        delay(1000);
        while (SerialAT.available()) {
            Serial.write(SerialAT.read());   // 原样打印到电脑，自己用眼睛看
        }
        delay(2000);
    }
}
```

什么时候需要发哪条（全部来自 SIMCom 官方手册第 21.2 节）：

| AT 指令 | 干什么 | 什么时候发 |
|---|---|---|
| `AT+CGDRT=4,1` | 把模块 GPIO4 设成输出 | **开 GPS 之前，必须** |
| `AT+CGSETV=4,1` | 模块 GPIO4 拉高 = 天线供电 | **开 GPS 之前，必须** |
| `AT+CGNSSPWR=1` | 打开 GNSS | 开 GPS 时 |
| `AT+CGNSSPWR=0` | 关掉 GNSS（省电） | 不用时 |
| `AT+CGNSSPWR?` | 查 GNSS 开关状态（回 `+CGNSSPWR: 1` 就是开着） | 随时 |
| `AT+CGNSSINFO` | 要一次定位信息 | 轮询 |
| `AT+CGNSSMODE=<n>` | 选星座。1=GPS，3=+GLONASS，5=+Galileo，9=+北斗，13，15=全都开 | 开 GPS 后 |
| `AT+CGNSSIPR=<baud>` | 设模块和 GPS 芯片之间串口的波特率 | 开 GPS 后 |
| `AT+CGNSSNMEA=...` | 选输出哪些 NMEA 句子 | 开 NMEA 前 |
| `AT+CGNSSTST=1` | **开始**把 NMEA 往串口吐（想读 NMEA 必须先发这条） | 开 NMEA 前 |
| `AT+CGNSSTST=0` | 停止吐 NMEA | 不用 NMEA 时 |
| `AT+CGNSSPORTSWITCH=0,1` | 把 NMEA 转到 UART 口（而不是 USB 口） | 开 NMEA 前 |
| `AT+CGPSCOLD` | 冷启动（忘掉所有卫星数据重新找） | 见第 4 节 |
| `AT+CGPSWARM` | 温启动 | 见第 4 节 |
| `AT+CGPSHOT` | 热启动（有缓存时最快） | 见第 4 节 |
| `AT+CGNSSPROD` | 查 GPS 芯片的版本信息 | 调试 |

在 TinyGSM 封装里，它们对应的函数是：

```cpp
modem.gpsColdStart();   // → AT+CGPSCOLD
modem.gpsWarmStart();   // → AT+CGPSWARM
modem.gpsHotStart();    // → AT+CGPSHOT
modem.getGPSraw();      // → AT+CGNSSINFO，但返回原始字符串，不解析
modem.getGPSTime(&y,&mo,&d,&h,&mi,&s);  // 只取时间，不取经纬度
modem.getGPS_Ex(info);  // 信息更全（各星座卫星数、PDOP/HDOP/VDOP），需要 GPSInfo 结构体
modem.disableGPS(MODEM_GPS_ENABLE_GPIO, !MODEM_GPS_ENABLE_LEVEL);  // 关 GPS + 断天线电
```

---

## 4. 冷启动注意事项

### 4.1 硬件层面（不做就永远定不上位）

1. **必须接 GPS 天线**，而且是**有源天线**（3.3V 供电那种）。
   官方文档原话：*"The external GPS antenna must be an active GPS antenna with a 3.3V power supply."*
   板子上的 `GNSS` 接口就是给它的，`SIM` 接口是给 4G 天线的，两个都要接。
2. **必须发 `AT+CGDRT=4,1` + `AT+CGSETV=4,1` 给天线供电**。这是这块板子排名第一的"GPS 不工作"原因（Issue #394、#477）。
3. **室内基本收不到**。GPS 信号从 20000 公里外的卫星来，穿不过屋顶。要放到**室外、天空开阔**的地方。
   官方文档在 FAQ 里也强调测试要在室外。有用户报告（#477）：两个天线、窗外和室外空旷处都试过，最后还是靠"给天线供电"解决的。
4. **4G 和 GPS 同时用没问题**（和 SIM7080G 不一样：那个板子 GPS 和 4G 不能同时开，代码里专门有 `setNetworkDeactivate()`）。SIM7670G 不需要这步。
5. **供电要够**。官方文档警告：USB/VBUS 输入要能提供至少 2A 峰值电流，电压不能掉到 5V 以下，否则会触发低压自动关机。还有：**USB 和电池之间切换时板子会自动重启**，这是硬件设计如此，改不了（`The board may reset and restart the device when switching between USB and battery. This is normal`）。这对"要连续定位"的项圈来说是个重要提醒。

### 4.2 软件层面

6. **冷启动要等**。断电几天后再开，模块要把星历（卫星轨道数据）从零开始下载，通常要 30 秒到几分钟。`AT+CGNSSINFO` 返回 `+CGNSSINFO: ,,,,,,,,` 是正常的"还没好"，**不要以为坏了**。
   - `AT+CGPSCOLD`（冷启动）/ `AT+CGPSWARM`（温启动）/ `AT+CGPSHOT`（热启动）可以手动控制。手册注明它们**必须在 GNSS 已开机之后**发（"This command is valid after the GNSS power on!"）。
   - **别在循环里反复发 `AT+CGPSCOLD`**。每次冷启动都会把已经攒下来的卫星数据全丢掉，越弄越慢。
7. **要能分辨"没数据"和"没定上位"**。`getGPS()` 返回 `false` 就重试，官方示例是等 15 秒再试一次。
8. **`AT+CGNSSINFO` 在个别固件版本上可能一直是空的**。
   Issue #477 的用户（固件 `SIM767XM5_B04V01_241010`）报告：卫星其实已经搜到了，但 `AT+CGNSSINFO` 的字段始终是空的；换成 **NMEA 输出**（`AT+CGNSSTST=1`）后用标准 NMEA 解析就正常了。
   ⚠️ **注意**：这位用户的结论是"改用 NMEA 路线"；但 LilyGO 作者（lewisxhe）在同一 Issue 里指出他一开始**忘了给 GPS 天线供电**，并坚持"轮询和 NMEA 两种方式都可以，请先用我的例子测试"。
   **所以这一条算是"有用户报告、官方不认可"的情况，建议实测确认。**
9. **时区**：`AT+CGNSSINFO` 里的时间是 **UTC（世界时）**。北京时间 = UTC + 8 小时。跨日要小心。
10. **`getGPS()` 有个已知的解析小坑**：源码里读 N/S、E/W 方向字符用的是 `stream.read()`。有用户（Issue #133 后续评论）报告这个 `read()` 有时会返回 255（没读到），导致南北/东西被弄反。如果发现坐标方向不对，试试用 `readStringUntil(',')` 自己取。**这一条只在用户评论里出现，官方未确认。**
11. **经纬度格式不用自己换算**。LilyGO 的驱动里写着注释 `// Latitude in ddmm.mmmmmm`，但**实际设备输出的是十进制度**（Issue #133 的真实日志：`34.xxxxxx,S`）。所以 `modem.getGPS()` 拿到的 `lat` / `lon` 就是可以直接画地图的度数，**不需要再除以 100 之类**。
    （SIMCom 手册第 21.2.21 节的字段说明也写 `Output format is dd.ddddd`；手册里那条 `3113.330650` 的示例是旧格式，和它自己的字段说明互相矛盾，**以真实设备为准**。）

---

## 5. 哪些结论是"确认的"，哪些还需要上机验证

### ✅ 已确认（有一手来源）

| 结论 | 来源 |
|---|---|
| GPS 是 SIM7670G 模块内置的，芯片型号 AG3352 | `lib/TinyGSM/src/TinyGsmClientSIM7672.h` 第 54 行注释 `// AG3352 GPS MODEL` |
| ESP32-S3 ↔ 模块串口：RX = GPIO10，TX = GPIO11，波特率 115200，用 `Serial1` | `examples/*/utilities.h` 中 `LILYGO_T_SIM7670G_S3` 段（第 197-252 行） |
| PWRKEY = GPIO18，RESET = GPIO17（复位电平 LOW），DTR = GPIO9，LED = GPIO12，电池 ADC = GPIO4（ESP32 侧） | 同上 |
| 开 GPS 前必须发 `AT+CGDRT=4,1` + `AT+CGSETV=4,1` 给天线供电 | 官方文档 `docs/en/esp32s3/sim7670g-s3/README.MD` 的 Modem Pins 表 + 作者回复 Issue #394、#375 |
| 模块 GPIO4 = GPS 天线供电，和 ESP32 的 GPIO4（电池 ADC）不是一回事 | 作者回复 Issue #375 |
| 开 GNSS 的指令序列：`AT+CGDRT=4,1` → `AT+CGSETV=4,1` → `AT+CGNSSPWR?` → `AT+CGNSSPWR=1` | `TinyGsmClientSIM7672.h` `enableGPSImpl()` 第 666-679 行 |
| 读定位：`AT+CGNSSINFO`，返回字段顺序（mode, GPS-SVs, GLONASS-SVs, GALILEO-SVs, BEIDOU-SVs, lat, N/S, lon, E/W, date ddmmyy, UTC hhmmss.ss, alt, speed, course, PDOP, HDOP, VDOP, NoSV） | SIMCom《SIM767XX Series_AT Command Manual_V1.02》21.2.21 节 |
| 星座模式取值：1/3/5/9/13/15 | 同手册 21.2.7 节 + `TinyGsmClientSIM7672.h` `SIM7670G_GPSMode` 枚举 |
| 冷/温/热启动指令 `AT+CGPSCOLD` / `AT+CGPSWARM` / `AT+CGPSHOT`，且必须在 GNSS 开机后发 | 同手册 21.2.3 / 21.2.4 / 21.2.5 节 |
| NMEA 输出要 `AT+CGNSSNMEA=...` → `AT+CGNSSTST=1` → `AT+CGNSSPORTSWITCH=0,1` | 同手册 21.2.2 / 21.2.8 / 21.2.10 节 + `enableNMEAImpl()` |
| 官方支持这块板的 GPS 例子：`GPS_BuiltIn`、`GPS_BuiltInEx`、`GPS_NMEA_Parse`、`GPS_NMEA_Output` | `.github/workflows/platformio.yml` 的 `.skip.<env>` 判断逻辑 + 这四个目录下没有 `.skip.T-SIM7670G` |
| TinyGPSPlus 用法：`bool encode(char c)`，然后读 `gps.location` / `gps.date` / `gps.time` | `mikalhart/TinyGPSPlus` 的 `src/TinyGPS++.h` 第 228-254 行 |
| 必须用 LilyGO 分支版 TinyGSM（原版会编译报错） | 例子结尾的 `#ifndef TINY_GSM_FORK_LIBRARY #error`，宏定义在 `lib/TinyGSM/src/TinyGsmClient.h` 第 13 行 |
| Arduino IDE 板子设置（ESP32S3 Dev Module / 16MB Flash / OPI PSRAM / 等） | 官方文档 `docs/en/esp32s3/sim7670g-s3/README.MD` |
| 天线必须是有源天线、USB 供电要 ≥2A、USB/电池切换会重启 | 同上文档的 Antenna / Electrical / FAQ 段落 |

### ⚠️ 未确认 / 需要上机实测

| 事项 | 说明 |
|---|---|
| LilyGO 官方 wiki 的引脚号是**错的** | `wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html` 的代码里写 `// SIM7670G UART: RX=4, TX=5, PWR_KEY=12`，并且真的用 `modemSerial.begin(115200, SERIAL_8N1, 4, 5)` / `pinMode(12, OUTPUT)`。**2026-10 板子到货后实测澄清**：RX=4 / TX=5 正是 **Standard 版（`LILYGO_SIM7670G_S3_STAN`）的串口引脚**（`PWR_KEY=12` 对不上任何板型，wiki 自身有误）。**本板实际为 T-SIM7670G-S3-Standard，引脚以 `utilities.h` 的 STAN 段为准：RX=5 / TX=4 / PWRKEY=46 / DTR=7；Pro 版才是 RX=10 / TX=11 / PWRKEY=18。** |
| `modem.getGPS()` 返回的经纬度到底是什么格式 | 真实设备日志显示是十进制度（Issue #133），驱动源码里没有做 ddmm→度的换算。**建议第一次上机时把 `getGPSraw()` 的原始字符串和 `getGPS()` 的数值一起打印出来对一遍**——这是最保险的验证方法。 |
| `AT+CGNSSINFO` 在某些固件上是否真的一直为空 | Issue #477 用户报告有（固件 `B04V01_241010`），作者不认可（认为是他没开天线电）。**需要实测。** |
| `stream.read()` 读 N/S、E/W 偶尔返回 255 的问题是否还在 | 只在 Issue #133 的一条用户评论里出现，作者未回复确认。当前源码（`TinyGsmClientSIM7672.h` 第 834、837 行）仍然是 `stream.read()` 写法。 |
| 第 2 到第 4 个卫星数字段的排列顺序 | SIMCom 手册说是 GPS / GLONASS / GALILEO / BEIDOU；TinyGSM 源码注释写的是 GPS / BEIDOU / GLONASS / GALILEO。因为驱动只用第一个（GPS 的可见卫星数），**所以不影响 `getGPS()` 的 `vsat`**；但如果你要分别统计各星座卫星数，需要实测确认顺序。 |
| 冷启动实际要多久、功耗多少（对项圈续航很关键） | 手册和官方文档都没给 TTFF 数值，必须自己测。 |
| 这块板子有没有 ESP32 侧的 GPS 电源控制脚 | `utilities.h` 里没有定义（`BOARD_POWERON_PIN` 对这块板也未定义）。**据源码判断是没有，但没找到官方明文说明。** |

---

## 6. 参考来源 / Sources

### LilyGO 官方仓库（主源）

- LilyGO 官方仓库（T-SIM7670G-S3 属于这个仓库）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series
  - 板子说明文档（英文）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD
  - 引脚定义 `utilities.h`：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_BuiltIn/utilities.h
  - `examples/GPS_BuiltIn/GPS_BuiltIn.ino`（推荐的最小路径）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_BuiltIn/GPS_BuiltIn.ino
  - `examples/GPS_BuiltInEx/GPS_BuiltInEx.ino`（信息更全）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_BuiltInEx/GPS_BuiltInEx.ino
  - `examples/GPS_NMEA_Parse/GPS_NMEA_Parse.ino`（TinyGPSPlus 路线）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_NMEA_Parse/GPS_NMEA_Parse.ino
  - `examples/GPS_NMEA_Output/GPS_NMEA_Output.ino`（NMEA 转发）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_NMEA_Output/GPS_NMEA_Output.ino
  - TinyGSM 分支版 SIM7670G 驱动：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/lib/TinyGSM/src/TinyGsmClientSIM7672.h
  - TinyGSM GPS 接口：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/lib/TinyGSM/src/TinyGsmGPS.tpp
  - 自动编译配置（`.skip` 机制）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/.github/workflows/platformio.yml
  - 板子原理图（V1.1）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/schematic/esp32s3/T-SIM7670G-S3-V1.1.pdf
- LilyGO 官方 wiki（⚠️ 引脚号有误，见第 5 节）：https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html
- 产品页：https://www.lilygo.cc/products/t-sim-7670g-s3

### GitHub Issues（官方作者亲自回复）

- Issue #394「T-SIM7670G-S3 GNSS NOT WORKING」—— 作者回复"你没开 GPS 供电"：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/394
- Issue #477「GNSS not working on T-SIM7670G-S3-Standard」—— 天线供电 + NMEA 替代方案讨论：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/477
- Issue #375「T-SIM7670G S3 use GPIO4」—— 作者澄清两个 GPIO4 的区别：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/375
- Issue #133「SIM7670G GPS reporting wrong hemisphere」—— 真实 `AT+CGNSSINFO` 输出日志 + 南北半球符号 bug 修复：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/133
- Issue #467「[SIM7670G-MNGV] GPS not working」：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/467

### SIMCom 官方 AT 指令手册

- 《SIM767XX Series_AT Command Manual_V1.02》（第 21 章 "AT Commands for GNSS"，含 AT+CGNSSPWR / AT+CGNSSTST / AT+CGPSCOLD/WARM/HOT / AT+CGNSSIPR / AT+CGNSSMODE / AT+CGNSSNMEA / AT+CGNSSNMEARATE / AT+CGNSSPORTSWITCH / AT+CGNSSINFO / AT+CGNSSPROD）：
  https://download.mikroe.com/documents/datasheets/SIM767xx_Series_AT_command_manual_v1.02.pdf
- SIMCom 官方产品资料页（SIM7670X）：https://cn.simcom.com/product/SIM7670X.html

### TinyGPSPlus 官方仓库

- 仓库：https://github.com/mikalhart/TinyGPSPlus
- 头文件（API）：https://github.com/mikalhart/TinyGPSPlus/blob/master/src/TinyGPS%2B%2B.h
- 说明：TinyGPSPlus 官网 https://arduiniana.org/libraries/tinygpsplus/ ；LilyGO 仓库内置的版本为 v1.0.3（见 `lib/TinyGPSPlus/library.properties`）

### ESP32 Arduino core 官方文档

- Arduino-ESP32 文档（`HardwareSerial::begin(baud, config, rxPin, txPin)` 用法）：https://docs.espressif.com/projects/arduino-esp32/en/latest/
- ESP32-S3 数据手册（引脚说明）：https://documentation.espressif.com/esp32-s3_datasheet_en.pdf

---

## 附：给这个宠物项圈项目的三句话总结

1. **不用买额外的 GPS 模块** —— SIM7670G 自带，天线插板子上的 `GNSS` 接口就行（要买**有源**天线）。
2. **代码最短的路**：复制 LilyGO `lib/` 到 Arduino 库目录 → 用 `GPS_BuiltIn` 的样子写 → `modem.enableGPS(4, 1)` → `modem.getGPS(...)`。
3. **做实物时最该盯的两件事**：① 开 GPS 前必须发那两条 `AT+CGDRT/CGSETV`；② 冷启动要等，室内收不到，得先拿到室外试。
