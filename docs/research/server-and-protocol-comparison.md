# 宠物猫 GPS 项圈 —— 服务器方案与上传协议调研

> 面向读者：初中生。所有技术名词第一次出现时都会解释。
> 调研日期：2026-10-05。**所有关键结论后面都跟了官方网站链接**，凡是查不到的会明确写「未核实」或「推算」。

---

## 0. 先给结论（懒人版）

| 问题 | 结论 |
|---|---|
| 用什么协议上传？ | **MQTT（长连接）最省流量、最省电**；但 LilyGo 官方示例固件走的是 **HTTP(S) POST 到 Traccar**，那条路「不用自己写协议」，最适合 demo |
| 服务器选哪个？ | **首选：Traccar**（可先用它官方免费 demo 服务器，0 元；满意后再花约 38 元/月租香港服务器自己搭） |
| 备选？ | 备选 1：巴法云（邮箱注册、免费、国内节点快）+ 自写网页；备选 2：EMQX Cloud 中国站 Serverless（免费额度很大，但要实名） |
| 最大的坑？ | ① 国内的「物联网卡」可能被运营商限制、**访问不了境外服务器**；② 腾讯云**未满 18 岁不能实名认证**，买不了；③ 国内服务器上放网页**必须 ICP 备案**，未成年人基本办不了 → **所以要么用境外/香港服务器，要么用国内平台自带的网页** |

---

## 1. 你的板子：能干什么（先摸清底细）

你的板子是 LilyGo **T-SIM7670G-S3**，里面装的是 **SIMCom SIM7670G** 模块（4G Cat-1 + 内置 GPS）。

| 项目 | 说明 |
|---|---|
| 开发方式 | 本路线图已定 **Arduino CLI**（可全自动编译/烧录）；LilyGo 官方文档与 CI 走 PlatformIO，两者都可以 |
| 用哪个库 | **TinyGSM**（注意：必须用 LilyGo 的**分支版**，原版编译不过）+ **TinyGPSPlus**，仓库 `LilyGo-Modem-Series` |
| 代码里的型号宏 | `#define TINY_GSM_MODEM_SIM7672` |
| 引脚 | 模块串口 **RX=10、TX=11、115200**；开机键 **PWRKEY=18**（⚠️ 官方 wiki 快速上手页写的 RX=4/TX=5 是另一块板子的，是错的；以仓库 `utilities.h` 为准，详见 docs/research/lilygo-t-sim7670g-s3-gnss-arduino.md） |
| 联网 | `modem.gprsConnect("你的APN","","")` |
| 定位 | `modem.enableGPS(4, 1)` / `modem.getGPS(...)`（具体参数以 GPS 调研报告为准） |

来源：<https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html>
源码仓库（MIT 协议）：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series>

### 一个超级重要的发现

LilyGo 官方仓库里**已经有一个现成的 Traccar 示例程序**，507 行，2025-03-25 写的，MIT 协议，而且 README 里写明「用 SIM7670G 实测过」。

它干的事正好就是你想要的功能：**定时定位 → 通过 HTTPS POST 把经纬度发给 Traccar 服务器 → 然后进入省电休眠**。

核心只有这几行：

```cpp
const char *client_id   = "your tarrcar device id";
const char *request_url = "https://your_tarrcar_server.com";
const char *post_format = "deviceid=%s&lat=%.7f&lon=%.7f&speed=%.2f&altitude=%.2f&batt=%u";
...
modem.https_begin();
modem.https_set_url(request_url);
int httpCode = modem.https_post(post_buffer);
modem.https_end();
```

来源：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/Traccar/Traccar.ino>

**这意味着：固件这一侧你几乎不用写代码**——改个设备 ID、改成你自己的服务器地址、调一下上传间隔（示例里是 `REPORT_LOCATION_RATE_SECOND = 20`，20 秒一次，你要的是一分钟一次就改成 60）就行。

### 厂商官方给的耗电实测数据

同一份示例的 README（SIM7670G 实测）：

| 状态 | 电流 |
|---|---|
| 开机瞬间 | 200mA+ |
| 每个上报周期的峰值 | 120mA+ |
| 上报完成后的平均（浅休眠） | **2~3mA** |
| 定位耗时 | 几秒（模块不关机，所以很快） |

来源：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/Traccar/README.MD>

---

## 2. 协议对比：MQTT 还是 HTTP POST？

### 2.1 先搞懂这两个词

- **HTTP POST**：像「寄快递」。每次要寄东西，都要重新打电话叫快递员、填单子、寄出去、对方签收、挂电话。下次再寄，**全部重来一遍**。
- **MQTT**：像「打电话聊天」。先拨通一次，然后一直保持通话，每次只要说一句话就行。中间不说话时，每隔几分钟「喂」一声确认电话没断。

### 2.2 在模块上实现有多难？（基于官方 AT 指令手册）

先说一个关键点：**SIM7670G 模块自己就会说 MQTT 和 HTTP，你的 ESP32 只需要用「AT 指令」下命令就行**，不需要自己拼数据包。

