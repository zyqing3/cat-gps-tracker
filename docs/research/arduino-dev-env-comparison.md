# 用哪个开发环境来自动编译/烧录 LilyGO T-SIM7670G-S3？

> 调研日期：2026-10-05
> 调研问题：在 Arduino C++ 前提下，哪种开发环境最适合 **agent（自动化程序）全自动**编译、烧录、测试 LilyGO T-SIM7670G-S3（ESP32-S3 + SIM7670G 内置 GPS）开发板？
> 说明：本文所有重要结论都附了来源链接。凡是本次没能亲自验证的，都会明确写"未验证"。

---

## 1. 一句话结论

**推荐用 Arduino CLI（命令行版 Arduino）作为主力，日常"自动编译 + 烧录 + 看串口"全都用它。**

理由只有三条，但每条都很硬：

1. **它就是 Arduino IDE 的"发动机"。** Arduino IDE 2.x 的编译和烧录，本来就是交给后台一个 arduino-cli 程序去做的（[来源](https://github.com/arduino/arduino-ide)）。所以用 CLI 不等于放弃 Arduino IDE 那套流程，反而更彻底。
2. **在这台中国区 Windows 电脑上，一条命令就能装好**：`winget install ArduinoSA.CLI`（本机实测 winget 里确实有 `ArduinoSA.CLI` 1.5.1）。而 PlatformIO 在 winget 里**没有**包。
3. **只有 CLI 能优雅解决本项目最大的一个坑**：LilyGO 官方示例依赖的是他们**自己改过的 TinyGSM**（见第 6 节），这个改过的版本不在官方库管理器的列表里。Arduino CLI 可以用 `--libraries <文件夹>` 直接指定它，**不用改任何文件、不用往系统库目录里复制东西**。

**PlatformIO 是很好的备选**（LilyGO 官方自己优先推荐它，而且他们的自动构建脚本证明了它也能全自动跑），但要接受"官方平台版本锁得比较旧"和"中国区下载慢且没有官方镜像"两个问题。详见第 7 节。

**Arduino IDE 不适合 agent**：它是图形界面软件，得用鼠标点，agent 没法可靠地驱动它。

---

## 2. 先说清楚几个名词

| 名词 | 大白话解释 |
| --- | --- |
| **编译** | 把你写的 C++ 代码"翻译"成芯片能懂的机器码。翻译结果是一个 `.bin` 文件。 |
| **烧录**（也叫上传/下载） | 把这个 `.bin` 文件通过 USB 线写进开发板的芯片里。 |
| **串口** | 电脑和开发板之间的一根"对讲机通道"。板子用 `Serial.print()` 往外说话，你在电脑上就能看到它打印的内容，用来调试。 |
| **COM 口** | Windows 给这根 USB 通道起的编号，比如 `COM5`。不同的 USB 插口/线，编号可能不同。 |
| **开发环境** | 一整套工具：编辑器 + 编译器 + 烧录器 + 串口监视器，帮你完成"写代码→编译→烧录→看结果"。 |
| **核心包**（core / 平台包） | 芯片厂商提供的一大包底层代码，告诉编译器"ESP32-S3 这块芯片长什么样、怎么用"。没有它，连 `setup()` 都不认识。 |
| **库**（library） | 别人写好的现成代码，拿来解决具体问题。比如 TinyGSM 就是专门帮你"跟 4G 模块聊天"的库。 |
| **agent** | 能自己读文档、自己敲命令、根据输出决定下一步的自动化程序（不是人手动点鼠标）。 |

---

## 3. 三种环境分别是什么

### 3.1 Arduino IDE —— 图形界面的"新手友好版"

就是那个有窗口、有按钮、有菜单的软件。装好后从"开发板管理器"里装 ESP32 核心包，然后选板子、选 COM 口、点"上传"按钮。
它的**编译和上传其实是后台的 arduino-cli 在干活**（[来源](https://github.com/arduino/arduino-ide)）。所以它上手最容易，但**要靠鼠标操作**。

### 3.2 Arduino CLI —— 同一个引擎，但只认命令

官方的原话是："Arduino CLI 提供了你在 Arduino IDE 里能找到的全部功能"（[来源](https://arduino.github.io/arduino-cli/1.5/getting-started/)）。它只有一个可执行文件，全部靠命令：

```
arduino-cli core install esp32:esp32     # 装核心包
arduino-cli compile -b esp32:esp32:esp32s3 mysketch   # 编译
arduino-cli upload  -b esp32:esp32:esp32s3 -p COM5 mysketch  # 烧录
arduino-cli monitor -p COM5              # 看串口输出
```

它还有一个对 agent 特别重要的开关：`--json`，能把结果用机器可读的 JSON 格式输出（[来源](https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_compile/)）。当前文档版本是 **1.5**。

### 3.3 PlatformIO —— 给"正规工程"准备的

它有两个形态：VS Code 里的图形插件，和独立的命令行 `pio`（PlatformIO Core）。
它的特点是**用一份配置文件 `platformio.ini` 描述整个工程**——用哪个平台、哪块板子、开什么编译开关、库从哪来，全写在文件里。别人拿到这份文件就能复现你的环境。

PlatformIO 在文档里把安装方式分成"安装脚本（推荐）"和"Python 包管理器（pip）"两种（[来源](https://docs.platformio.org/en/latest/core/installation/index.html)）。**pip 方式不需要 VS Code，纯命令行就能跑**。

### 3.4 三者的关系图解

```
Arduino IDE（图形界面）
      └── 后台偷偷调用 ──> arduino-cli
                              │
PlatformIO（图形插件 / pio 命令）── 完全是另一套，自己下载工具链
```

---

## 4. 对比表

| 对比项 | Arduino IDE | **Arduino CLI（推荐）** | PlatformIO |
| --- | --- | --- | --- |
| 能否命令行全自动 | ❌ 图形界面，必须点鼠标 | ✅ 纯命令 | ✅ 纯命令（`pio`） |
| 能否被 agent 调用 | ❌ 不可靠 | ✅ 可以，还有 `--json` 机器可读输出 | ✅ 可以 |
| 本机怎么装 | 官网下载安装包 | ✅ **`winget install ArduinoSA.CLI`**（本机实测有 1.5.1） | ❌ winget 无包；用 `python -m pip install platformio`（本机有 Python 3.12.2） |
| 装核心包 | 图形界面里点 | `arduino-cli core install esp32:esp32` | 自动下载（按 `platformio.ini` 里的 `platform` 字段） |
| 中国区下载速度 | 可用官方中国镜像（见第 7.5 节） | ✅ 可用同一个官方中国镜像 | ⚠️ **没有官方文档化的中国镜像**（未验证） |
| 装库 | 图形界面里点 | `lib install`；或编译时用 `--libraries` 直接指目录 | 写在 `platformio.ini` 的 `lib_deps` 里 |
| 编译 + 烧录 + 串口监控 | 三个按钮 | `compile` / `upload` / `monitor` 三条命令 | `pio run` / `-t upload` / `pio device monitor` |
| LilyGO 官方示例支持 | ✅ 官方文档写了完整设置表 | ✅ 用同一套核心包和库，命令一一对应 | ✅ **官方首选推荐**，仓库自带现成 env 和自动构建脚本 |
| 对 LilyGO 改版 TinyGSM 的支持 | 要手动把 `lib/` 复制到系统库目录 | ✅ `--libraries <路径>` 直接指过去，不动系统 | ✅ 仓库自带 `lib/`，PlatformIO 自动认 |
| 上手难度 | 最简单 | 中等（要记命令） | 中等偏难（要懂 `platformio.ini`） |
| agent 友好度 | ★☆☆☆☆ | ★★★★★ | ★★★★☆ |

---

## 5. LilyGO 官方对 T-SIM7670G-S3 的支持情况

### 5.1 官方仓库叫什么？

**没有**单独叫 `T-SIM7670G-S3` 的仓库。这块板的示例代码在：

**`Xinyuan-LilyGO/LilyGo-Modem-Series`** —— https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series

仓库简介写的是 "LilyGo A7670X A7608X SIM7670G SIM7000G SIM7080 SIM7600 series"，最后一次提交是 2026-09-30（本次查询时）。

> 注意：网上有些资料把它写成 `LilyGo-T-A76XX`，那是这个仓库的**旧名字**，现在改名叫 LilyGo-Modem-Series 了。

### 5.2 这块板有两个版本，**别搞混**

这是最容易踩的坑，两个版本 PSRAM 不一样：

| 产品 | 芯片 | Flash | PSRAM | 官方文档里的宏 |
| --- | --- | --- | --- | --- |
| T-SIM7670G**-S3** | ESP32-S3-WROOM-1 | 16MB | **8MB（OPI）** | `LILYGO_T_SIM7670G_S3` |
| T-SIM7670G-S3-**Standard** | ESP32-S3-WROOM-1 | 16MB | **2MB（QSPI）** | `LILYGO_SIM7670G_S3_STAN` |

- 8MB/OPI 版本文档：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD
- 2MB/QSPI 版本文档：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3-standard/README.MD

**选错 PSRAM 会导致板子反复重启。** 这是新手最常见的"板子坏了？"原因。

### 5.3 官方示例默认怎么编译？

官方**优先推荐 PlatformIO**（文档里 "PlatformIO Quick Start" 排在 "Arduino IDE quick start" 前面）。
`platformio.ini` 里已经有现成的环境（[来源](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/platformio.ini)）：

- `default_envs = T-SIM7670G` —— 对应 8MB OPI 版本，用 `board = esp32s3box`
- `default_envs = T-SIM7670G-S3-Standard` —— 对应 2MB QSPI 版本，用 `board = esp32-s3-wroom-1-n16r2`

平台版本被**锁死在 `espressif32@6.12.0`**，串口速度 `monitor_speed = 115200`。

> ⚠️ **官方文档里有个笔误**：文档叫你取消注释 `default_envs = T-SIM7670G-S3`，但 `platformio.ini` 里**根本没有这一行**。这是真实的 issue：[#413](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/issues/413)（2025-12-06）。实际要写 `T-SIM7670G` 或 `T-SIM7670G-S3-Standard`。

> ⚠️ 另外 `esp32-s3-wroom-1-n16r2` **不是** PlatformIO 官方平台里的板子，是 LilyGO 自己在仓库里带的 `boards/esp32-s3-wroom-1-n16r2.json`（[来源](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/boards/esp32-s3-wroom-1-n16r2.json)）。

### 5.4 用 Arduino IDE 时官方要求的设置

官方文档给的设置表（两个版本通用，只有 PSRAM 那一行不同）：

| 设置名 | 值 |
| --- | --- |
| Board（开发板） | **ESP32S3 Dev Module** |
| USB CDC On Boot | **Enable**（必须开，否则串口看不到打印） |
| CPU Frequency | 240MHz (WiFi) |
| Flash Size | **16MB (128Mb)** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | **OPI PSRAM**（Standard 版选 **QSPI PSRAM**） |
| Upload Speed | 921600 |
| Programmer | **Esptool** |

### 5.5 官方示例依赖哪些库？

官方文档明确要求：**把仓库里 `lib` 文件夹下的所有文件夹复制到你的 Arduino 库目录**，并且特别警告：

> "打开 Arduino IDE 时会提示有库可以更新，**请不要点更新**……更新后可能出现问题，或者默认配置（比如 TinyGSM）会被覆盖。"

（[来源](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD)）

这句话说明了本项目的**核心风险**：**必须用 LilyGO 自己那份 TinyGSM**，不能用官方最新版。原因见第 6 节。

主要库：
- **TinyGSM**（LilyGO 改版，在 `lib/TinyGSM`）
- **TinyGPSPlus**（解析 GPS 的 NMEA 数据，在 `lib/TinyGPSPlus`）
- 可选：StreamDebugger（想看 AT 指令流水时用）

### 5.6 一个反例：官方示例里用的宏

`examples/GPS_BuiltIn/utilities.h` 里给这块板定义的是：

```c
#ifndef TINY_GSM_MODEM_SIM7670G
    #define TINY_GSM_MODEM_SIM7670G
#endif
```

**这个宏是 LilyGO 自己发明的，官方 TinyGSM 里没有。** 下一节细说。

### 5.7 官方自动构建脚本证明"全自动"可行

仓库里有 `.github/workflows/platformio.yml`（[来源](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/.github/workflows/platformio.yml)）。它做的事是：

```bash
pip install --upgrade platformio
PLATFORMIO_SRC_DIR=examples/GPS_BuiltIn pio run -e T-SIM7670G
```

运行在 `ubuntu-latest` 上，一次跑几十个示例。**这就是"没有任何人工点击、全自动编译"的铁证**——LilyGO 自己就是这么做的。

---

## 6. TinyGSM 对 SIM7670（SIM7670G / A7670 系列）的支持情况

这一节是整个调研里**最重要**的部分，请仔细看。

### 6.1 官方 TinyGSM：**完全不支持 SIM7670**

对官方仓库 `vshymanskyy/TinyGSM` 的 master 分支源码做了完整检索：

| 检查项 | 结果 |
| --- | --- |
| 有没有 `TinyGsmClientSIM7670.h`？ | ❌ **没有** |
| 整个仓库出现 "sim7670" 几次？ | **0 次** |
| 整个仓库出现 "a7670" 几次？ | **0 次** |
| README 支持列表里有没有 SIM7670/A7670？ | ❌ 没有（只有 "SIMCom A7672X CAT-M1 Module"） |
| 当前版本 / 最后提交 | 0.12.0 / 2026-06-24 |

### 6.2 官方 TinyGSM 有个"长得像"的，但**不能定位**

官方里有 `TinyGsmClientA7672x.h`（对应宏 `TINY_GSM_MODEM_A7672X`）。

但打开这个文件看 GPS 部分，里面写着：

```
/* GPS/GNSS/GLONASS location functions */
// No functions of this type supported
```

还有 GSN 定位、时间、NTP 也都是 "No functions of this type supported"。

**翻译：官方 TinyGSM 的 A7672X 客户端压根没有定位功能。** 而我们的项目核心就是要 GPS 定位。所以**官方 TinyGSM 走不通**。

### 6.3 LilyGO 的改版 TinyGSM：才是能用的那个

LilyGO 在仓库里放了一份**自己改过的 TinyGSM**（`lib/TinyGSM/`），比官方多加了一堆文件：

| 官方版有吗 | 文件 | 作用 |
| --- | --- | --- |
| ❌ | `TinyGsmClientSIM7672.h` | **SIM7670G 的客户端本体** |
| ❌ | `TinyGsmClientA7670.h` | A7670 系列 |
| ❌ | `TinyGsmClientA7608.h` | A7608 系列 |
| ❌ | `TinyGsmGPS_EX.tpp` | **扩展的 GPS 功能** |
| ❌ | `TinyGsmHttpsSIM7xxx.h`、`TinyGsmMqttSIM7xxx.h` | HTTPS / MQTT |
| ❌ | `TinyGsmEmail.tpp`、`TinyGsmTextToSpeech.tpp`、`TinyGsmFSComm.tpp` | 邮件 / 语音 / 文件系统 |

改版里的 `TinyGsmClient.h` 这样分发（[来源](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/lib/TinyGSM/src/TinyGsmClient.h)）：

```c
#define TINY_GSM_FORK_LIBRARY        // 第 13 行：给自己发个"我是改版"的记号
...
#elif defined(TINY_GSM_MODEM_SIM7672) || defined(TINY_GSM_MODEM_SIM7670G)
#include "TinyGsmClientSIM7672.h"
```

LilyGO 在 `platformio.ini` 的注释里还写了：**"SIM7672G 和 SIM7670G 完全一样，只是改了个名字"**。

**两份 TinyGSM 的差别，用一张表说清楚：**

| | 官方 TinyGSM | LilyGO 改版 TinyGSM |
| --- | --- | --- |
| 有 `TINY_GSM_MODEM_SIM7670G` 分支吗 | ❌ 没有 | ✅ 有 |
| 有 `#define TINY_GSM_FORK_LIBRARY` 记号吗 | ❌ 没有（全仓库搜不到） | ✅ 第 13 行有 |
| 走到最后会怎样 | 落到 `#else` 分支 | 正确找到 SIM7672 客户端 |
| 落到 `#else` 的后果 | **`#error "Please define GSM modem model"`——编译直接停住** | — |

（官方版结尾的 `#else / #error` 可在[这里](https://github.com/vshymanskyy/TinyGSM/blob/master/src/TinyGsmClient.h)看到。）

**所以结论是：**

> 写 `#include <TinyGsmClient.h>` 时，编译器必须能找到 **LilyGO 那份**，而不是官方那份。
> 如果用库管理器装了官方的 TinyGSM，编译会**直接停在 `#error "Please define GSM modem model"`**，报错信息看起来像是"你没定义模块型号"，很容易把人带偏——其实你定义了，只是**官方版不认这个型号**。

### 6.4 GPS 怎么读？

官方示例的路子是（`examples/GPS_BuiltIn/GPS_BuiltIn.ino`）：

```c
TinyGsm modem(SerialAT);      // SerialAT 是连 4G 模块的串口
modem.testAT(1000);           // 先确认能跟模块说上话

// 打开 GPS。注意要带两个参数：给 GPS 天线供电用的"模块 GPIO 号"和电平
while (!modem.enableGPS(MODEM_GPS_ENABLE_GPIO, MODEM_GPS_ENABLE_LEVEL)) { Serial.print("."); }

// 读定位。它一次要填一整套值（下面这些变量要先在程序里声明好）
uint8_t fixMode;
float lat, lon, speed, alt, accuracy;
int vsat, usat;
int year, month, day, hour, minute, second;
modem.getGPS(&fixMode, &lat, &lon, &speed, &alt, &vsat, &usat, &accuracy,
             &year, &month, &day, &hour, &minute, &second);
```

（这两个函数的参数写法是照 `examples/GPS_BuiltIn/GPS_BuiltIn.ino` 里的真实调用抄的，不是简写。）

先用 `pinMode(BOARD_PWRKEY_PIN, OUTPUT)` + 拉高一小段时间来**给模块开机**（模块不是上电就自动开的）。

> ⚠️ **一处对不上的地方**：`GPS_BuiltIn.ino` 开头的注释写 "GPS only supports A7670X/A7608X/SIM7000G/SIM7600 series"（没提 SIM7670G）。但 LilyGO 在同一仓库的型号对比表里明确写了 **SIM7670G 支持 GPS（✅）**，而且他们的自动构建脚本里 `GPS_BuiltIn` 对 `T-SIM7670G` 这个环境**没有被跳过**（没有 `.skip.T-SIM7670G` 标记文件）。所以我判断**那句注释是 2023 年留下的旧文案，已经过时**，但没有真机实测，标为"待验证"。

### 6.5 本板引脚（来自 `examples/GPS_BuiltIn/utilities.h`）

**T-SIM7670G-S3（8MB OPI 版）：**

| 名称 | GPIO |
| --- | --- |
| 模块 TX | 11 |
| 模块 RX | 10 |
| 模块 DTR | 9 |
| 模块 PWRKEY（开机键） | 18 |
| 模块 RING | 3 |
| 模块 RESET | 17 |
| GPS 使能 | 4（高电平有效） |

**T-SIM7670G-S3-Standard（2MB QSPI 版）：** 模块 TX=4、RX=5、PWRKEY=46、RING=6、DTR=7。

> ⚠️ 网上有些教程写 "RX=4, TX=5, PWR_KEY=12"，那是**更老的板子版本或 Standard 版**的引脚。以你手上板子的实际丝印和仓库里的 `utilities.h` 为准。

---

## 7. 推荐方案 + 具体命令

### 7.1 推荐：Arduino CLI

**核心思路**：不往系统库目录复制任何东西、不改任何文件，全部用命令行参数搞定。

下面命令里的 `<仓库>` 指你下载的 LilyGo-Modem-Series 文件夹路径。

#### 第 1 步：装 Arduino CLI

```bash
winget install ArduinoSA.CLI --accept-source-agreements --accept-package-agreements
```

（本次在本机实测：winget 源里确实有 `ArduinoSA.CLI`，版本 1.5.1。）

#### 第 2 步：配置 + 装 ESP32 核心包（用官方中国镜像）

```bash
arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls \
  "https://jihulab.com/esp-mirror/espressif/arduino-esp32/-/raw/gh-pages/package_esp32_index_cn.json"
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

**为什么用这个地址？** 这是 Espressif 官方文档里给出的**中国镜像**（[来源](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)）。实测该索引里的核心包和编译器工具链都指向 `https://dl.espressif.cn/...`，在国内下载会明显更快。它上面的版本号带 `-cn` 后缀（例如 `3.3.10-cn`），**可能比国际版最新版慢一点点**（国际版当前稳定版是 3.3.12），属正常现象。

#### 第 3 步：编译（关键就一条命令）

```bash
arduino-cli compile \
  -b esp32:esp32:esp32s3 \
  --board-options "PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio" \
  --libraries "<仓库>/lib" \
  --build-property "compiler.cpp.extra_flags=-DLILYGO_SIM7670G_S3_STAN" \
  --output-dir ./build \
  "<仓库>/examples/GPS_BuiltIn"
```

这一条命令里藏着三个关键技巧：

| 参数 | 干什么用 | 为什么需要 |
| --- | --- | --- |
| `--libraries "<仓库>/lib"` | 告诉编译器去 LilyGO 的 `lib` 文件夹找库 | **这样就用上了他们改过的 TinyGSM**，不用复制到系统目录 |
| `--build-property ...extra_flags=-DLILYGO_T_SIM7670G_S3` | 命令行定义一个宏 | **不用手动去改 `utilities.h` 删注释**，agent 完全不用编辑源码 |
| `--board-options` | 设置 PSRAM/Flash/分区/USB | 等价于 Arduino IDE 里下拉菜单的那几项，**设错了会重启** |

`--board-options` 的名字和取值，是从 ESP32 核心包的 `boards.txt` 里查出来的（[来源](https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt)）：

| 选项名 | 本板该填 | 含义 |
| --- | --- | --- |
| `PSRAM` | `enabled`（本板为 Standard 版 2MB QSPI；8MB Pro 版才是 `opi`） | OPI 还是 QSPI 内存 |
| `FlashSize` | `16M` | 板子是 16MB 闪存 |
| `PartitionScheme` | `app3M_fat9M_16MB` | 官方要求的 "16M Flash (3MB APP/9.9MB FATFS)" |
| `CDCOnBoot` | `cdc` | **USB CDC On Boot = Enable** |
| `USBMode` | `hwcdc` | USB Mode = Hardware CDC and JTAG |
| `FlashMode` | `qio` | Flash Mode = QIO 80MHz |

> ✅ 本次实测核对：上面这 6 个选项名和取值，都在 arduino-esp32 **3.3.12 版的 `boards.txt`** 里逐条查到，拼写完全对得上（`PSRAM=opi` 那行写的是 "OPI PSRAM"，`enabled` 那行是 "QSPI PSRAM"）。
> 另外，本次下载了 arduino-cli **1.5.1** 的正式版本，用 `--help` 确认了本节用到的参数**全部存在**：`--libraries`（"Path to a collection of libraries"）、`--board-options`（注明支持逗号分隔或多写几次）、`--build-property`、`--output-dir`、`--json`，以及 `upload` 的 `--input-dir`、`monitor` 的 `-c` / `--timestamp`。

#### 第 4 步：烧录

```bash
arduino-cli board list                      # 先看板子在哪个 COM 口
arduino-cli upload -b esp32:esp32:esp32s3 -p COM5 --input-dir ./build
```

（`upload` 命令本身**不会先编译**，所以要先用上一步的 `compile`，或者加 `--input-dir` 指到编译产物。）

#### 第 5 步：看串口 + 机器可读输出

```bash
arduino-cli monitor -p COM5 -c baudrate=115200 --timestamp
arduino-cli compile -b esp32:esp32:esp32s3 ... --json   # 让 agent 解析 JSON
```

### 7.2 备选：PlatformIO 的 `platformio.ini`

如果你更想要"一份文件描述整个工程"的方式：

```ini
[env:t-sim7670g-s3]
platform = espressif32@6.12.0        ; 先跟官方仓库保持一致，别急着升级
framework = arduino
board = esp32s3box                    ; 对应 8MB OPI 版本
build_flags =
    -DLILYGO_T_SIM7670G_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=0
monitor_speed = 115200
upload_port = COM5
lib_extra_dirs = ./LilyGo-Modem-Series/lib   ; 关键：用 LilyGO 改版的 TinyGSM
```

然后：

```bash
python -m pip install --upgrade platformio
pio run -e t-sim7670g-s3               # 编译
pio run -e t-sim7670g-s3 -t upload     # 烧录
pio device monitor                     # 看串口
```

**但要知道 PlatformIO 的几个现实问题：**

| 问题 | 事实 |
| --- | --- |
| 官方平台有没有这块板的定义？ | ❌ **没有**。官方 `espressif32` 平台 v7.1.3 共 242 块板子，LilyGO 的只有 `lilygo-t-display`、`lilygo-t-display-s3`、`lilygo-t3-s3` 和几块 `ttgo-*`，**没有任何 T-SIM 系列**。PlatformIO 仓库搜 "sim7670" 结果是 **0 条**。 |
| 那 LilyGO 用的 `esp32-s3-wroom-1-n16r2` 是哪来的？ | 是他们**自己放在仓库 `boards/` 里的自定义板子文件**。 |
| 官方平台版本有多新？ | 最新是 **v7.1.3（2026-09-11）**，但 LilyGO **锁在 6.12.0（2025-07-31）**。 |
| 6.12.0 配的是哪版 Arduino 核心？ | 对应的框架包版本是 `~3.20017.0`，**即 Arduino ESP32 核心 2.0.17**——而当前稳定核心已经是 **3.3.12**。也就是说官方示例是按**老一代核心**验证的。 |
| Python 3.12 能用吗？ | ✅ 能。PlatformIO Core 从 6.1.12 起支持 Python 3.12（[来源](https://docs.platformio.org/en/stable/core/history.html)），本机 Python 3.12.2 满足要求。 |
| 中国区下载快吗？ | ⚠️ **未验证**。有用户 issue 反映下载慢、且没有可配置的官方镜像（[#5484](https://github.com/platformio/platformio-core/issues/5484)、[#4345](https://github.com/platformio/platformio-core/issues/4345)），但**我没有在 PlatformIO 官方文档里找到任何"中国镜像"的正式配置项**。 |

### 7.3 为什么不推荐 Arduino IDE

- 它是图形界面软件，agent 没法可靠地"点按钮"。
- 每次换示例都要手动改 `utilities.h`、手动选下拉菜单——这些都没法脚本化。
- 唯一优势是新手看得见摸得着，适合**人类**第一次点亮板子时用。

> 建议：**第一次拿到板子时，可以先用 Arduino IDE 按第 5.4 节表格点亮一次**，确认硬件没坏、USB 线能认；之后日常全交给 Arduino CLI。

### 7.4 Windows 上常见的坑

| 坑 | 现象 | 处理办法 |
| --- | --- | --- |
| **进不了下载模式** | 一直提示上传失败 | 官方 FAQ：**按住 BOOT 键 → 按一下并松开 RST → 再松开 BOOT**，就进入下载模式了（[来源](https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html)） |
| **串口监视器一片空白** | 烧录成功了，但 `Serial.print` 什么都没显示 | ESP32-S3 有两个 USB 口，`Serial` 走哪条路由 `USB CDC On Boot` 决定。**用 USB 口就要开 CDC**（`-DARDUINO_USB_CDC_ON_BOOT=1`）并设 USB Mode 为 Hardware CDC and JTAG（`-DARDUINO_USB_MODE=1`）；用 UART 口则要关掉它（[来源](https://docs.espressif.com/projects/arduino-esp32/en/latest/troubleshooting.html)） |
| **PSRAM 设错** | 板子反复重启 / 起不来 | 8MB 版选 **OPI**，Standard 2MB 版选 **QSPI**。两个版本不一样！ |
| **USB 驱动** | 设备管理器里出现带感叹号的未知设备 | 本板用的是 ESP32-S3 **原生 USB**（不是 CH343/CH9102 转串口芯片），理论上 Win11 免驱。**如果 COM 口不出现**，优先怀疑：① 线是"只能充电"的劣质线；② 插错了板上的 USB 口。关于"必须装 CH343/CH9102 驱动"的说法，**本次未在 LilyGO 官方文档中找到对本板的此类要求，标为未验证** |
| **COM 口号会变** | 昨天 COM5，今天 COM7 | 正常。agent 脚本里应该用 `arduino-cli board list --json` 动态查，别写死 |
| **库被"自动更新"覆盖** | 昨天能编译，今天报 `#error "Please define GSM modem model"` | 说明官方 TinyGSM 把 LilyGO 改版覆盖了。LilyGO 明确警告**不要点库更新**。用 CLI 的 `--libraries` 方式可以彻底避免这个问题（见 6.3 节） |
| **AT 指令看不到** | 不知道模块在干嘛 | 示例里取消注释 `// #define DUMP_AT_COMMANDS`（需要 `StreamDebugger` 库） |

### 7.5 中国区下载的补充说明

| 要下什么 | 推荐办法 | 验证状态 |
| --- | --- | --- |
| Arduino CLI 本体 | `winget install ArduinoSA.CLI` | ✅ 本机实测可查到 1.5.1 |
| ESP32 核心包 + 编译器工具链 | 用官方中国镜像 `package_esp32_index_cn.json` | ✅ 已实测：索引里指向 `dl.espressif.cn` |
| PlatformIO 本体 | `python -m pip install platformio`，pip 可换国内源 | pip 换源是通用做法，**但本次没找到 PlatformIO 官方文档对此的说明，标为未验证** |
| PlatformIO 平台包/工具链 | ❓ **未验证**。没找到官方文档化的中国镜像配置项 | ❌ 未验证 |

---

## 8. 未验证 / 不确定的地方（请务必看）

1. **没有真机实测**。本文所有命令都是根据官方文档和源码推导出来的，**没有在这块板子上真的编译烧录过**（本机当前没装 ESP32 核心包，也没有这块板）。
   已经逐条核对过的部分：**arduino-cli 1.5.1 的真实参数表**（下载正式版程序跑 `--help` 得到）、**ESP32 核心 3.3.12 的板子选项取值**（`boards.txt` 原文）、**中国镜像索引**（下载后确认指向 `dl.espressif.cn`）、**PlatformIO 平台版本映射**（`platform.json` 原文）、**LilyGO 引脚表**（`utilities.h` 原文）、**两份 TinyGSM 的差异**（源码原文）。
2. **LilyGO 示例能不能配 Arduino 核心 3.x 编译，未验证。** 他们自己的 PlatformIO 配置锁的是 **2.0.17**（老一代），而当前稳定核心是 **3.3.12**。如果直接用最新核心编译失败，**降级到 2.0.17**（`arduino-cli core install esp32:esp32@2.0.17`）是最稳的退路。
3. **`GPS_BuiltIn.ino` 里"GPS 只支持 A7670X/A7608X/SIM7000G/SIM7600"那句注释**，与官方型号表（SIM7670G 支持 GPS ✅）**互相矛盾**。我判断注释是旧文案，但没有实测，请以实机为准。
4. **`--board-options` 的取值组合**：选项名和取值已和 arduino-esp32 **3.3.12 的 `boards.txt`** 逐条核对，全部存在且含义一致；`arduino-cli 1.5.1` 也实测确认支持"逗号分隔"写法。但**没有真正跑过一次完整编译**——如果这套组合报错，可以退回用 `--build-property` 一个个传（例如 `build.psram_type=opi`）。
5. **PlatformIO 的中国镜像** —— 官方文档里**没有**找到。网上流传的清华源、`core_mirror_url` 等做法，**本次无法从一手来源确认**（有中文文章说清华的 PlatformIO 镜像路径已经 404）。所以本文不给具体镜像地址。
6. **`TINY_GSM_MODEM_SIM7670G` 宏与上游的关系**：可以确定上游没有这个宏、LilyGO 有；但 **LilyGO 从未把这份改版 TinyGSM 单独发布到库管理器**，所以只能从他们仓库拿，这一点请按"必须"对待。
7. **USB 驱动**：LilyGO 官方文档里**没有**提到本板需要装 CH343/CH9102 驱动。坊间说法可能与别的板子混淆了。
8. **`esp32s3box` 这个 board 定义与 8MB OPI 版的匹配度**：我核对了 `esp32s3box` 的内存类型确实是 `qio_opi`、16MB flash、921600 速度、CDC 开启，与该版本吻合；但**这是"能对上"，不等于"LilyGO 实测过"**。
9. 如果搜索结果与本文冲突，**以本文引用的官方原始页面为准**（例如：官方文档写 `default_envs = T-SIM7670G-S3` 是笔误，实际文件名里没有这一行，已由 issue #413 证实）。

---

## 9. 参考来源

**LilyGO 官方**
- 主仓库（示例代码所在地）：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series
- 8MB OPI 版文档：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3/README.MD
- 2MB QSPI Standard 版文档：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/sim7670g-s3-standard/README.MD
- 官方 `platformio.ini`：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/platformio.ini
- 官方自动构建脚本：https://github.com/Xinyuan-LilyGo/LilyGo-Modem-Series/blob/main/.github/workflows/platformio.yml
- GPS 示例：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_BuiltIn/GPS_BuiltIn.ino
- 引脚定义：https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/GPS_BuiltIn/utilities.h
- 自定义板子文件：https://github.com/Xinyuan-LilyGo/LilyGo-Modem-Series/blob/main/boards/esp32-s3-wroom-1-n16r2.json
- LilyGO 改版 TinyGSM 的客户端分发文件：https://github.com/Xinyuan-LilyGo/LilyGo-Modem-Series/blob/main/lib/TinyGSM/src/TinyGsmClient.h
- 官方 Wiki 快速开始（含 BOOT 键 FAQ）：https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html
- 已知笔误 issue #413：https://github.com/Xinyuan-LilyGo/LilyGo-Modem-Series/issues/413

**TinyGSM**
- 官方仓库：https://github.com/vshymanskyy/TinyGSM
- 官方 README（支持型号表）：https://github.com/vshymanskyy/TinyGSM/blob/master/README.md
- 官方客户端分发文件（对比用）：https://github.com/vshymanskyy/TinyGSM/blob/master/src/TinyGsmClient.h
- 官方 A7672X 客户端（明确写着不支持 GPS）：https://github.com/vshymanskyy/TinyGSM/blob/master/src/TinyGsmClientA7672x.h

**Arduino**
- Arduino CLI 文档首页（当前版本 1.5）：https://arduino.github.io/arduino-cli/
- 快速上手：https://arduino.github.io/arduino-cli/1.5/getting-started/
- `compile` 命令：https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_compile/
- `upload` 命令：https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_upload/
- `monitor` 命令：https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_monitor/
- `lib install` 命令：https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_lib_install/
- Arduino IDE 2.x 说明（证明它后台调用 arduino-cli）：https://github.com/arduino/arduino-ide

**Espressif / arduino-esp32**
- 安装说明（含官方中国镜像地址）：https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
- 故障排查（USB CDC On Boot 说明）：https://docs.espressif.com/projects/arduino-esp32/en/latest/troubleshooting.html
- 版本发布列表（当前稳定 3.3.12）：https://github.com/espressif/arduino-esp32/releases
- 板子选项定义 `boards.txt`（PSRAM/Flash/分区选项名）：https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt
- 官方中国镜像索引：https://jihulab.com/esp-mirror/espressif/arduino-esp32/-/raw/gh-pages/package_esp32_index_cn.json

**PlatformIO**
- 官方平台仓库与板子列表：https://github.com/platformio/platform-espressif32
- `espressif32` v6.12.0 的 `platform.json`（框架版本映射）：https://github.com/platformio/platform-espressif32/blob/v6.12.0/platform.json
- 自定义板子文档：https://docs.platformio.org/en/latest/platforms/creating_board.html
- `platformio.ini` 配置说明：https://docs.platformio.org/en/latest/projectconf.html
- 安装说明（安装脚本 / pip 两种方式）：https://docs.platformio.org/en/latest/core/installation/index.html
- 版本历史（Python 3.12 支持）：https://docs.platformio.org/en/stable/core/history.html
- 设备监视器：https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html
- 中国镜像相关用户 issue（说明"缺少官方镜像"）：https://github.com/platformio/platformio-core/issues/5484 、 https://github.com/platformio/platformio-core/issues/4345

**本机实测（2026-10-05，Windows 11）**
- `winget search --id ArduinoSA.CLI` → 有 `ArduinoSA.CLI` 1.5.1
- `winget search platformio` → 无结果
- 本机 Python 3.12.2 / pip 25.0.1 已安装；arduino-cli 与 platformio 均未安装
