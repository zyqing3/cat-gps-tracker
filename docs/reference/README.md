# 参考资料库（原始文档本地存档）

> 项目调研时引用的原始文档的本地副本，离线也能查。下载日期：2026-10-06。
> 在线查看入口按文件列在下面；文件较大（PDF），只存了与本项目直接相关的。

## 已下载

| 文件 | 内容 | 来源 |
|---|---|---|
| `A76XX_Series_AT_Command_Manual.pdf` (2.5MB) | A76XX 系列 AT 指令手册（SIM7670X 指令集兼容它，GNSS 指令查这里） | macrogroup.ru 镜像（原站 rbtronic.com.ua 文件已移动） |
| `SIM7500_SIM7600_GNSS_Application_Note.pdf` (577KB) | SIMCOM GNSS 应用笔记（冷/温/热启动、AGPS、天线供电，同族模块通用） | momoiot.co.kr |
| `esp32-s3_datasheet_en.pdf` (1.1MB) | ESP32-S3 芯片数据手册（深睡电流、RTC 唤醒等） | documentation.espressif.com |
| `traccar-osmand-protocol.html` (12KB) | Traccar OsmAnd 协议页（上报字段定义，报文模块的依据） | traccar.org/osmand |

## 未能下载（在线可查）

| 文档 | 说明 |
|---|---|
| SIM7670 系列规格书（T-Mobile 托管 PDF） | 下载被拒（403，需浏览器会话）。**关键数据已摘录**进 docs/design/low-power-strategy.md：睡眠 800µA / PSM 10µA / 空闲 4.2mA。在线地址见该设计文档第 10 节 |
| SIM7670X 硬件设计手册 | 官方 cn.simcom.com 需要注册下载；sekorm.com 有在线查看版 |
| SIM7670X 系列中文规格书 | SIMCom 官方文档下载区（需注册） |

## 说明

- 官方示例代码在 `vendor/LilyGo-Modem-Series/`（git 忽略，按 README 重新克隆）
- 各文档的具体数据引用位置见对应设计文档的"依据"一节
