#!/usr/bin/env python3
"""通过串口抓取设备屏幕（依赖固件里的 screenshot_dump_serial）。

用法：
    python tools/capture_screen.py COM20 out.png
"""

from __future__ import annotations

import base64
import io
import re
import sys
import time

import serial
from PIL import Image

B64_RE = re.compile(r"^[A-Za-z0-9+/=]+$")


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM20"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "tools/.cache/shot.png"

    with serial.Serial(port, 115200, timeout=0.5) as ser:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.15)
        ser.rts = False
        ser.reset_input_buffer()

        deadline = time.time() + 300
        meta = None
        chunks: list[str] = []
        while time.time() < deadline:
            line = ser.readline().decode("ascii", errors="ignore").strip()
            if not line:
                continue
            if line.startswith("GLYPHDBG"):
                print(line)
                continue
            if line.startswith("SHOT "):
                meta = line.split()
                continue
            if line == "SHOTEND":
                break
            if meta and B64_RE.match(line):
                chunks.append(line)

    if not meta:
        print("未收到截图数据（检查固件是否包含 screenshot_dump_serial）")
        return 1

    w, h, n = int(meta[1]), int(meta[2]), int(meta[3])
    raw = base64.b64decode("".join(chunks))
    print(f"收到 {len(raw)}/{n} 字节")
    if len(raw) != n:
        print("数据不完整（串口丢包），请重试；本次结果不可信")
        return 2

    img = Image.new("RGB", (w, h))
    pixels = img.load()
    idx = 0
    pos = 0
    while pos + 4 <= len(raw) and idx < w * h:
        color = raw[pos] | (raw[pos + 1] << 8)
        run = raw[pos + 2] | (raw[pos + 3] << 8)
        pos += 4
        r = ((color >> 11) & 0x1F) * 255 // 31
        g = ((color >> 5) & 0x3F) * 255 // 63
        b = (color & 0x1F) * 255 // 31
        for _ in range(run):
            if idx >= w * h:
                break
            pixels[idx % w, idx // w] = (r, g, b)
            idx += 1

    img.save(out_path)
    print("已保存", out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