官方手册《SIM767XX Series_AT Command Manual V1.02》（380 页）里有专门的章节：
- 第 14 章 HTTP(S)：`AT+HTTPINIT` / `AT+HTTPTERM` / `AT+HTTPPARA` / `AT+HTTPACTION` / `AT+HTTPHEAD` / `AT+HTTPREAD` / `AT+HTTPDATA` / `AT+HTTPPOSTFILE` / `AT+HTTPREADFILE`
- 第 16 章 MQTT(S)：`AT+CMQTTSTART` / `AT+CMQTTSTOP` / `AT+CMQTTACCQ` / `AT+CMQTTREL` / `AT+CMQTTSSLCFG` / `AT+CMQTTWILLTOPIC` / `AT+CMQTTWILLMSG` / `AT+CMQTTCONNECT` / `AT+CMQTTDISC` / `AT+CMQTTTOPIC` / `AT+CMQTTPAYLOAD` / `AT+CMQTTPUB` / `AT+CMQTTSUB` / `AT+CMQTTUNSUB` / `AT+CMQTTCFG`

手册 PDF：<https://download.mikroe.com/documents/datasheets/SIM767xx_Series_AT_command_manual_v1.02.pdf>

**指令步骤数对比：**

| | MQTT | HTTP(S) POST |
|---|---|---|
| 首次建立 | `AT+CMQTTSTART` → `AT+CMQTTACCQ` → `AT+CMQTTCONNECT`（**3 条**）<br>（若要加密再加 `AT+CCERTDOWN` 下证书 + `AT+CMQTTSSLCFG`） | `AT+HTTPINIT` → `AT+HTTPPARA="URL",...`（**2 条**） |
| 每次上报 | `AT+CMQTTTOPIC` → `AT+CMQTTPAYLOAD` → `AT+CMQTTPUB`（**3 条**） | `AT+HTTPPARA="CONTENT",...` → `AT+HTTPDATA` → `AT+HTTPACTION` → `AT+HTTPREAD`（**约 4 条**） |
| 结束时 | `AT+CMQTTDISC` → `AT+CMQTTREL` → `AT+CMQTTSTOP`（3 条） | `AT+HTTPTERM`（1 条） |

两边其实都只有几条指令，**难度差别不大**。真正的差别是**连接要不要重来**。

来源（TinyGSM 分支版把这两套指令都封装好了，可以直接抄）：
- MQTT 封装源码：`lib/TinyGSM/src/TinyGsmMqttA76xx.h`
- HTTP 封装源码：`lib/TinyGSM/src/TinyGsmHttpsComm.h`
- 仓库：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/main/lib/TinyGSM/src>

**谁负责重连？** 两边都不用你管 TCP 层的重连——重连是**模块内部**做的。但如果是 MQTT 长连接断了，模块会主动吐一条通知 `+CMQTTCONNLOST:<client_index>,<cause>` 告诉你原因（`1`=对端关闭，`2`=连接被重置，`3`=网络断开），你在代码里收到这条就重新连接即可。HTTP 每次都是新连接，失败就是那一次失败，下次重来。

来源：SIM767XX AT 手册第 16.4 节（URC）。

### 2.3 流量消耗（**以下数字是推算**，依据是协议规范的字节数，不是实测）

先说明推算依据（全部是一手规范）：
- IPv4 头最小 **20 字节**：RFC 791 <https://www.rfc-editor.org/rfc/rfc791>
- TCP 头最小 **20 字节**：RFC 9293 <https://www.rfc-editor.org/rfc/rfc9293>
- MQTT 的 `PINGREQ`（心跳）**没有可变头、没有载荷**，总共 2 字节：OASIS MQTT 3.1.1 规范 §3.12 <https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html>

假设：每分钟 1 条，载荷约 90 字节（大致对应 LilyGo 示例那种 `deviceid=..&lat=..&lon=..`），上行+下行都算流量。

| 方式 | 每分钟 | 每天 | **每月** |
|---|---|---|---|
| **MQTT（明文 1883）** | ~191 字节 | ~0.27 MB | **约 8.3 MB** |
| **MQTT（加密 8883）** | ~250 字节 | ~0.35 MB | **约 10.8 MB** |
| **HTTP POST（明文）**<br>每分钟新建一次连接 | ~780 字节 | ~1.1 MB | **约 34 MB** |
| **HTTPS POST**<br>每分钟新建一次连接 | ~5.4 KB | ~7.6 MB | **约 230 MB** |

**为什么 HTTPS 会暴涨到 230MB？** 因为每次都要重新做一次 TLS 握手，而握手里服务器要把**数字证书**发给你，一个证书链大约 3.5~4.5 KB。传的数据只有 90 字节，证书却有 4000 字节——**贵的不是数据，是每次重新认识一遍**。

> **推算说明**：以上是纯协议字节数计算，真实流量还会多一点点（4G 底层还有额外封装），而且不同服务器证书大小不同。**建议实测验证**。

