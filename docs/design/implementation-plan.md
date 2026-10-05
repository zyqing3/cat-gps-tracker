# 实施方案：动手做 demo 的步骤清单

> 面向读者：初中生。路线图已全部走通（所有关键决策已敲定），这份清单是"开工之后按什么顺序干"。
> 前置条件：开发板到货 + 有一张能上网的 4G SIM 卡（任务卡 #5）。

---

## 步骤总览

| 步骤 | 内容 | 谁做 |
|---|---|---|
| 0 | 开发板到货 + 办 4G SIM 卡 | 👤 你（#5） |
| 1 | 安装 Arduino CLI + ESP32 支持 | 🤖 Claude |
| 2 | 获取 LilyGo 官方库（分支版 TinyGSM） | 🤖 Claude |
| 3 | 按设计文档写固件（gps_collar.ino + config.h） | 🤖 Claude |
| 4 | 编译固件 | 🤖 Claude |
| 5 | 你插 SIM 卡、接天线、插 USB，我烧录 | 🤝 一起 |
| 6 | 串口日志联调（开机 → 联网 → 搜星） | 🤝 一起 |
| 7 | 室外定位 → 上传成功 | 🤝 一起（你拿到室外） |
| 8 | Traccar 网页验收：地图上每分钟更新 | 🤝 一起 |

## 每个步骤的要点

1. **安装工具**：`winget install ArduinoSA.CLI`，再装 ESP32 核心（见 docs/research/arduino-dev-env-comparison.md）。
2. **获取库**：clone LilyGo-Modem-Series 仓库，用 Arduino CLI 的 `--libraries` 指向它的 `lib/`（分支版 TinyGSM + TinyGPSPlus）。
3. **写固件**：按 docs/design/firmware-architecture.md（常开模式）、payload-fields.md（OsmAnd 字段）、traccar-setup-plan.md（服务器三参数）。
4. **编译**：Arduino CLI 编译，有错就改（代理网络问题与 GitHub 无关，编译不依赖网络外网）。
5. **烧录**：板子用 Type-C 数据线连电脑，Arduino CLI 上传；注意板子型号 T-SIM7670G-S3 对应 esp32s3 系列参数（见调研报告）。
6. **联调**：串口监视器 115200 看日志，对照"防坑清单"（固件架构文档 §8）。
7. **室外测试**：GPS 冷启动需要室外开阔处 + 有源天线，等 30 秒到几分钟。
8. **验收**：打开 http://traccar.atoo.top:8081 登录，地图看到 CatCollar 每分钟更新 → 符合 spec_gps.md 验收标准 ✅

## 失败了怎么排查（按顺序）

1. 串口没日志 → 检查数据线（要能传数据的线）、驱动、端口选择
2. 模块不响应 → PWRKEY 引脚接线/脉冲（引脚以 utilities.h 为准：RX=10/TX=11/PWRKEY=18）
3. 联网失败 → SIM 卡有没有流量、APN 对不对
4. 永远搜不到星 → 天线供电两条指令（AT+CGDRT/AT+CGSETV）有没有执行、天线接没接、人在不在室外
5. 上传失败 → 先浏览器打开服务器地址看通不通；卡是否限制访问；防火墙端口 8081
6. 服务器连不上（境外网络问题不适用，服务器在国内）→ 若仍不通，回退巴法云方案（#7 修订结论）

## 依据

- 路线图地图（issue #1）与全部已关闭决策卡
- docs/design/ 下三份设计文档
- docs/research/ 下三份调研报告
