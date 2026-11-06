"""生成调试用测试素材：一张真实 JPG + 一段 MJPEG AVI（320x240，8 帧）。

只用于自测图片/视频解码，生成到 main/assets/。
"""

import io
import os
import struct

from PIL import Image, ImageDraw

SRC = os.path.join(
    "资料", "02_开发板资料", "ESP32-P4-WIFI6-Touch-LCD-4.3-官方源码",
    "assets", "ESP32-P4-WIFI6-Touch-LCD-4.3-details-1.jpg",
)
OUT = os.path.join("main", "assets")


def make_jpg() -> bytes:
    im = Image.open(SRC).convert("RGB")
    im.save(os.path.join(OUT, "test.jpg"), "JPEG", quality=85)
    return open(os.path.join(OUT, "test.jpg"), "rb").read()


def make_avi() -> None:
    im = Image.open(SRC).convert("RGB").resize((320, 240))
    frames = []
    for i in range(40):
        f = im.copy()
        d = ImageDraw.Draw(f)
        d.rectangle([0, 0, 320, 26], fill=(0, 0, 0))
        d.text((8, 6), f"FRAME {i + 1} / 40", fill=(255, 255, 0))
        d.rectangle([16 + (i % 8) * 32, 208, 42 + (i % 8) * 32, 234], fill=(255, 0, 0))
        buf = io.BytesIO()
        f.save(buf, "JPEG", quality=70)
        frames.append(buf.getvalue())

    w, h, fps = 320, 240, 5
    max_frame = max(len(f) for f in frames)
    total = len(frames)

    # ---- RIFF/AVI 结构 ----
    def chunk(fourcc: bytes, data: bytes) -> bytes:
        pad = b"\x00" if len(data) % 2 else b""
        return fourcc + struct.pack("<I", len(data)) + data + pad

    avih = struct.pack(
        "<IIIIIIIIII4I",
        1000000 // fps,      # microsec per frame
        max_frame * fps,     # max bytes/sec
        0, 0x10, total, 0, 1,
        max_frame, w, h, 0, 0, 0, 0,
    )

    strh = (
        b"vids" + b"MJPG"
        + struct.pack("<IHHIIIIIIII", 0, 0, 0, 0, 1, fps, 0, total, max_frame, 0, 0)
        + struct.pack("<hhhh", 0, 0, w, h)
    )
    strf = struct.pack("<IiiHH4sIiiII", 40, w, h, 1, 24, b"MJPG", w * h * 3, 0, 0, 0, 0)

    movi_payload = b""
    idx = b""
    for f in frames:
        off = 4 + len(movi_payload)  # 相对 movi 数据区（含 'movi' 四字符）
        movi_payload += chunk(b"00dc", f)
        idx += struct.pack("<4sIII", b"00dc", 0x10, off, len(f))

    hdrl = b"LIST" + struct.pack("<I", 4 + 8 + len(avih) + 4 + 8 + len(strh) + 8 + len(strf))
    hdrl += b"hdrl" + chunk(b"avih", avih)
    strl_data = chunk(b"strh", strh) + chunk(b"strf", strf)
    hdrl += b"LIST" + struct.pack("<I", 4 + len(strl_data)) + b"strl" + strl_data

    movi = b"LIST" + struct.pack("<I", 4 + len(movi_payload)) + b"movi" + movi_payload
    body = hdrl + movi + chunk(b"idx1", idx)
    avi = b"RIFF" + struct.pack("<I", 4 + len(body)) + b"AVI " + body

    with open(os.path.join(OUT, "test.avi"), "wb") as fp:
        fp.write(avi)
    print(f"test.avi: {len(avi)} bytes, {total} frames")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_jpg()
    make_avi()