**结论**：MQTT 比 HTTP POST 省 **约 4 倍**流量；比 HTTPS POST 省 **约 28 倍**。

**参考对照**：中国移动 OneNET 官方流量套餐「30M/月」约 3.6 元/月、「1G/月」约 9 元/月——也就是说 **8 MB/月的 MQTT 方案，一个月连最便宜的 5M 套餐都超不了多少**。
（来源：<https://open.iot.10086.cn/cmiot/accessPackage> —— 注意该页面是 JS 动态渲染的，没能直接抓到正文，数字来自搜索摘要，**建议以页面实际显示为准**）

### 2.4 耗电对比

官方实测数据（LilyGo Traccar 示例 README，见上文表格）：休眠 2~3mA、峰值 120mA+。

**基于协议开销的合理推算（非实测）**：
- **MQTT 长连接**：打电话打通一次，之后一直保持。手机网络只要「每 5 分钟喂一声」就不会把连接踢掉，射频频段大部分时间处于低功耗状态。
- **HTTP 每分钟新建连接**：每分钟都要重新握手。在 4G 网络里，每次新建连接会把射频拉回高功耗状态一段时间才回落。**每分钟折腾一次，比每 5 分钟折腾一次要费电。**

> 具体省多少电，**没有找到官方数据，未核实**。建议实物做出来后用 USB 电流表实测两种方式的平均值。
>
> **另一个未核实的点**：MQTT 连接在 ESP32 浅休眠期间能否保持不断（模块不关机理论上可以，但要实测确认）。

### 2.5 断网、弱网、NAT 超时

| 问题 | MQTT | HTTP POST |
|---|---|---|
| 弱网丢包 | 长连接会断，模块报 `+CMQTTCONNLOST`，需要重连 | 那一次就失败，下次重新来 |
| NAT 超时 | **风险点**：运营商的 NAT 表会把「太久没说话」的连接悄悄删掉。所以心跳（Keep Alive）必须设得比 NAT 超时短。MQTT 规范规定：服务端在 **1.5 倍 Keep Alive** 时间内没收到任何包就断开连接。手册里 `AT+CMQTTCONNECT` 的 keepalive 可设 **1~64800 秒** | 不存在这个问题（每次都是新连接） |
| 断网恢复 | 需要在代码里检测并重连 | 天然免疫 |

来源：OASIS MQTT 3.1.1 §3.1.2.10；SIM767XX AT 手册 §16.2.8

> **建议**：keepalive 设 **300 秒（5 分钟）** 比较稳妥。NAT 具体多久超时，**不同运营商不同，未核实，建议实测**。

### 2.6 代码量和调试难度

| | MQTT | HTTP POST |
|---|---|---|
| 用 LilyGo 官方示例 | `MqttsBuiltlnEMQX.ino` / `MqttsBuiltlnHivemq.ino` / `MqttsBuiltlnNoSSL.ino` | **`Traccar.ino`（现成、完整、含省电管理）** |
| 服务端要写代码吗 | **要**（要自己写一个网页去订阅和画地图） | **不用**（Traccar 自带网页地图） |
| 调试 | 可以用 MQTTX 之类的工具在电脑上先试通 | 浏览器直接访问一下 URL 就知道通不通 |

**对初中生的实际差别**：HTTP + Traccar 这条路上，**你和服务端都不用写代码**，只需要改配置。MQTT 那条路上，服务端那个网页得有人给你写。

### 2.7 协议结论

1. **纯技术最优是 MQTT**：省流量 4 倍、省电、适合长期运行。
2. **demo 最省事是 HTTP POST 到 Traccar**：LilyGo 官方固件示例 + Traccar 自带地图界面，两块拼图都是现成的。
3. **建议路线**：**先用 HTTP POST + Traccar 把 demo 跑通**（快速拿到可运行实物，符合验收标准），第二阶段要省电/省流量时再切 MQTT。切换成本很低，因为 LilyGo 两种示例都有。

---

## 3. 方案 A：免费服务器方案

### 3.1 免费 MQTT 服务器

