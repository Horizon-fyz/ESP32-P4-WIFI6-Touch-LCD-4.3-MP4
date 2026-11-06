#!/usr/bin/env python3
"""通过串口调试台逐页打开界面并截图，用于 UI 自检。

固件内置命令（main/app/debug_cmd.c）：
    ui <name> / shot / theme <n> / ls <dir> / wifiscan / ble / blescan
    wavtest / bmptest / gview <n> / play <path> / stop

用法：
    python tools/ui_probe.py COM20 tools/.cache/ui
"""

from __future__ import annotations

import base64
import os
import re
import sys
import time

import serial
from PIL import Image

B64_RE = re.compile(r"^[A-Za-z0-9+/=]+$")
SHOT_W, SHOT_H = 480, 800


def send(ser: serial.Serial, text: str) -> None:
    ser.write((text + "\n").encode())
    ser.flush()


def drain(ser: serial.Serial, seconds: float) -> list[str]:
    end = time.time() + seconds
    lines: list[str] = []
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if line:
            lines.append(line)
    return lines


def shot(ser: serial.Serial, path: str) -> bool:
    send(ser, "shot")
    meta = None
    chunks: list[str] = []
    deadline = time.time() + 120
    started = False
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="ignore").strip()
        if line == "DBG shot begin":
            started = True
            continue
        if line.startswith("SHOT "):
            meta = line.split()
            continue
        if line == "SHOTEND":
            break
        if meta and B64_RE.match(line):
            chunks.append(line)
    drain(ser, 1.0)   # 清掉 SHOTEND 之后的调试输出
    if not meta:
        print(f"  ! 未收到截图数据: {path}")
        return False

    raw = base64.b64decode("".join(chunks) + "=" * (-len("".join(chunks)) % 4))
    n = int(meta[3])
    if len(raw) != n:
        print(f"  ! 截图数据不完整 {len(raw)}/{n}")
        return False

    img = Image.new("RGB", (SHOT_W, SHOT_H))
    px = img.load()
    i = 0
    pos = 0
    runs = 0
    while i + 3 < len(raw):
        v = raw[i] | (raw[i + 1] << 8)
        run = raw[i + 2] | (raw[i + 3] << 8)
        i += 4
        r = ((v >> 11) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x3F) * 255 // 63
        b = (v & 0x1F) * 255 // 31
        for _ in range(run):
            if pos >= SHOT_W * SHOT_H:
                break
            px[pos % SHOT_W, pos // SHOT_W] = (r, g, b)
            pos += 1
        runs += 1
    img.save(path)
    print(f"  shot -> {path} ({runs} runs, {pos} px)")
    return pos == SHOT_W * SHOT_H


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM20"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "tools/.cache/ui"
    only = sys.argv[3].split(",") if len(sys.argv) > 3 else None
    os.makedirs(out_dir, exist_ok=True)

    with serial.Serial(port, 115200, timeout=0.3) as ser:
        ser.dtr = False
        ser.rts = False
        # 打开串口会让板子复位：先等启动完成并吞掉开机自动截图
        print("等待设备启动…")
        start = time.time()
        while time.time() - start < 18:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line.startswith("I (") and "启动完成" in line:
                break
        drain(ser, 10.0)   # 吞掉开机自动截图
        ser.reset_input_buffer()

        send(ser, "help")
        print("\n".join(drain(ser, 1.0)))

        if only and "diag" in only:
            for c, wait in (("heap", 1.0), ("wavtest", 8.0), ("heap", 1.0),
                            ("ble", 8.0), ("heap", 1.0), ("wavtest", 8.0),
                            ("heap", 1.0), ("ls /sdcard/music", 1.0)):
                send(ser, c)
                print(f"--- {c} ---")
                print("\n".join(drain(ser, wait)))
            return 0

        if only and "media" in only:
            send(ser, "mktest")
            print("\n".join(drain(ser, 3.0)))
            send(ser, "ui gallery")
            time.sleep(1.5)
            drain(ser, 0.3)
            shot(ser, os.path.join(out_dir, "gallery_jpg.png"))
            send(ser, "gview 0")
            time.sleep(2.0)
            shot(ser, os.path.join(out_dir, "gallery_jpg_view.png"))
            send(ser, "ui video")
            time.sleep(1.2)
            shot(ser, os.path.join(out_dir, "video_list.png"))
            send(ser, "vidplay /sdcard/videos/test.avi")
            time.sleep(2.5)
            shot(ser, os.path.join(out_dir, "video_play1.png"))
            time.sleep(2.0)
            shot(ser, os.path.join(out_dir, "video_play2.png"))
            print("\n".join(drain(ser, 8.0)))
            return 0

        screens = ["home", "library", "gallery", "music", "video", "wifi", "ble", "clock", "settings"]
        do_themes = True
        do_extras = True
        if only:
            do_themes = "themes" in only
            do_extras = "extras" in only
            if "all" in only:
                screens = ["home", "library", "gallery", "music", "video", "wifi", "ble", "clock", "settings"]
            else:
                screens = [s for s in only if s in screens]
        for name in screens:
            send(ser, f"ui {name}")
            time.sleep(2.0)
            drain(ser, 0.3)
            shot(ser, os.path.join(out_dir, f"{name}.png"))

        if not do_themes and not do_extras:
            return 0

        # 主题全局化：夜间 / 护眼
        if do_themes:
            for idx, tag in ((1, "night"), (2, "green"), (0, "paper")):
                send(ser, f"theme {idx}")
                time.sleep(2.0)
                shot(ser, os.path.join(out_dir, f"theme_{tag}.png"))

        if not do_extras:
            return 0

        for d in ("/sdcard/pictures", "/sdcard/music", "/sdcard/videos", "/sdcard/books"):
            send(ser, f"ls {d}")
            print("\n".join(drain(ser, 0.8)))

        send(ser, "bmptest")
        print("\n".join(drain(ser, 2.0)))
        send(ser, "ui gallery")
        time.sleep(1.0)
        drain(ser, 0.3)
        shot(ser, os.path.join(out_dir, "gallery_after_bmp.png"))
        send(ser, "gview 0")
        time.sleep(1.5)
        shot(ser, os.path.join(out_dir, "gallery_view.png"))

        send(ser, "wifiscan")
        print("\n".join(drain(ser, 10.0)))

        send(ser, "ble")
        print("\n".join(drain(ser, 3.0)))
        send(ser, "blescan")
        print("\n".join(drain(ser, 9.0)))

        send(ser, "wavtest")
        print("\n".join(drain(ser, 8.0)))

        send(ser, "stop")
        drain(ser, 0.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
