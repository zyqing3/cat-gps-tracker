# 缺陷报告：AT+CAGPSTOGPS 手动注入星历无法加速定位

- **报告日期**：2026-10-14
- **报告人**：zyqing3（LilyGO T-SIM7670G-S3-Standard 用户）
- **严重程度**：中（功能不可用，有替代方案但增加功耗与流量成本）

---

## 1. 环境信息

| 项目 | 值 |
|---|---|
| 模块 | SIM7670G（内置 GNSS） |
| 模块固件 | SIM7670G-MNGV |
| 开发板 | LilyGO T-SIM7670G-S3-Standard（ESP32-S3 主控） |
| 通信方式 | 主控通过 AT 口（115200）下发指令 |
| 手册依据 | SIM76XX Series AT Command Manual V2.05，§21.2.23~21.2.26 |
| 测试环境 | 室内窗边，开阔天空视野，正常 4G 网络（AGNSS 服务器下载正常） |
| 测试方法 | 每轮：GNSS 上电 → AGPS 操作 → 轮询 CGNSSINFO 记录定位耗时；CAGPSCHK 记录芯片内星历有效性 |

## 2. 问题描述

**通过 `AT+CAGPSTOGPS` 手动注入到 GPS 芯片的 AGPS 星历（QEPO），无法加速定位。**

- 即使 `AT+CAGPSCHK` 返回星历"可用"（US 位=1，甚至四系统全可用 `1,1,1,1,1,1,1,1`），定位耗时仍为 30~84 秒，与不使用 AGPS 的自主冷启动（32~50 秒）无差别甚至更差。
- 同一时刻、同一份下载数据，走模组内部直注管线（`AT+CAGPSTOLOCAL=0` 模式 + `AT+CAGPS`）则稳定 2~11 秒定位。

**核心矛盾**：CAGPSCHK 的"可用"位与 GNSS 引擎实际采用情况不一致——数据在芯片里，引擎却不使用（或使用无效）。

## 3. 复现步骤

### 3.1 有效路径（对照基线，稳定秒级定位）

```
AT+CGNSSPWR=1                     → +CGNSSPWR: READY!
AT+CAGPSTOLOCAL=0                 → +AGPS:success.（约 9 秒）
AT+CAGPS                          → +AGPS:success.（约 9 秒）
AT+CAGPSCHK                       → 1,1,0,0,1,1,1,1（GPS/GAL/GLO 可用）
→ 定位耗时 2~11 秒（多次重复稳定）
```

### 3.2 缺陷路径（手动注入，不加速）

```
AT+CGNSSPWR=1                     → +CGNSSPWR: READY!
AT+CAGPSTOLOCAL=2                 → +AGPS:success.（下载并存入 flash：simfile-agpsData.rtc）
AT+CAGPS                          → +AGPS:success.
AT+CAGPSTOGPS=2                   → OK / agpssendtoGnss:success
AT+CAGPSCHK
   结果随机：
   - 有时 0,0,0,0,0,0,0,0（连续轮询 10 秒仍为 0）
   - 有时 1,1,0,0,0,0,0,0（仅 GPS 可用）
   - 有时 0,0,1,1,1,1,1,1（BDS/GAL/GLO 可用）
→ 定位耗时 30~84 秒；即使 CHK 显示"全可用"也不加速
```

后续轮次从 flash 注入（免下载）：

```
AT+CAGPSTOGPS=2                   → OK / agpssendtoGnss:success
AT+CAGPSCHK                       → 同上，随机
→ 定位耗时 31~54 秒
```

## 4. 已排查的变体（8 种，全部无效）

| # | 变体 | 结果 |
|---|---|---|
| 1 | 裸注入（flash，无辅助信息） | CHK 稳定仅 GPS 可用；定位 31~48 秒 |
| 2 | 注入 + 辅助位置（上一轮定位，ddmm.mmmmmm 格式，5km 内） | 同上，44~48 秒 |
| 3 | 注入 + 辅助时间（±1 秒精度）+ 辅助位置 | 结果随机：有时 2.8 秒，有时 22 秒 |
| 4 | 注入后等待完整完成回应（9 秒，手册 Max Response Time） | CHK 仍随机 |
| 5 | 注入后轮询 CAGPSCHK 连续 10 秒 | 恒为 0（数据从未"可用"） |
| 6 | 注入后发 CGPSHOT 热启动 | 定位超时 120 秒（更差） |
| 7 | GNSS 上电前注入（手册未禁止） | CHK 显示 GPS 可用，定位 54 秒 |
| 8 | RAM 注入（mode 1）替代 flash 注入 | 同上，无改善 |

每次 GNSS 断电重开（CGNSSPWR=0→1）后 CAGPSCHK 归零，符合"芯片内数据易失"的预期，不是本缺陷。

## 5. 关于辅助时间精度（§21.2.25 注）

手册要求辅助时间误差在 1ms 内才"有效"。但：

- 模块自身只通过 `AT+CCLK?` 以**秒级**精度对外提供时钟（`"yy/mm/dd,hh:mm:ss+zz"`）；
- 外部（主控）没有任何途径获得 ms 级精度的模块时间；
- 若辅助时间精度不足会导致注入"负效果"，那么手动注入路径对外部开发者而言**没有可用输入**——除非模块内部直注管线使用了外部不可见的更高精度时间源（疑为基站驯服晶振 + 模块内部缓存的上次定位时间）。

请官方确认：
1. `AT+CAGPSTOGPS` 的正确使用前提（GNSS 引擎状态要求？辅助时间外部如何获得 1ms 精度？）；
2. `AT+CAGPSCHK` 的 US 位为何与引擎实际使用行为不一致；
3. 该问题是否为 SIM7670G-MNGV 固件缺陷，是否有修复计划或 workaround。

## 6. 影响

- **无法使用 flash 缓存方案**（下载一次缓存、每轮免下载注入），每轮必须重新走内部直注下载：约 18 秒射频时间 + ~80KB 流量。
- 按 15 分钟上报间隔计算：每天约 8MB 流量、约 30 分钟射频唤醒——对小流量物联网卡和电池续航均有实质影响。

## 7. 附件

- 实验日志可按要求提供（含各变体的完整 AT 交互与时间戳）。
- 测试固件为开源项目：github.com/zyqing3/cat-gps-tracker（feature/low-power 分支）。