| 平台 | 免费额度 | 注册要求 | 服务器在哪 | 官方来源 |
|---|---|---|---|---|
| **EMQX 公共 broker** | 完全免费，不用注册 | **无** | 全球多区域 | <https://www.emqx.com/en/mqtt/public-mqtt5-broker> |
| **EMQX Cloud Serverless（国际站）** | **100 万会话分钟/月 + 1 GB 流量/月 + 100 万条规则动作/月**，永久免费 | 邮箱；**不需要信用卡** | 新加坡（GCP） | <https://www.emqx.com/en/cloud/serverless-mqtt> |
| **EMQX Cloud 中国站 Serverless** | 同上免费额度 | **需手机号 + 实名认证** | **阿里云杭州/北京（国内！）** | <https://docs.emqx.com/zh/cloud/latest/deployments/regions.html> |
| **巴法云 bemfa.com** | 免费，但**具体额度和限流规则官方文档没有公布**（未核实） | **只需邮箱，无需手机号/实名** | 国内 | <https://cloud.bemfa.com/docs/src/mqtt.html> |
| **HiveMQ Cloud** | ⚠️ **原「100 连接永久免费」档位在现行定价页上已看不到**，目前免费路径是自托管的 Lab 版（25 连接、25 条/秒、30 天需手动续期）；Cloud 版是 Starter 档「$0.34/小时 + $0.80/百万条」，15 天试用。**免费云档现状未能核实**，请在注册页确认 | 邮箱 | **欧洲（法兰克福）/ 东京** | <https://www.hivemq.com/pricing/> |
| **阿里云 IoT** | ⚠️ **公共实例已于 2025 年 2 月 1 日起停止新购** | — | 国内 | <https://help.aliyun.com/zh/iot/product-overview/notice-on-discontinuation-of-public-instances> |
| **腾讯云 IoT Explorer** | ⚠️ **新用户需先购买 1000 个激活码**才能用 | — | 国内 | <https://cloud.tencent.com/document/product/1081/128862> |
| **中国移动 OneNET** | 平台设备接入免费；但物联网卡/模组是「激活码」制，需要走商务渠道 | 实名 | 国内 | <https://open.iot.10086.cn/cmiot/accessPackage> |

**EMQX Serverless 免费额度够用吗？** 完全够。1 台设备 24 小时在线一个月 = 43,200 会话分钟，免费额度 100 万分钟 ≈ 可以养 **23 台**常年在线的设备。流量方面推算才 ~10 MB/月，免费给 1 GB。来源：<https://www.emqx.com/en/cloud/serverless-mqtt>

**巴法云的连接信息**（官方文档）：服务器 `bemfa.com`，TLS 端口 `9503`，加密 WebSocket 端口 `9504`（路径 `/wss`），支持标准 MQTT 3.1.1、QoS 0/1（**不支持 QoS 2，用了会被强制下线**）、支持保留消息。用「用户私钥」当客户端 ID，用户名密码可空。来源：<https://cloud.bemfa.com/docs/src/mqtt.html>

**EMQX 公共 broker 的端口**：TCP `1883`、TLS `8883`、WebSocket `8083`（路径 `/mqtt`）、WSS `8084`（路径 `/mqtt`）、QUIC `14567`。
⚠️ **官方明确警告：这是公共 broker，所有人的消息互相可见，不要发敏感数据。** 来源：<https://www.emqx.com/en/mqtt/public-mqtt5-broker>

#### ⚠️ 中国大陆 4G 访问海外 broker 的问题（**这是最大的坑**）

- HiveMQ Cloud 的免费集群在**欧洲（法兰克福）**——LilyGo 官方示例里的地址就是 `xxxxx.s2.eu.hivemq.cloud`，证实了这一点。来源：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/MqttsBuiltlnHivemq/MqttsBuiltlnHivemq.ino>
- EMQX Cloud 国际站 Serverless 在新加坡，**没有中国大陆节点**；**中国站有阿里云杭州/北京节点**（但要实名）。来源：<https://docs.emqx.com/zh/cloud/latest/deployments/regions.html>
- **更麻烦的是卡的问题**：华为云官方文档明确说明「物联网定向流量」的机制是——**只允许终端访问预先设置好的白名单（域名或 IP），不允许访问任何其他网络和服务**，支持 http / https / tcp / udp / mqtt 等协议。也就是说，**如果你的物联网卡开了定向功能，境外服务器不在白名单里就直接访问不了**。来源：<https://support.huaweicloud.com/usermanual-ocgsl/oceanlink_04_0059.html>
- > 「境外 IP 能不能加进定向白名单」**没有找到运营商官方明文规定，未核实，建议直接问卖卡的运营商或实测**。

**建议**：**做 demo 时优先用国内节点**（巴法云 / EMQX 中国站），或者**先做一次「模块 ping 一下海外 IP」的实验**，确认你的卡能出去再说。

### 3.2 免费地图看板（能在地图上看位置）

#### Traccar（重点推荐）

| 项目 | 情况 |
|---|---|
| 是什么 | 开源 GPS 追踪服务器，自带网页后台 |
| 上报协议 | **支持 HTTP GET/POST 上报**（OsmAnd 协议）。**不支持**「设备发到 MQTT broker、Traccar 去订阅」这种模式 |
| 网页地图 | **自带**，默认用 OpenStreetMap 瓦片，不用申请任何 key |
| 中国地图（高德/腾讯图层） | Traccar 网页里内置了这些图层选项，但需要你自己填 key（源码 `traccar-web/src/map/core/useMapStyles.js` 里这几个图层的 `available` 是 `false`） |
| 自托管资源 | 很低，官方提供 **Docker 镜像**，一行命令就能跑：`docker run --publish 80:8082 traccar/traccar:latest`（网页端口 8082，设备上报端口 5000-5300） |
| 许可证 | Apache-2.0 |
| **官方免费 demo 服务器** | **有！而且免费注册自己的账号** |

