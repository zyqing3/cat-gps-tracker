#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GNSS 最低功耗配置实测 —— PC 脚本用例矩阵（纯 GNSS，完全不碰 4G）

玩法：板子烧 AT 直通桥固件（firmware/at_bridge），本脚本用 COM7 跟模组对话，
按「星座组合 × 启动方式」逐格测「上电到定位耗时（TTFF）」，再按规格书电流
折算成能耗，找出综合功耗最低的 GNSS 功能配置。

为什么要这么测：板子没有电流表，但 GNSS 工作电流已知（规格书 Table 56，
VBAT=3.8V 典型 7~12 mA）。功耗 ≈ 电流 × 时间，同一档星座的电流差别不大，
所以「GNSS 开启时长」几乎就是功耗，时间最短 = 最省电。

用法：
  python scripts/gnss_power_test.py                          # 全矩阵，每格 3 次
  python scripts/gnss_power_test.py --reps 1                 # 快速冒烟
  python scripts/gnss_power_test.py --modes 1,15 --starts cold
  python scripts/gnss_power_test.py --timeout 180 --gap 5 --out logs/gnss_power.json

输出：logs/gnss_power_<时间戳>.json + .csv，并在终端打印汇总表。

安全：Ctrl+C 会先给模组关 GNSS、释放串口再退出。任何一格超时（默认 180s）
记为 timeout 并继续下一格。
"""

import argparse
import csv
import json
import os
import re
import signal
import statistics
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("缺少 pyserial：pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------- 常量与查表

# AT+CGNSSMODE 的星座组合取值（手册 21.2.7，SIM7670G 实测支持）
MODES = {
    1:  "GPS",
    3:  "GPS+GLONASS",
    5:  "GPS+GALILEO",
    9:  "GPS+BDS",
    13: "GPS+GALILEO+BDS",
    15: "GPS+GLONASS+GALILEO+BDS",
}

# 规格书典型电流（mA，VBAT=3.8V，CAT1 关，独立 GNSS 接收机）
# 来源：docs/reference/SIM7672X_Series_Hardware_Design_V1.02.pdf 表 56
# ⚠️ 该表按「星座 × 场景」混排（tracking / cold start / 失锁 / 搜索），
#    且 SIM7672X 与本板 SIM7670G 属同族但非同一型号，故整体按 ±2 mA 误差看待。
#    取值优先级：本实验每格都是「断电后重新搜星」，所以优先取 cold start 值；
#    没有 cold start 行时退而取 tracking 值。
SPEC_CURRENT_MA = {
    1:  7.4,    # GPS(L1) tracking
    3:  9.8,    # GPS+GLONASS cold start
    5:  10.0,   # GPS+GALILEO：手册无此行，按相邻配置内插（明确标注为估计值）
    9:  8.7,    # GPS+BEIDOU tracking
    13: 11.1,   # GPS+GALILEO+BEIDOU cold start
    15: 10.7,   # GPS+GLONASS+GALILEO+BEIDOU cold start
}
FALLBACK_CURRENT_MA = 10.0  # 查不到时的兜底

# 天线电源 & GNSS 电源（GPIO 1 = 天线电源，见 utilities.h STAN 段）
CMD_ANT_ON  = "AT+CGDRT=1,1"     # GPIO1 设为输出
CMD_ANT_V1  = "AT+CGSETV=1,1"    # GPIO1 输出高 = 天线供电
CMD_ANT_V0  = "AT+CGSETV=1,0"    # GPIO1 输出低 = 天线断电
CMD_PWR_ON  = "AT+CGNSSPWR=1"
CMD_PWR_OFF = "AT+CGNSSPWR=0"


# ---------------------------------------------------------------- 串口桥封装

class Bridge:
    """跟 at_bridge 固件对话。

    桥的协议（见 firmware/at_bridge/at_bridge.ino）：
      收到一行 → 先回显 "> 命令" → 原样转给模组 → 打印模组回应 → 再补一个空行
    模组回应读到「静默 300ms」或最多 5 秒为止。所以 PC 侧每条命令
    至少要留 300ms 静默窗口，超时给 8 秒足够。
    """

    def __init__(self, port, baud=115200, verbose=True):
        self.port = port
        self.baud = baud
        self.verbose = verbose
        self.ser = None
        self._stop = False

    # -- 连接管理 ----------------------------------------------------------
    def open(self):
        # 注意：不要在打开后动 DTR/RTS —— ESP32-S3 原生 USB-CDC 会因此复位，
        # 复位→桥重跑 setup→可能给模组来一发 PWRKEY（变成关机），纯属自找麻烦。
        self.ser = serial.Serial(self.port, self.baud, timeout=0.1)

    def close(self):
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def request_stop(self, *_):
        # 第一次 Ctrl+C：温和收尾（跑完当前这一轮，关 GNSS，放串口）
        # 第二次 Ctrl+C：不再客气，直接按默认行为抛 KeyboardInterrupt 立刻退出
        # （否则连按也没用，用户会以为脚本卡死了）
        if self._stop:
            print("\n[!] 再次收到 Ctrl+C，立即退出")
            raise KeyboardInterrupt
        print("\n[!] 收到 Ctrl+C，正在收尾（关 GNSS + 释放串口）... "
              "再按一次 Ctrl+C 可强制立即退出")
        self._stop = True

    @property
    def stopping(self):
        return self._stop

    # -- 收发 --------------------------------------------------------------
    def send(self, cmd, timeout=8.0, quiet=1.0):
        """发一条 AT，返回桥回显+模组回应的整段文本。"""
        if self.ser is None:
            raise RuntimeError("串口未打开")
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass
        self.ser.write((cmd + "\r\n").encode("ascii", "ignore"))

        t0 = time.time()
        last = t0
        buf = b""
        while time.time() - t0 < timeout:
            try:
                d = self.ser.read(4096)
            except Exception:
                break
            if d:
                buf += d
                last = time.time()
                # 已经看到结果码且静默够了，就可以收工
                filled = buf.decode("utf-8", "replace")
                if self._has_result(filled) and time.time() - last > quiet:
                    break
            elif time.time() - last > quiet:
                break
        text = buf.decode("utf-8", "replace")
        if self.verbose:
            print(f"    > {cmd}")
        return text

    @staticmethod
    def _has_result(text):
        """文本里是否出现了最终结果码（OK / ERROR / READY!）。"""
        for raw in text.splitlines():
            line = raw.strip()
            if line in ("OK", "ERROR") or line.endswith("READY!"):
                return True
        return False

    def wait_ok(self, cmd, timeout=8.0):
        """发命令并判断是否 OK。返回 (ok, text)。"""
        text = self.send(cmd, timeout=timeout)
        ok = any(l.strip() == "OK" for l in text.splitlines())
        return ok, text

    # -- 模组存活 ----------------------------------------------------------
    def ensure_alive(self, tries=12, interval=1.5):
        """确认模组回 AT；不行就 PWRKEY 脉冲重开再等。"""
        for _ in range(tries):
            ok, _t = self.wait_ok("AT", timeout=3.0)
            if ok:
                return True
            if self._stop:
                return False
            time.sleep(interval)

        # 桥刚复位 / 模组睡着：脉冲重开（~reset 打一次 PWRKEY）
        print("  [!] 模组无响应，发 ~reset 脉冲重开...")
        self.send("~reset", timeout=5.0)
        for _ in range(30):
            time.sleep(2)
            if self._stop:
                return False
            ok, _t = self.wait_ok("AT", timeout=3.0)
            if ok:
                print("  [OK] 模组已唤醒")
                return True
        return False


# ---------------------------------------------------------------- GNSS 控制

def gnss_off(br):
    """关 GNSS：先断天线，再关总电源（顺序按项目既有约定）。

    注意 CGSETV 前必须先 CGDRT 把 GPIO 设成输出方向，否则模组直接回 ERROR
    （手册 9.2.11 明确要求），天线也就断不掉了。
    """
    br.wait_ok(CMD_ANT_ON)   # 先把 GPIO1 设成输出方向，否则下面 CGSETV 会 ERROR
    br.wait_ok(CMD_ANT_V0)
    br.wait_ok(CMD_PWR_OFF)


def gnss_on(br):
    """开 GNSS：天线供电 → 总电源。返回 (ok, 上电时刻, 原始文本)。

    返回的上电时刻 = 收到 +CGNSSPWR: READY! 的时刻（没有 READY! 就退而
    用发完命令的时刻），这个时刻之后模组就开始吃电了，用于算能耗窗口。
    """
    br.wait_ok(CMD_ANT_ON)          # GPIO1 方向 = 输出
    ok_ant, _ = br.wait_ok(CMD_ANT_V1)   # GPIO1 = 高，天线供电
    text = br.send(CMD_PWR_ON, timeout=15.0)
    t_power = time.time()
    ok = ("READY!" in text) or any(l.strip() == "OK" for l in text.splitlines())
    return (ok and ok_ant), t_power, text


def set_mode(br, mode):
    """设星座组合。手册：CGNSSMODE 必须在 GNSS 上电后才有效。"""
    return br.wait_ok(f"AT+CGNSSMODE={mode}")[0]


def parse_cgnssinfo(text):
    """从回应文本里抠出 +CGNSSINFO 那一行并解析成字段 dict。

    字段（手册 21.2.21）：
      mode, GPS, GLONASS, GALILEO, BEIDOU, lat, N/S, lon, E/W,
      date, utc, alt, speed, course, PDOP, HDOP, VDOP, NoSV
    未定位时是一串空字段（全逗号）。
    """
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("+CGNSSINFO:"):
            continue
        body = line[len("+CGNSSINFO:"):].strip()
        parts = [p.strip() for p in body.split(",")]

        def num(i):
            if i < len(parts) and parts[i] != "":
                try:
                    return float(parts[i])
                except ValueError:
                    return None
            return None

        return {
            "mode": int(parts[0]) if parts and parts[0].isdigit() else None,
            # 字段顺序采用**手册 21.2.21**：GPS, GLONASS, GALILEO, BEIDOU。
            # （TinyGSM 的 TinyGsmClientSIM7672.h 注释里写的是 GPS,BEIDOU,GLONASS,
            #  GALILEO，两者矛盾。本板实测 mode=9(GPS+北斗) 只有第 1、第 4 个字段
            #  有数、第 2/3 字段为空 —— 正好对应「GPS + BEIDOU 使能」，故采信手册顺序。）
            "sats": {
                "gps": num(1), "glonass": num(2), "galileo": num(3), "beidou": num(4),
            },
            "lat": parts[5] if len(parts) > 5 and parts[5] else None,
            "ns": parts[6] if len(parts) > 6 and parts[6] else None,
            "lon": parts[7] if len(parts) > 7 and parts[7] else None,
            "ew": parts[8] if len(parts) > 8 and parts[8] else None,
            "date": parts[9] if len(parts) > 9 and parts[9] else None,
            "utc": parts[10] if len(parts) > 10 and parts[10] else None,
            "pdop": num(14), "hdop": num(15), "vdop": num(16),
            "nosv": num(17),
            "raw": line,
        }
    return None


def is_valid_fix(info):
    """判断一个 mode=2/3 的回应是不是「真定位」。

    项目主固件踩过的坑：刚上电的 GNSS 偶尔报「假定位」——mode 明明是 2/3，
    但卫星数是空的、日期是乱码，这种数据服务器会拒收。所以这里跟主固件
    stepWaitFix 用同一套体检标准：必须看得到卫星 + 日期是合理的 ddmmyy。
    """
    if info is None or info["mode"] not in (2, 3):
        return False
    # 至少得看到卫星（全空 = 假定位）
    total = sum(v for v in info["sats"].values() if v)
    if total <= 0:
        return False
    # 日期必须是合理的 ddmmyy（20 年之后），否则时间的可信度存疑
    d = info.get("date")
    if not d or len(d) != 6 or not d.isdigit():
        return False
    day, mon, yr = int(d[0:2]), int(d[2:4]), int(d[4:6])
    return 1 <= day <= 31 and 1 <= mon <= 12 and 20 <= yr <= 99


# ---------------------------------------------------------------- 单格测试

def run_cell(br, mode, start, args):
    """跑一格：开 GNSS → 设星座 →（可选冷启动）→ 轮询到定位 → 关 GNSS。"""
    label = f"mode={mode}({MODES.get(mode, '?')}) start={start}"
    print(f"\n--- {label} ---")

    result = {
        "mode": mode,
        "mode_name": MODES.get(mode, str(mode)),
        "start": start,
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "ok": False,
        "timeout": False,
        "error": None,
        "ttff_s": None,        # 搜索开始 → 定位（头条指标）
        "setup_s": None,       # GNSS 上电 → 搜索开始（上电初始化 + 设星座）
        "gnss_on_s": None,     # GNSS 上电 → 定位（= 能耗窗口）
        "polls": 0,
        "fake_fixes": 0,
        "sats": None,
        "fix_mode": None,
        "fix": {},
        "last_info": None,
        "history": [],
    }

    # 上一格收尾：确保 GNSS 是关的，再等 gap 秒让模组冷静
    gnss_off(br)
    time.sleep(args.gap)

    if br.stopping:
        result["error"] = "interrupted"
        return result

    if not br.ensure_alive():
        result["error"] = "modem-dead"
        print("  [X] 模组失联，跳过本格")
        return result

    ok, t_power, text = gnss_on(br)
    if not ok:
        result["error"] = "gnss-power-on-failed"
        print(f"  [X] GNSS 上电失败: {text.strip()!r}")
        gnss_off(br)
        return result
    print(f"  [OK] GNSS 上电 (t_power)")

    time.sleep(args.power_settle)   # 上电初始化

    if not set_mode(br, mode):
        result["error"] = "set-mode-failed"
        print("  [X] 设星座失败")
        gnss_off(br)
        return result

    if start == "cold":
        okc, tc = br.wait_ok("AT+CGPSCOLD", timeout=15.0)
        if not okc:
            result["error"] = "cold-start-failed"
            print(f"  [X] 冷启动命令失败: {tc.strip()!r}")
            gnss_off(br)
            return result
        print("  [OK] 已发冷启动")
    elif start == "hot":
        # 热启动：星历为空时会毒害引擎（项目实测 120 秒零可见星），默认不跑
        okh, th = br.wait_ok("AT+CGPSHOT", timeout=15.0)
        if not okh:
            result["error"] = "hot-start-failed"
            print(f"  [X] 热启动命令失败: {th.strip()!r}")
            gnss_off(br)
            return result
        print("  [OK] 已发热启动")

    # 搜索正式开始计时（此时星座组合已生效）
    t0 = time.time()
    result["setup_s"] = round(t0 - t_power, 2)
    print(f"  [t] 开始搜星（setup {result['setup_s']:.1f}s）")

    deadline = t0 + args.timeout
    fix = None
    while time.time() < deadline:
        if br.stopping:
            result["error"] = "interrupted"
            break
        text = br.send("AT+CGNSSINFO", timeout=6.0, quiet=0.4)
        info = parse_cgnssinfo(text)
        result["polls"] += 1
        now = time.time()
        if info is not None:
            result["last_info"] = info["raw"]
            elapsed = round(now - t0, 2)
            # 记录轨迹，方便事后看信号是怎么长起来的
            if len(result["history"]) < 400:
                result["history"].append({
                    "t": round(now - t_power, 2),
                    "mode": info["mode"],
                    "sats": info["sats"],
                    "raw": info["raw"],     # 留原始回应，报告里当证据
                })
            if info["mode"] in (2, 3) and not is_valid_fix(info):
                # 假定位：记下来但不认，继续等真的
                result["fake_fixes"] += 1
                print(f"    (假定位，忽略：{info['raw']})")

            if is_valid_fix(info):
                fix = info
                result["ttff_s"] = elapsed
                result["gnss_on_s"] = round(now - t_power, 2)
                result["fix_mode"] = info["mode"]
                result["sats"] = info["sats"]
                result["fix"] = {
                    k: info[k] for k in
                    ("lat", "ns", "lon", "ew", "date", "utc",
                     "pdop", "hdop", "vdop", "nosv")
                }
                result["ok"] = True
                print(f"  [OK] 定位成功 TTFF={result['ttff_s']:.0f}s "
                      f"(GNSS 开启 {result['gnss_on_s']:.0f}s) "
                      f"sats={result['sats']}")
                break
        # CGNSSINFO 每秒一次（桥自身每次约 300ms 静默窗口，节奏约 1s）
        remain = 1.0 - (time.time() - now)
        if remain > 0:
            time.sleep(remain)

    if not result["ok"] and not result["error"]:
        result["timeout"] = True
        result["ttff_s"] = args.timeout
        result["gnss_on_s"] = round(time.time() - t_power, 2)
        print(f"  [X] {args.timeout}s 超时未定位（记 timeout）")

    gnss_off(br)
    return result


# ---------------------------------------------------------------- 汇总与输出

def median_or_none(vals):
    vals = [v for v in vals if v is not None]
    return statistics.median(vals) if vals else None


def summarize(results, args):
    """按 (mode, start) 分组算中位数等统计量。"""
    groups = {}
    for r in results:
        groups.setdefault((r["mode"], r["start"]), []).append(r)

    rows = []
    for (mode, start), rs in sorted(groups.items()):
        ttff = [r["ttff_s"] for r in rs if r["ok"]]
        on = [r["gnss_on_s"] for r in rs if r["ok"]]
        n_sat = []
        for r in rs:
            if r["ok"] and r["sats"]:
                n_sat.append(sum(v for v in r["sats"].values() if v))
        cur = SPEC_CURRENT_MA.get(mode, FALLBACK_CURRENT_MA)

        med_on = median_or_none(on)
        rows.append({
            "mode": mode,
            "mode_name": MODES.get(mode, str(mode)),
            "start": start,
            "runs": len(rs),
            "ok": sum(1 for r in rs if r["ok"]),
            "timeouts": sum(1 for r in rs if r["timeout"]),
            "errors": sum(1 for r in rs if r["error"]),
            "fake_fixes": sum(r.get("fake_fixes", 0) for r in rs),
            "ttff_median_s": median_or_none(ttff),
            "ttff_min_s": min(ttff) if ttff else None,
            "ttff_max_s": max(ttff) if ttff else None,
            "gnss_on_median_s": med_on,
            "sats_median": median_or_none(n_sat),
            "current_ma": cur,
            # 能耗：中位开启时长 × 估算电流，换算成 mAh
            "energy_mah": round(cur * med_on / 3600.0, 4) if med_on else None,
        })
    return rows


def print_table(rows, results):
    print("\n" + "=" * 100)
    print("汇总表（每格 ≥1 次；TTFF 取中位数）")
    print("=" * 100)
    hdr = (f"{'星座组合':<26}{'启动':<9}{'成功':<6}{'TTFF中位':<10}"
           f"{'GNSS开':<9}{'卫星':<6}{'估算电流':<10}{'估算能耗':<10}")
    print(hdr)
    print("-" * 100)
    for r in rows:
        ttff = f"{r['ttff_median_s']:.0f}s" if r["ttff_median_s"] is not None else "-"
        on = f"{r['gnss_on_median_s']:.0f}s" if r["gnss_on_median_s"] is not None else "-"
        sat = f"{r['sats_median']:.0f}" if r["sats_median"] is not None else "-"
        en = f"{r['energy_mah']:.3f}mAh" if r["energy_mah"] is not None else "-"
        print(f"{r['mode_name']:<26}{r['start']:<9}"
              f"{r['ok']}/{r['runs']:<4}{ttff:<10}{on:<9}{sat:<6}"
              f"{r['current_ma']:.1f}mA{'':<4}{en:<10}")
    print("-" * 100)

    # 挑赢家：先看能耗，再看耗时
    good = [r for r in rows if r["energy_mah"] is not None]
    if good:
        best = min(good, key=lambda r: r["energy_mah"])
        print(f"\n* 最低功耗配置：{best['mode_name']} / {best['start']} "
              f"（TTFF 中位 {best['ttff_median_s']:.0f}s，"
              f"估计 {best['energy_mah']:.3f} mAh）")

    to = sum(1 for r in results if r["timeout"])
    er = sum(1 for r in results if r["error"])
    print(f"\n共 {len(results)} 次测试：成功 {sum(1 for r in results if r['ok'])}，"
          f"超时 {to}，出错 {er}")


def write_outputs(results, rows, args):
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base = args.out or os.path.join("logs", f"gnss_power_{stamp}")
    root, _ = os.path.splitext(base)

    payload = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "port": args.port,
        "reps": args.reps,
        "timeout_s": args.timeout,
        "gap_s": args.gap,
        "power_settle_s": args.power_settle,
        "modes": args.modes,
        "starts": args.starts,
        "current_assumption_ma": SPEC_CURRENT_MA,
        "current_source": "SIM7672X HW Design V1.02 Table 56 (VBAT=3.8V, CAT1 off)",
        "summary": rows,
        "runs": results,
    }
    with open(root + ".json", "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)

    with open(root + ".csv", "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["星座mode", "星座组合", "启动", "次数", "成功", "超时", "出错",
                    "假定位次数", "TTFF中位s", "TTFF最小s", "TTFF最大s",
                    "GNSS开启中位s", "卫星数中位", "估算电流mA", "估算能耗mAh"])
        for r in rows:
            w.writerow([r["mode"], r["mode_name"], r["start"], r["runs"], r["ok"],
                        r["timeouts"], r["errors"], r.get("fake_fixes", 0),
                        r["ttff_median_s"], r["ttff_min_s"], r["ttff_max_s"],
                        r["gnss_on_median_s"], r["sats_median"],
                        r["current_ma"], r["energy_mah"]])

        # 每次原始记录也写进 CSV，方便复核
        w.writerow([])
        w.writerow(["—— 逐次原始记录 ——"])
        w.writerow(["时刻", "星座mode", "星座组合", "启动", "成功", "超时", "TTFFs",
                    "GNSS开启s", "setup_s", "定位mode", "GPS", "GLONASS",
                    "GALILEO", "BDS", "NoSV", "轮询次数", "错误", "最后CGNSSINFO"])
        for r in results:
            s = r["sats"] or {}
            w.writerow([r["timestamp"], r["mode"], r["mode_name"], r["start"],
                        r["ok"], r["timeout"], r["ttff_s"], r["gnss_on_s"],
                        r["setup_s"], r["fix_mode"],
                        s.get("gps"), s.get("glonass"), s.get("galileo"),
                        s.get("beidou"), r["fix"].get("nosv") if r["fix"] else None,
                        r["polls"], r["error"], r["last_info"]])

    print(f"\n结果已写入：\n   {root}.json\n   {root}.csv")
    return root


# ---------------------------------------------------------------- 入口

def main():
    # Windows 控制台默认 GBK，中文/符号直接 print 会炸（UnicodeEncodeError）。
    # 强制 stdout 走 UTF-8 并把编不出的字符降级替换，保证脚本永远不会因为打印而挂。
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    ap = argparse.ArgumentParser(
        description="GNSS 最低功耗配置实测（纯 GNSS，AT 直通桥 + PC 脚本）")
    ap.add_argument("--port", default="COM7", help="串口（默认 COM7）")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--modes", default="1,9,5,15",
                    help="星座组合，逗号分隔（1 GPS / 3 +GLONASS / 5 +GALILEO "
                         "/ 9 +BDS / 13 +GALILEO+BDS / 15 四系，默认 1,9,5,15）")
    ap.add_argument("--starts", default="default,cold",
                    help="启动方式，逗号分隔（default/cold/hot，默认 default,cold）")
    ap.add_argument("--reps", type=int, default=3, help="每格重复次数（默认 3）")
    ap.add_argument("--timeout", type=int, default=180, help="单格超时秒数（默认 180）")
    ap.add_argument("--gap", type=float, default=5.0, help="两格之间冷却秒数（默认 5）")
    ap.add_argument("--power-settle", type=float, default=2.0,
                    help="GNSS 上电后到设星座的等待秒数（默认 2）")
    ap.add_argument("--out", default=None, help="输出文件名前缀（默认 logs/gnss_power_<时间戳>）")
    ap.add_argument("--quiet", action="store_true", help="少打日志")
    args = ap.parse_args()

    args.modes = [int(m) for m in str(args.modes).split(",") if m.strip()]
    args.starts = [s.strip() for s in str(args.starts).split(",") if s.strip()]

    for m in args.modes:
        if m not in MODES:
            print(f"[!] 未知星座 mode={m}，手册支持 {sorted(MODES)}")

    cells = [(m, s) for m in args.modes for s in args.starts]
    print(f"用例矩阵：{len(cells)} 格 × {args.reps} 次 = {len(cells) * args.reps} 次测试")
    mode_desc = ", ".join(f"{m}={MODES.get(m, '?')}" for m in args.modes)
    print(f"  星座：{mode_desc}")
    print(f"  启动：{', '.join(args.starts)}")
    print(f"  串口：{args.port}  单格超时：{args.timeout}s")

    br = Bridge(args.port, args.baud, verbose=not args.quiet)
    signal.signal(signal.SIGINT, br.request_stop)

    results = []
    try:
        br.open()
        print("等待模组就绪...")
        if not br.ensure_alive():
            print("[X] 模组唤不醒，退出。检查：板子插好没 / 桥固件烧没烧 / 串口被占用没")
            return 1

        # 轮转顺序（第 1 轮跑完所有格，再跑第 2 轮…）：
        # 卫星几何随时间变化，这样能把「时间段偏差」平摊到每个配置上，更公平。
        t_start = time.time()
        for rep in range(args.reps):
            print(f"\n{'=' * 70}\n第 {rep + 1}/{args.reps} 轮\n{'=' * 70}")
            for mode, start in cells:
                if br.stopping:
                    break
                r = run_cell(br, mode, start, args)
                r["rep"] = rep + 1
                results.append(r)
            if br.stopping:
                break
        print(f"\n总耗时 {time.time() - t_start:.0f}s")

    finally:
        # 不管怎么退出，都尽量把 GNSS 关掉再放串口
        try:
            if br.ser and br.ser.is_open:
                gnss_off(br)
        except Exception:
            pass
        br.close()

    if not results:
        print("没有产生任何结果。")
        return 1

    rows = summarize(results, args)
    print_table(rows, results)
    write_outputs(results, rows, args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
