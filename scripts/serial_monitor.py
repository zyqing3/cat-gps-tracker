#!/usr/bin/env python3
"""
serial_monitor.py —— 抓取 GPS 项圈一个唤醒周期的串口日志

背景：低功耗固件平时深睡（USB 口消失），每 15 分钟醒来几分钟（USB 口出现）。
本脚本守在电脑上：等 ESP32 串口出现 → 连上抓日志 → 板子睡回去（口消失）→ 收工。
日志同时写到 logs/serial_<时间戳>.log。

用法：
  python scripts/serial_monitor.py              # 等下一个唤醒周期，抓完退出
  python scripts/serial_monitor.py --timeout 25 # 最多等多少分钟（默认 25）
"""

import sys
import time
import datetime
import pathlib

import serial
import serial.tools.list_ports

ESP_VID = 0x303A  # Espressif（ESP32-S3 USB 的厂商号）

BAUD = 115200
PORT_GONE_GRACE_S = 10   # 口消失后多等几秒确认（可能只是瞬间重枚举）
QUIET_LIMIT_S = 180      # 口还在但这么久没数据 → 视为异常，收工


def find_esp32_port():
    """找 ESP32 的串口；深睡时找不到，返回 None"""
    for p in serial.tools.list_ports.comports():
        if p.vid == ESP_VID:
            return p.device
    return None


def main():
    # Windows 控制台默认 GBK，强制 UTF-8 输出，避免任务日志乱码
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    wait_min = float(sys.argv[sys.argv.index("--timeout") + 1]) if "--timeout" in sys.argv else 25.0
    deadline = time.time() + wait_min * 60

    print(f"[哨兵] 启动，最多等 {wait_min:.0f} 分钟，等 ESP32 串口出现 ...", flush=True)

    port = None
    while time.time() < deadline:
        port = find_esp32_port()
        if port:
            break
        time.sleep(1)
    if not port:
        print(f"[哨兵] {wait_min:.0f} 分钟内板子没醒（可能没插 USB 或没供电），收工", flush=True)
        return 1

    log_dir = pathlib.Path("logs")
    log_dir.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = log_dir / f"serial_{stamp}.log"
    print(f"[哨兵] 板子醒了！串口 {port}，日志写到 {log_path}", flush=True)

    # open() 前释放 DTR/RTS：打开串口断言 DTR 会复位 ESP32-S3
    # （rst:0x15 USB_UART_CHIP_RESET），深睡中的板子会被打开动作唤醒重启
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    last_data = time.time()
    port_gone_at = None

    try:
        while True:
            try:
                data = ser.readline()
            except serial.SerialException:
                # 板子深睡，USB 口消失
                if port_gone_at is None:
                    port_gone_at = time.time()
                if time.time() - port_gone_at > PORT_GONE_GRACE_S:
                    print("[哨兵] 串口消失（板子睡回去了），本轮日志抓完", flush=True)
                    break
                time.sleep(0.5)
                continue
            if data:
                port_gone_at = None
                last_data = time.time()
                text = data.decode("utf-8", errors="replace").rstrip()
                print(text, flush=True)                      # 打到任务输出
                with open(log_path, "a", encoding="utf-8") as f:
                    f.write(text + "\n")                     # 存盘
            elif time.time() - last_data > QUIET_LIMIT_S:
                print("[哨兵] 长时间无数据，收工", flush=True)
                break
    finally:
        try:
            ser.close()
        except Exception:
            pass

    print(f"[哨兵] 日志文件：{log_path}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