Traccar 官方 OsmAnd 协议文档（**必须的参数只有 `deviceid` 和经纬度**，可选时间戳/速度/方向/高度/精度/电量）：
<https://www.traccar.org/osmand/>

官方示例 URL：

```
http://demo.traccar.org:5055/?deviceid=12345&lat=48.8566&lon=2.3522
  &timestamp=2021-01-01T00:00:00Z&speed=15&bearing=270&altitude=35&accuracy=10&hdop=0.8&batt=75
```

**官方 demo 服务器清单**（全部免费注册，但都在海外）：

| 服务器 | 国家 | IP | 网址 |
|---|---|---|---|
| Demo 1 | 英国 | 46.101.24.212 | demo.traccar.org |
| Demo 2 | 加拿大 | 51.79.43.115 | demo2.traccar.org |
| Demo 3 | 德国 | 188.245.151.94 | demo3.traccar.org |
| Demo 4 | 美国 | 104.237.9.196 | demo4.traccar.org |

来源：<https://www.traccar.org/demo/> 和 <https://www.traccar.org/docker/>

> ⚠️ **重要提醒**：Traccar **没有一个通用的「设备通过 MQTT 上报」入口**。查了它的源码，只有厂商专用的 MQTT 解码器（`BaseMqttProtocolDecoder`）和「把位置转发出去」的功能（`PositionForwarderMqtt`），**没有** `MqttProtocolDecoder`。所以走 Traccar 就必须用 HTTP POST。
> 来源：<https://github.com/traccar/traccar/tree/master/src/main/java/org/traccar/protocol>

#### 其他免费看板平台

| 平台 | 有地图组件吗 | 免费额度 | 备注 |
|---|---|---|---|
| **ThingSpeak**（MathWorks） | 官方文档确认有地图可视化 | 免费版有消息数/频率限制 | 未核实具体数字 |
| **Ubidots** | 有地图组件 | 有免费/教育版 | 未核实 |
| **Datacake** | 有地图组件 | 有免费版 | 未核实 |
| **Blynk** | 有地图组件 | 有免费版 | 需要手机 App |
| **高德地图 JS API** | 是地图本身，不是看板 | 个人开发者可申请，**必须实名认证**，2021-12-02 后申请的 key 还要配「安全密钥」 | <https://lbs.amap.com/api/javascript-api-v2/guide/abc/prepare> |
| **百度地图 JS API** | 同上 | 个人可申请，需实名 | 未核实具体额度 |
| **Leaflet + OpenStreetMap** | 完全免费，不需要任何 key | Leaflet 是 BSD-2 开源 | 注意 OSM 官方瓦片有使用政策限制，详见下 |

> **未核实**：ThingSpeak / Ubidots / Datacake / Blynk 的具体免费额度数字，本次调研**没有抓到官方页面确认**，请以各官网为准。

#### Leaflet + OpenStreetMap 自己写网页

**完全可行，而且不需要任何账号**：
- Leaflet 是开源库（BSD-2），免费，不需要 key
- OpenStreetMap 提供免费瓦片，但**有使用政策**：必须保留署名，禁止大量批量下载
- 瓦片政策官方页面：<https://operations.osmfoundation.org/policies/tiles/>

**一个很妙的做法**：写成一个**本地 HTML 文件**，直接用浏览器打开就能看地图（用浏览器通过 WebSocket 订阅 MQTT 消息）。这样**不用买服务器、不用买域名、不用备案**。

### 3.3 免费方案的「坑」：注册需要什么

**这一条直接决定了「Claude 能不能无人值守帮你搭好」。答案很残酷：所有平台的账号注册 Claude 都做不了**（因为需要收邮件验证码或短信验证码，Claude 收不到）。

| 平台 | 注册要什么 | Claude 能自动做吗 |
|---|---|---|
| Traccar 官方 demo 服务器 | 网页上点 Register，填邮箱/用户名/密码（**是否需要邮箱验证未核实**） | ❌ 注册要你自己来；注册后配置可以帮你做 |
| EMQX 公共 broker | **不用注册** | ✅ 可以直接用 |
| 巴法云 | **只要邮箱**，不用手机号/实名 | ❌ 注册要你来 |
| EMQX Cloud 中国站 | **手机号 + 实名认证** | ❌ 只能你来 |
| HiveMQ Cloud | 邮箱（免费档现状未核实） | ❌ |
| 高德 / 百度地图 key | **实名认证** | ❌ 只能你来 |

---

## 4. 方案 B：云服务器方案

### 4.1 价格

