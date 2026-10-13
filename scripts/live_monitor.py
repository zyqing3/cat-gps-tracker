#!/usr/bin/env python3
"""
live_monitor.py —— 持续把板子串口日志写到 logs/live.log（断线自动重连）

板子深睡时 USB 串口会消失，本脚本自动等它下次出现再接上，日志不断档。
配合另一个终端窗口运行：tail -f logs/live.log  即可实时看日志。
停止：Ctrl+C。
"""

import sys
import time
import pathlib

import serial
import serial.tools.list_ports

LOG = pathlib.Path("logs/live.log")
BAUD = 115200


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x303A:
            return p.device
    return None


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    LOG.parent.mkdir(exist_ok=True)
    print("[live] 启动，日志写到 logs/live.log（另开终端：tail -f logs/live.log）", flush=True)

    ser = None
    while True:
        if ser is None:
            port = None
            for _ in range(60):
                port = find_port()
                if port:
                    break
                time.sleep(1)
            if not port:
                print("[live] 1 分钟内没找到板子串口（深睡中？），继续等 ...", flush=True)
                continue
            try:
                ser = serial.Serial(port, BAUD, timeout=0.2)
                print(f"[live] 已连接 {port}", flush=True)
            except Exception as e:
                print(f"[live] 打开 {port} 失败：{e}，稍后重试", flush=True)
                ser = None
                time.sleep(2)
                continue

        try:
            data = ser.readline()
        except serial.SerialException:
            print("[live] 串口断开（板子睡回去了），等下次醒来 ...", flush=True)
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            continue

        if data:
            text = data.decode("utf-8", errors="replace").rstrip("\r\n")
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(text + "\n")
            print(text, flush=True)


if __name__ == "__main__":
    main()
