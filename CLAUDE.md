# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目背景

- 目标：宠物猫用的 GPS 定位项圈 —— GPS 定位、4G 无线公网数据上传、手机 App 查看实时定位信息与运动轨迹。
- 允许基于开源项目，也可从零开始。
- 验收标准：可运行的 demo 实物。
- 分工边界：Claude 负责规划、计划、实施；用户负责购买元器件、开发设备等。
- 需求说明见 `spec_gps.md`（中文）。

## 当前状态

- 仓库已托管到 GitHub（私有）：zyqing3/cat-gps-tracker，任务用 GitHub Issues 记录。
- 硬件选型已定：LilyGO T-SIM7670G-S3-Standard（ESP32-S3 + SIM7670G，内置 GPS；16MB 闪存 / 2MB QSPI PSRAM；引脚 RX=5/TX=4/PWRKEY=46），见 `hardware_solution.md`。
- 路线图已完成（GitHub issue #1，已关闭）：固件与通信方案已定 —— Arduino C++ ｜ Arduino CLI ｜ 第一阶段常开模式（4G/GPS 不断电）｜ HTTP POST 到自有 Traccar（traccar.atoo.top:8081，设备 cat-collar-001）。调研与设计文档见 `docs/research/`、`docs/design/`。
- 构建命令已就绪：`bash scripts/compile.sh <草图路径>`（板子参数与宏定义已内置，官方示例编译验证通过）。烧录命令已就绪：`bash scripts/flash.sh`（自动找串口，已烧录成功）。
- ⚠️ **内核必须用 esp32:esp32@2.0.17**（官方固件同款）：3.3.12 构建的固件在本板无法启动（详见 #18 排查记录）。构建脚本已按 Standard 版配置（PSRAM=enabled + `-DLILYGO_SIM7670G_S3_STAN`）。
- ⚠️ 上传走**模组内置 HTTP 客户端**（https_begin/set_url/post + http:// 地址）：TinyGSM TCP 读通道与模组固件 SIM7670G-MNGV 不兼容（HTTP 恒为 0），勿改回 ArduinoHttpClient。
- 实机已跑通（2026-10-07）：SIM/联网/GPRS/GPS 定位/上传全流程 OK，服务器收到每分钟位置。
- 测试命令已就绪：`bash scripts/test_payload.sh`（报文模块本机单测，用 Qt 自带 MinGW g++）、`bash scripts/simulate_report.sh [纬度] [经度]`（模拟上报到真实服务器）。固件骨架与报文模块在 `firmware/gps_collar/`，编译 0 错误、测试全绿、模拟上报已通过服务器验证。
- 手机 App 为第二阶段，不在当前范围。

## 沟通说明

- 用户是初中生：解释、文档和沟通应通俗易懂，避免堆砌专业术语。
- 需求文档和沟通使用中文。

## Agent skills

### Issue tracker

任务和问题记录在 GitHub Issues（用 `gh` 命令操作）。详见 `docs/agents/issue-tracker.md`。

### Domain docs

单上下文布局（single-context）：根目录的 `CONTEXT.md` + `docs/adr/`。详见 `docs/agents/domain.md`。