| 厂商 | 方案 | 价格 | 来源 |
|---|---|---|---|
| **腾讯云** | 学生机 2核2G3M | **25 元/月**（1 年 300 元） | <https://cloud.tencent.com/act/campus> |
| **腾讯云** | 学生机 2核4G5M | **40 元/月**（1 年 480 元） | 同上 |
| **腾讯云** | 轻量应用服务器最低配 | 2核2G3M 起 | <https://cloud.tencent.com/product/lighthouse> |
| **阿里云** | 学生（300 元无门槛券，可换 ECS 免费约 6 个月 / 2核2G） | 现名「开发者成长计划」 | <https://developer.aliyun.com/plan/student> |
| **阿里云** | 轻量应用服务器最低配 | 2核2G 起 | <https://www.aliyun.com/product/swas> |

> 以上价格来自官方活动页，**活动价随时变，下单前请以页面实际显示为准**。

### 4.2 买之前必须过的关（**这条最关键**）

| 门槛 | 腾讯云 | 阿里云 |
|---|---|---|
| 实名认证 | **必须**。而且官方网站明确写：**暂不支持 18 周岁以下的用户进行实名认证** | **必须**。**14~18 周岁**可以办，但**需要法定监护人陪同** |
| 中国大陆手机号 | 需要 | 需要 |
| 支付方式 | 微信/支付宝/银行卡 | 支付宝/微信/银行卡 |
| 学生认证 | 需要学生身份证明 | 需要学信网等学籍材料 |

来源：
- 腾讯云实名认证说明：<https://cloud.tencent.com/document/product/378/3629>
- 阿里云个人实名认证说明：<https://help.aliyun.com/zh/account/user/individual-real-name-authentication>

> ⚠️ **结论：对初中生来说，腾讯云基本走不通（明文写了不支持 18 岁以下实名）。阿里云可以，但需要家长陪同做实名认证。**

### 4.3 ICP 备案问题（**网页能不能给人看**）

这是中国特有的规定。结论：

| 问题 | 结论 |
|---|---|
| 国内服务器上放网页，必须备案吗？ | **必须**。官方说明：未备案的域名不能在国内服务器上提供 Web 服务，80/443 端口会被拦截 |
| 备案是备「域名」还是备「服务器」？ | **备案是绑定「域名 + 服务器」的**，**不能只备案一个 IP** |
| 那我只用 `http://1.2.3.4:8080`（IP + 非标准端口）呢？ | 官方 FAQ 的说法是：**没有备案的域名不能在大陆服务器上提供 Web 服务**。用 IP 直接访问网页这件事，一方面绕不过监管要求，另一方面**「只备案 IP」这个流程本身就不存在**。这条路**不靠谱，不建议** |
| 未成年人能备案吗？ | 备案需要身份证 + 人脸核验，**未成年人（尤其无身份证的）基本办不下来** |
| 那怎么办？ | **① 用香港/境外服务器**（不需要 ICP 备案）；**② 用平台自带的网页**（比如 Traccar 官网的 demo 服务器，或者国内 IoT 平台自己的控制台） |

**推荐结论**：**如果你要自己搭服务器 + 自己看网页，就买香港等境外节点，避开备案这道墙。**

---

## 5. 推荐组合

### 🥇 首选：Traccar（HTTP POST）+ LilyGo 官方 Traccar 示例固件

**分两步走，先用 0 元把 demo 跑起来：**

**第一步（0 元，验证用）**：设备 → `https://demo.traccar.org:5055`（Traccar 官方免费 demo 服务器）
**第二步（约 38 元/月，长期用）**：设备 → 自己的香港轻量服务器上跑的 Traccar

**为什么选它：**
1. **固件不用写**——LilyGo 官方 507 行 Traccar 示例现成，还带省电管理，README 里说 SIM7670G 实测过
2. **服务端不用写**——Traccar 自带网页地图、轨迹回放，默认 OSM 瓦片不用申请 key
3. **协议有官方文档**——Traccar 的 OsmAnd 协议文档写得很清楚
4. **验收标准最容易达成**——「服务器上可以看到每分钟刷新的定位信息」这句话，Traccar 打开网页就是
5. **Claude 能自动部署**——Traccar 有官方 Docker 镜像，一条命令跑起来

**成本估算：**

| 项目 | 费用/年 | 说明 |
|---|---|---|
| 服务器（香港轻量，约 38元/月） | **≈ 456 元** | 可选，第一步用免费 demo 就是 0 元 |
| 4G 流量卡 | **约 40~120 元** | 按每月 34 MB（HTTP 方案）算，最便宜的流量包就够；用 MQTT 更省 |
| 云服务 | 0 元 | |
| **合计** | **约 40 元（纯免费方案）~ 600 元** | |

**哪些必须你来做、哪些 Claude 能做：**

| 步骤 | 谁做 |
|---|---|
| 在 demo.traccar.org 注册账号（填邮箱） | ❌ **你**（Claude 收不到验证邮件） |
| 注册完把账号密码给 Claude | 你 |
| 在 Traccar 里创建设备、拿到 deviceid | ✅ Claude（用你的账号登录后） |
| 把 LilyGo 官方 Traccar 示例改成你的服务器地址 + deviceid + 60 秒间隔 | ✅ Claude |
| Arduino CLI 编译、烧录固件 | ✅ Claude（板子插在这台电脑上就行） |
| 准备一张能上网的 SIM 卡（**推荐普通手机副卡，别用物联网卡**） | ❌ **你**（要实名） |
| （可选）买香港服务器 | ❌ **你**（要实名 + 支付） |
| （可选）在服务器上装 Docker、跑 Traccar、开防火墙 | ✅ **Claude 全自动**（只要给你 SSH 密码） |

