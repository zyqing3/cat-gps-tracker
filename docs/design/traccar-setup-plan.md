# Traccar 接入与部署方案（修订版：国内自有服务器）

> 面向读者：初中生。讲清楚"数据发到哪、怎么发、怎么看"。
> 修订原因：用户已有一台国内自建的 Traccar 服务器，直接使用，无需注册 demo 账号、无需购买服务器。

---

## 1. 概览

| 项目 | 内容 |
|---|---|
| 服务器 | **http://traccar.atoo.top:8081**（国内节点，用户已提供） |
| 账号 | zyqing3@aliyun.com（已实测可登录，可管理设备） |
| 设备 | **CatCollar**，设备号（uniqueId）**cat-collar-001**（已通过 API 创建，编号 112） |
| 费用 | **0 元** |

## 2. 数据流

```
  ┌──────────┐   HTTP POST（OsmAnd 协议）   ┌────────────────────────┐   ┌──────────────┐
  │ 项圈固件  │ ──────────────────────────▶ │ traccar.atoo.top:8081   │ ▶ │ 网页地图      │
  │ SIM7670G │     每 60 秒一条定位          │ （国内自有 Traccar）      │   │ 实时位置+轨迹  │
  └──────────┘                              └────────────────────────┘   └──────────────┘
```

## 3. 上报格式（OsmAnd 协议）

设备每 60 秒发一个普通的网页请求：

```
http://traccar.atoo.top:8081/?deviceid=cat-collar-001&lat=31.2304&lon=121.4737&timestamp=2026-10-05T08:00:00Z&speed=0.5&altitude=4.2&batt=95
```

| 参数 | 必填？ | 说明 |
|---|---|---|
| `deviceid` | ✅ 必填 | 就是 **cat-collar-001**（设备的 uniqueId，不是数字编号 112） |
| `lat` / `lon` | ✅ 必填 | 纬度和经度（十进制度，GPS 原始格式直接用） |
| `timestamp` | 可选（建议带上） | UTC 时间，ISO 格式；不带则服务器用收到的时间 |
| `speed` / `altitude` | 可选 | 速度（节）、海拔（米） |
| `batt` | 可选 | 电量百分比（demo 用 USB 供电可省） |

官方协议文档：<https://www.traccar.org/osmand/>

> ⚠️ 注意：这台服务器用的是 **http（明文）**，8081 端口没有开 https。demo 阶段可以接受；LilyGo 示例代码里 `https_begin()` 要换成 http 对应的接口。

## 4. 固件参数（config.h 里就改这几处）

| 参数 | 值 |
|---|---|
| `TRACCAR_SERVER` | `traccar.atoo.top` |
| `TRACCAR_PORT` | `8081` |
| `DEVICE_ID` | `cat-collar-001` |

## 5. 验证清单（拿到板子后按顺序做）

| # | 动作 | 看到什么算通过 |
|---|---|---|
| 1 | 插 SIM 卡、接 GPS 天线、插 USB | 板子亮灯，电脑识别串口 |
| 2 | 烧录固件，打开串口监视器 | 日志：模块开机 → 联网成功 → 搜星 |
| 3 | 拿到室外开阔处等首次定位 | 日志出现经纬度 + 上传成功 |
| 4 | 打开 http://traccar.atoo.top:8081 登录（zyqing3@aliyun.com） | 地图上看到 CatCollar 的位置，每分钟更新 |
| 5 | ⚠️ 若始终连不上服务器 | 回退巴法云方案（见 #7 修订结论与已关闭的 #11） |

## 6. 凭据与信息存放约定

| 信息 | 放哪 |
|---|---|
| deviceid（cat-collar-001）、服务器地址 | 固件 `config.h`（入库没关系，不算秘密） |
| 服务器账号密码 | **不写进仓库**；存放在 Claude 本地记忆里（仅在对话中使用） |

> 🔒 安全提醒：服务器密码目前比较简单，建议有空时在 Traccar 网页右上角「用户设置」里改成强密码；同时别把密码发给陌生人。

## 7. 依据

- docs/research/server-and-protocol-comparison.md（Traccar 部分）
- Traccar OsmAnd 协议文档：<https://www.traccar.org/osmand/>
- 服务器实测：网页首页 200；API 登录成功（用户 zyq）；设备 CatCollar 创建成功（id 112）
