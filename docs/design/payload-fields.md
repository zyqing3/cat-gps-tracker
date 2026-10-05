# 上传数据包字段定义

> 面向读者：初中生。定义"项圈每分钟发给服务器的定位数据长什么样"。
> 依据：Traccar OsmAnd 协议 + 路线图 #7/#9 已定方案。

---

## 1. 结论：每分钟发这样一条数据

```
http://traccar.atoo.top:8081/?deviceid=cat-collar-001&lat=31.2304000&lon=121.4737000&timestamp=2026-10-05T08:00:00Z&speed=0.00&altitude=4.20&batt=100
```

## 2. 字段清单（按 LilyGo 官方示例的格式）

| 字段 | 必填？ | 格式 | 示例 | 说明 |
|---|---|---|---|---|
| `deviceid` | ✅ | 字符串 | `cat-collar-001` | 设备唯一号（Traccar 里设备的 uniqueId），固定不变 |
| `lat` | ✅ | 十进制度，7 位小数 | `31.2304000` | 纬度，GPS 直接给出，不换算 |
| `lon` | ✅ | 十进制度，7 位小数 | `121.4737000` | 经度，同上 |
| `timestamp` | 建议带 | UTC 时间 ISO 格式 | `2026-10-05T08:00:00Z` | GPS 给的时间是 UTC（北京时间 = UTC+8 小时）；Traccar 网页按账号时区显示，不用自己换算 |
| `speed` | 可选 | 节（1 节 ≈ 1.85 km/h） | `0.00` | 速度，保留 2 位小数 |
| `altitude` | 可选 | 米 | `4.20` | 海拔，保留 2 位小数 |
| `batt` | 可选 | 0-100 整数 | `100` | 电量百分比；第一阶段 USB 供电，固定填 100 |

> 为什么第一阶段简单：USB 供电 → batt 固定 100；猫速度慢 → speed 精度不重要，照传即可。字段与 LilyGo 官方 Traccar 示例的 `post_format` 完全一致，代码直接沿用，不用自己发明格式。

## 3. 各字段从哪来

| 字段 | 来源 |
|---|---|
| `deviceid` | config.h 里的 `DEVICE_ID`（= cat-collar-001） |
| `lat` / `lon` | `modem.getGPS()` 返回值（模块内置 GNSS 解析） |
| `timestamp` | GPS 报文里的 UTC 时间（LilyGo 示例已实现，取不到就用服务器收到的时间） |
| `speed` / `altitude` | `getGPS()` 同批返回 |
| `batt` | 第一阶段写死 100；第二阶段电池版再读 ADC |

## 4. 与两侧架构的对应

- **固件侧**（docs/design/firmware-architecture.md §5）：`uploadLocation()` 按上表组装查询串 → HTTP POST。
- **服务器侧**（docs/design/traccar-setup-plan.md §3）：Traccar 按 OsmAnd 协议解析；`deviceid` 必须与设备 uniqueId 完全一致，否则数据会被丢弃。

## 5. 验证方式

1. 板子上电、联网、定位成功后，串口日志会打印完整上报串（对照上表检查格式）。
2. 打开 traccar.atoo.top:8081 地图，CatCollar 的位置点出现且每分钟更新。
3. 地图上的时间/速度/海拔与实际情况对得上（时间注意 UTC 显示差 8 小时是正常的）。

## 6. 依据

- Traccar OsmAnd 协议文档：<https://www.traccar.org/osmand/>
- LilyGo 官方 Traccar 示例的 post_format：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/Traccar/Traccar.ino>
- docs/design/traccar-setup-plan.md、docs/design/firmware-architecture.md