---

### 🥈 备选 1：巴法云 + 自写网页（国内、免费、邮箱注册）

**适合**：想完全零成本，或者发现境外服务器连不上。

- 设备 → MQTT（TLS `bemfa.com:9503`）→ 巴法云 → 浏览器通过加密 WebSocket（`9504` 路径 `/wss`）订阅 → **Leaflet + OSM 的地图网页**
- 优点：**国内节点，速度最快**；**注册只要邮箱，不用实名**；免费
- 缺点：**巴法云没有地图功能**，地图网页要自己写（Claude 可以写）；免费额度官方没公布

**成本**：服务器 0 元 + 流量卡约 40 元/年（MQTT 方案约 10 MB/月，1G 流量包能用很久）

**必须人做**：注册巴法云账号（邮箱）/ 买卡。**其余 Claude 全自动。**

来源：<https://cloud.bemfa.com/docs/src/mqtt.html>

---

### 🥉 备选 2：EMQX Cloud 中国站 Serverless + 自写网页

**适合**：想要正规平台、免费额度大（100 万会话分钟 + 1GB/月）、国内节点的。

- 设备 → MQTTS（8883，LilyGo 有现成的 `MqttsBuiltlnEMQX.ino` 示例，连证书都给你备好了）→ EMQX 中国站（阿里云杭州/北京）→ 浏览器 WSS 订阅 → Leaflet 地图
- 优点：**免费额度非常大方**；**LilyGo 官方示例直接支持**；国内节点
- 缺点：**注册需要手机号 + 实名认证**；服务端网页要自己写

**成本**：0 元（免费额度内）+ 流量卡

来源：<https://docs.emqx.com/zh/cloud/latest/deployments/regions.html>

---

### ⚡ 临时验证用：EMQX 公共 broker（0 注册、0 成本）

如果只是想**先确认板子能不能连上网、能不能发数据**，直接用 `broker.emqx.io`，**什么账号都不用注册**。但记住：**公共 broker 上所有人的消息都互相可见，只能用来测试，不能放真实位置。**

来源：<https://www.emqx.com/en/mqtt/public-mqtt5-broker>

---

## 6. 所有方案对比表

| | Traccar 自建 | Traccar 官方 demo | 巴法云 + 自写网页 | EMQX 中国站 + 自写网页 | EMQX 公共 broker |
|---|---|---|---|---|---|
| **费用/年** | 约 456 元 | **0 元** | **0 元** | **0 元** | **0 元** |
| **服务器位置** | 香港 | 英/德/美/加 | 国内 | 国内（杭州/北京） | 海外 |
| **国内 4G 连得上吗** | 香港一般可以 | ⚠️ 未核实，需实测 | ✅ 可以 | ✅ 可以 | ⚠️ 未核实 |
| **要地图吗** | 自带 | 自带 | 自己写 | 自己写 | 自己写 |
| **服务端要写代码吗** | ❌ 不用 | ❌ 不用 | ✅ 要 | ✅ 要 | ✅ 要 |
| **注册要实名吗** | 不要（仅 demo 要邮箱） | 不要（仅邮箱） | 不要（仅邮箱） | **要实名 + 手机号** | 不用注册 |
| **备案** | 香港不需要 | 不需要 | 不需要 | 不需要 | 不需要 |
| **Claude 能自动搭吗** | ✅ 部署全自动 | ✅ 部分 | ✅ 网页部分 | ✅ 网页部分 | ✅ |
| **适合当 demo 实物吗** | ✅✅ 最合适 | ✅ 先用这个 | ⚠️ 中等 | ⚠️ 中等 | ⚠️ 只测试 |

---

## 7. 必须由人来做的步骤清单（Claude 做不了的）

**记住一句话：Claude 收不到短信、收不到邮件、没有身份证、不能付钱。所以下面这四类事永远要你自己来。**

| # | 事项 | 为什么 Claude 做不了 | 涉及方案 |
|---|---|---|---|
| 1 | **注册任何账号** | 需要邮箱验证码 / 手机验证码 | 全部 |
| 2 | **实名认证** | 需要身份证 + 人脸识别 | EMQX 中国站、阿里云、腾讯云、高德/百度地图 key |
| 3 | **付款买服务器 / 买流量卡** | 需要支付工具 | Traccar 自建、流量卡 |
| 4 | **办理 SIM 卡** | 需要本人实名（未成年人需监护人） | 全部 |
| 5 | （如果买国内服务器）**ICP 备案** | 需要身份证 + 人脸核验，未成年人基本办不了 | 国内服务器方案 |

