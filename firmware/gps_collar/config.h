/**
 * config.h —— GPS 项圈参数文件
 *
 * 所有可调参数都集中在这一个文件里，改这里就行，不用动主程序。
 * 每项都有注释说明，配合 docs/design/payload-fields.md 一起看。
 */

#pragma once

// ---------- 上报服务器（Traccar） ----------

// 服务器地址：自有 Traccar 服务器（国内，已建好设备 cat-collar-001）
#define TRACCAR_SERVER "traccar.atoo.top"
// 上报端口：8081（OsmAnd 协议；注意服务器是明文 HTTP，没开 https）
#define TRACCAR_PORT 8081
// 设备号：Traccar 里设备的 uniqueId（是字符串，不是数字编号）
#define DEVICE_ID "cat-collar-001"

// ---------- 上报内容 ----------

// 上报间隔（秒）：低功耗版正式用 15 分钟（900 秒）；当前 10 秒纯调试用（加快节奏）。
// 600mAh 电池续航对照（docs/design/low-power-strategy.md 预算表）：
//   60(1 分钟)=0.3 天 ❌  300(5 分钟)=1.3 天 ❌  600(10 分钟)=2.4 天 ❌
//   900(15 分钟)=3.3 天 ✅  1200(20 分钟)=4 天 ✅  1800(30 分钟)=5.2 天 ✅  3600(60 分钟)=7.3 天 ✅
#define REPORT_INTERVAL_S 10

// 电量（%）：第一阶段 USB 供电，固定 100
#define BATTERY_PERCENT 100

// ---------- 4G 网络 ----------

// APN 接入点：普通手机卡一般留空即可（模块自动识别）；
// 物联网卡按运营商要求填，例如：中国移动 cmiot、中国电信 ctlte、中国联通 uninet
#define APN ""

// ---------- GPS ----------

// 等首次定位的最长时间（秒）：室外开阔处约 30 秒到几分钟；
// 超时就跳过本次上报，不阻塞主循环
#define GPS_TIMEOUT_S 120

// AGPS（XTRA）辅助定位：1 = 用 4G 下载辅助星历（每次几 KB 流量），
// 冷启动提速几十秒 → 几秒；失败自动回退普通冷启动。SIM 卡流量紧张时可关。
#define AGPS_ENABLE 1

// PSM（省电模式）：1 = 向网络申请 PSM，获批后睡眠电流可降到微安级（对比 DTR 睡眠毫安级）；
// 是否批准由运营商网络决定。eDRX 同理（延长寻呼周期）。实测不稳定可改 0。
#define PSM_ENABLE 1
#define EDRX_ENABLE 1

// PSM 实验开关：1 = 睡眠期不干预模组（不设 DTR 睡眠），
// 让模组在活跃时间结束后自己进入 PSM 深睡，醒来用 AT 戳醒——用于验证 PSM 是否真的生效。
// 实测结论（2026-10-14）：无论放养 10 秒还是 60 秒，唤醒延迟恒 4ms——模组未进入 PSM。
#define PSM_TEST_MODE 0

// ---------- 调试开关 ----------

// 1 = ESP32 不深睡（用 delay 代替），USB 串口全程在线，方便盯 4G/GPS 模组状态；
//     模组的睡眠/唤醒流程照常执行（DTR 锁高 + CSCLK 睡眠），测的是真实模组低功耗行为。
// 0 = 正式低功耗模式（ESP32 深睡，USB 口在睡眠期间消失）
#define DEBUG_ESP_NO_SLEEP 1
