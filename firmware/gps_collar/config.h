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

// 上报间隔（秒）：每 60 秒上传一次定位
#define REPORT_INTERVAL_S 60
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