**反过来，Claude 能全自动做的：**
- ✅ 写/改固件代码（含 MQTT、HTTP、定位、省电逻辑）
- ✅ Arduino CLI 编译 + 烧录 + 看串口日志
- ✅ 写服务端网页（Leaflet 地图 + 订阅 MQTT）
- ✅ SSH 登录服务器装 Docker、跑 Traccar、配防火墙
- ✅ 调 API 创建设备、拿 deviceid、配规则

---

## 8. 未核实 / 推算的清单（诚实声明）

| 事项 | 状态 |
|---|---|
| 4G 流量消耗的具体数字（8.3 / 34 / 230 MB 每月） | **推算**（依据 RFC 791/9293 和 MQTT 规范的字节数计算，未实测） |
| MQTT 比 HTTP 省多少电 | **推算**（基于协议开销；无官方数据） |
| MQTT 长连接能否跨过 ESP32 浅休眠 | **未核实**，建议实测 |
| 运营商 NAT 超时具体多少秒 | **未核实**，建议实测 |
| 物联网卡能不能访问境外服务器 | **未核实**（华为云官方文档只说明了「定向流量=白名单内才放行」的机制；境外能否入白名单无官方明文）；**强烈建议实测** |
| 巴法云免费额度的具体数字 | **未核实**（官方文档未公布） |
| HiveMQ Cloud 免费档是否还存在 | **未能核实**（现行定价页上已看不到原「100 连接永久免费」档位，请以注册页为准） |
| ThingSpeak / Ubidots / Datacake / Blynk 免费额度 | **未核实**（未抓到官方页面） |
| 百度地图 JS API 免费额度 | **未核实** |
| OneNET 流量套餐价格 | 来自官方页面的搜索摘要，**该页 JS 渲染未能直接抓取正文**，建议以页面为准 |
| 腾讯云/阿里云学生机价格 | 来自官方活动页，**活动价会变**，下单前请核对 |
| Traccar demo 服务器注册是否需要邮箱验证 | **未核实** |
| Traccar 内置高德/腾讯图层是否需要 key | **未完全核实**（源码显示默认不可用、需填 key，建议以实际运行结果为准） |

---

## 9. 下一步建议（给用户的行动清单）

1. **先花 10 分钟**：在 <https://demo.traccar.org> 注册一个免费账号，创建设备，记下 deviceid
2. **把账号密码告诉我**，我把 LilyGo 官方 Traccar 示例改好、编译、烧录
3. **插上一张能上网的 SIM 卡**（建议先用手机副卡试，别急着买物联网卡）
4. **看效果**：Traccar 网页上应该每分钟出现一个新位置点
5. **如果 Traccar demo 服务器连不上**（说明你的卡出不了国），就切**巴法云方案**，我来写地图网页
6. **如果一切顺利、想长期用**，再考虑买香港轻量服务器（约 38 元/月），我把 Traccar 全自动部署好

---

**主要一手来源汇总：**

- LilyGo T-SIM7670G-S3 官方文档：<https://wiki.lilygo.cc/products/t-sim-series/t-sim7670g-s3/quick-start.html>
- LilyGo 官方 Traccar 示例：<https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/examples/Traccar/Traccar.ino>
- SIMCom SIM767XX 官方 AT 指令手册 V1.02（380 页，第 14 章 HTTP、第 16 章 MQTT）：<https://download.mikroe.com/documents/datasheets/SIM767xx_Series_AT_command_manual_v1.02.pdf>
- Traccar 协议文档（OsmAnd）：<https://www.traccar.org/osmand/>
- Traccar 官方 demo 服务器：<https://www.traccar.org/demo/>
- Traccar 官方 Docker：<https://www.traccar.org/docker/>
- EMQX Cloud Serverless 定价：<https://www.emqx.com/en/cloud/serverless-mqtt>
- EMQX 部署区域：<https://docs.emqx.com/zh/cloud/latest/deployments/regions.html>
- EMQX 公共 broker：<https://www.emqx.com/en/mqtt/public-mqtt5-broker>
- 巴法云 MQTT 文档：<https://cloud.bemfa.com/docs/src/mqtt.html>
- HiveMQ 定价：<https://www.hivemq.com/pricing/>
- 华为云物联网定向流量说明：<https://support.huaweicloud.com/usermanual-ocgsl/oceanlink_04_0059.html>
- 腾讯云实名认证说明：<https://cloud.tencent.com/document/product/378/3629>
- 阿里云个人实名认证：<https://help.aliyun.com/zh/account/user/individual-real-name-authentication>
- 高德地图 JS API key 申请：<https://lbs.amap.com/api/javascript-api-v2/guide/abc/prepare>
- OASIS MQTT 3.1.1 规范：<https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html>
- RFC 791（IP 头 20 字节）：<https://www.rfc-editor.org/rfc/rfc791>
- RFC 9293（TCP 头 20 字节）：<https://www.rfc-editor.org/rfc/rfc9293>
