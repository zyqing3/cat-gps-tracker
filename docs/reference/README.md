# 参考资料库（原始文档本地存档）

> 项目调研时引用的原始文档的本地副本，离线也能查。
> 首批下载日期：2026-10-06；用户手动补充：2026-10-07。

## 已下载

| 文件 | 内容 | 来源 |
|---|---|---|
| `A76XX_Series_AT_Command_Manual.pdf` (2.5MB) | A76XX 系列 AT 指令手册（SIM7670X 指令集兼容它，GNSS 指令查这里） | macrogroup.ru 镜像 |
| `SIM7500_SIM7600_GNSS_Application_Note.pdf` (577KB) | SIMCOM GNSS 应用笔记（冷/温/热启动、AGPS、天线供电，同族模块通用） | momoiot.co.kr |
| `esp32-s3_datasheet_en.pdf` (1.1MB) | ESP32-S3 芯片数据手册（深睡电流、RTC 唤醒等） | documentation.espressif.com |
| `traccar-osmand-protocol.html` (12KB) | Traccar OsmAnd 协议页（上报字段定义，报文模块的依据） | traccar.org/osmand |
| `SIM7670_Series_Spec_231205.pdf` (549KB) | **SIM7670 系列规格书**（睡眠/PSM/空闲电流、GNSS 特性等，低功耗预算的依据） | T-Mobile 托管（用户手动下载） |
| `SIM7672X_Series_Hardware_Design_V1.02.pdf` (6.2MB) | 硬件设计手册（SIM7672X 系列，SIM7670X 的姊妹型号，电源/充电/睡眠电路设计可参考） | SIMCom 官方（用户手动下载） |

## 仍可补充（在线可查）

| 文档 | 说明 |
|---|---|
| SIM7670X Series Hardware Design V1.00 | SIM7670X 本系列的硬件设计手册，官方下载列表可下（现已有 SIM7672X 版作参考） |
| SIM7670X 系列中文规格书 | SIMCom 官方文档下载区（需注册）：cn.simcom.com/product/SIM7670X.html |

## 说明

- 官方示例代码在 `vendor/LilyGo-Modem-Series/`（git 忽略，按需重新克隆）
- 各文档的具体数据引用位置见对应设计文档的"依据"一节
