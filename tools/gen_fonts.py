#!/usr/bin/env python3
"""生成 LVGL 二进制字体（.bin）。

阅读器需要中文点阵字体，而 LVGL 自带字体不含 CJK。这里用 Noto Sans SC
裁剪出 GB2312 全集 + ASCII + 中文标点，再用 lv_font_conv 生成多个字号的
.bin 文件，随固件一起嵌入。

两个关键约束（都踩过坑）：

1. 必须用 --no-compress。
   lv_font_conv 的 RLE(+prefilter) 压缩位图在 LVGL 9.5 上解出来的字形是
   乱的，非压缩（PLAIN）路径正常。

2. 单个字体文件的解码后位图必须 < 1MB。
   lv_font_fmt_txt_glyph_dsc_t.bitmap_index 只有 20 位，上限 1048575 字节。
   中文字库 4bpp 时单个字号会到 1.6~3.9MB，索引回绕后后半段字形全部指到
   错误位置（表现为部分汉字变成乱码）。因此按字节数自动拆包，运行时用
   lv_font_t.fallback 把各包串起来。

用法：
    python tools/gen_fonts.py                 # 生成 22/26/30/34
    python tools/gen_fonts.py --sizes 22 30
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "fonts"
CACHE_DIR = ROOT / "tools" / ".cache"
MANIFEST = ROOT / "main" / "app" / "fonts_manifest.h"

FONT_URL = (
    "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/"
    "NotoSansSC%5Bwght%5D.ttf"
)
FONT_FILE = CACHE_DIR / "NotoSansSC-VF.ttf"

# 单个字体包解码后位图的目标上限（字节）。
# LVGL 20 位索引上限是 1048575，留出余量避免临界溢出。
PACK_BUDGET = 800 * 1024

BPP = 4

# 额外需要的符号区间（ASCII、中文标点、全角字符、常见排版符号）
EXTRA_RANGES = [
    (0x0020, 0x007E),
    (0x00B7, 0x00B7),
    (0x2010, 0x2027),
    (0x3000, 0x303F),
    (0xFF01, 0xFF5E),
]


def gb2312_set() -> str:
    """GB2312 全部字符：一级字 3755 + 二级字 3008 + 符号区。"""
    chars = []
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    return "".join(chars)


def ensure_source_font() -> Path:
    if FONT_FILE.exists() and FONT_FILE.stat().st_size > 1_000_000:
        return FONT_FILE
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    import urllib.request

    print(f"下载字体 {FONT_URL}")
    with urllib.request.urlopen(FONT_URL, timeout=180) as resp, open(FONT_FILE, "wb") as fh:
        shutil.copyfileobj(resp, fh)
    if FONT_FILE.stat().st_size < 1_000_000:
        raise RuntimeError("字体下载不完整，请手动放到 " + str(FONT_FILE))
    return FONT_FILE


def subset_font(src: Path, chars: str) -> Path:
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    out = CACHE_DIR / "subset.ttf"
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.drop_tables += ["DSIG"]

    # 可变字体先固定到 Regular(400)，再把字形转为静态 TTF。
    # 注意：不要用 CFF(OTF) 字体，opentype.js 解析 CFF 的度量会出错，
    # 导致生成的字号偏小、行高异常、字形发糊。
    font = TTFont(str(src))
    if "fvar" in font:
        font = instantiateVariableFont(font, {"wght": 400})

    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=[ord(c) for c in chars])
    subsetter.subset(font)
    font.save(str(out))
    font.close()
    return out


def find_lv_font_conv() -> list[str]:
    import os

    env = os.environ.get("LV_FONT_CONV")
    if env and Path(env).exists():
        return [env]

    exe = shutil.which("lv_font_conv")
    if exe:
        return [exe]

    local_bin = ROOT / "tools" / ".node" / "node_modules" / ".bin"
    for name in ("lv_font_conv.cmd", "lv_font_conv"):
        cand = local_bin / name
        if cand.exists():
            return [str(cand)]

    npm = shutil.which("npm")
    if npm:
        print("本地未找到 lv_font_conv，安装到 tools/.node ...")
        subprocess.run(
            [npm, "install", "--prefix", str(ROOT / "tools" / ".node"), "--no-audit",
             "--no-fund", "lv_font_conv"],
            check=True,
        )
        for name in ("lv_font_conv.cmd", "lv_font_conv"):
            cand = local_bin / name
            if cand.exists():
                return [str(cand)]
    raise RuntimeError("找不到 lv_font_conv，请先执行 npm install -g lv_font_conv")


def report_metrics(path: Path) -> None:
    """打印生成字体的关键度量，便于发现字号/行高异常。"""
    import struct

    data = path.read_bytes()[:64]
    if len(data) < 40 or data[4:8] != b"head":
        return
    _, _, fsize, asc, desc, _, _, _, _, _, dadv, _ = struct.unpack_from("<IHHHhHhHhhHH", data, 8)
    print(f"    {path.name}: size={fsize} ascent={asc} descent={desc} "
          f"line={asc - desc} advance={dadv}")


def estimate_glyph_bytes(size: int) -> int:
    """估算单字形解码后占用：位图 (size*size*bpp/8) + 位字段 + 位域对齐余量。"""
    return (size * size * BPP + 7) // 8 + 12


def split_chars(chars: str, size: int) -> list[str]:
    per_pack = max(1, PACK_BUDGET // estimate_glyph_bytes(size))
    return [chars[i:i + per_pack] for i in range(0, len(chars), per_pack)]


def verify_pack(path: Path, size: int) -> int:
    """按 LVGL 加载器语义算出该字体解码后的位图总字节数。"""
    import struct

    d = path.read_bytes()
    head_len, = struct.unpack_from("<I", d, 0)
    cmap_len, = struct.unpack_from("<I", d, head_len)
    loca_start = head_len + cmap_len
    loca_len, = struct.unpack_from("<I", d, loca_start)
    count, = struct.unpack_from("<I", d, loca_start + 8)
    glyf_start = loca_start + loca_len
    glyf_len, = struct.unpack_from("<I", d, glyf_start)
    nbits = 34  # 本字体固定：adv 10 位 + xy 6*2 + wh 6*2
    total = 0
    for i in range(count):
        off, = struct.unpack_from("<I", d, loca_start + 12 + i * 4)
        nxt = (struct.unpack_from("<I", d, loca_start + 12 + (i + 1) * 4)[0]
               if i + 1 < count else glyf_len)
        if i == 0:
            continue
        total += max(0, nxt - off - nbits // 8)
    return total


def run_conv(font: Path, size: int, chars: str, out: Path) -> None:
    cmd = find_lv_font_conv() + [
        "--font", str(font),
        "--size", str(size),
        "--bpp", str(BPP),
        "--format", "bin",
        "--output", str(out),
        "--no-compress",
        "--range", "0x20-0x7E",
        "--range", "0xB7",
        "--range", "0x2010-0x2027",
        "--range", "0x3000-0x303F",
        "--range", "0xFF01-0xFF5E",
        "--symbols", chars,
    ]
    subprocess.run(cmd, check=True, shell=False)


def write_manifest(parts_by_size: dict[int, int]) -> None:
    sizes = sorted(parts_by_size)
    max_parts = max(parts_by_size.values())
    lines = [
        "/* 由 tools/gen_fonts.py 自动生成，请勿手工修改 */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define FONT_SIZE_COUNT {len(sizes)}",
        f"#define FONT_PARTS_MAX {max_parts}",
        "",
    ]
    for size in sizes:
        for i in range(parts_by_size[size]):
            lines.append(
                f'extern const uint8_t cjk_{size}_{i}_bin_start[] asm("_binary_cjk_{size}_{i}_bin_start");'
            )
            lines.append(
                f'extern const uint8_t cjk_{size}_{i}_bin_end[] asm("_binary_cjk_{size}_{i}_bin_end");'
            )
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    int size_px;")
    lines.append("    int part_count;")
    lines.append("    const uint8_t *start[FONT_PARTS_MAX];")
    lines.append("    const uint8_t *end[FONT_PARTS_MAX];")
    lines.append("} font_pack_t;")
    lines.append("")
    lines.append("static const font_pack_t g_font_packs[FONT_SIZE_COUNT] = {")
    for size in sizes:
        n = parts_by_size[size]
        lines.append(f"    {{ {size}, {n}, {{")
        lines.append("        " + ", ".join(f"cjk_{size}_{i}_bin_start" for i in range(n))
                     + (", NULL" * (max_parts - n)))
        lines.append("      }, {")
        lines.append("        " + ", ".join(f"cjk_{size}_{i}_bin_end" for i in range(n))
                     + (", NULL" * (max_parts - n)))
        lines.append("      } },")
    lines.append("};")
    lines.append("")
    MANIFEST.write_text("\n".join(lines), encoding="utf-8")
    print(f"清单已写入 {MANIFEST}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sizes", nargs="+", type=int, default=[22, 26, 30, 34])
    args = parser.parse_args()

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    CACHE_DIR.mkdir(parents=True, exist_ok=True)

    # 清掉旧产物，避免 CMake glob 到过期文件
    for old in FONT_DIR.glob("cjk_*.bin"):
        old.unlink()

    chars = gb2312_set()
    extra = "".join(chr(cp) for lo, hi in EXTRA_RANGES for cp in range(lo, hi + 1))
    print(f"字符集：{len(chars)} 个汉字 + {len(extra)} 个 ASCII/标点")
    src = ensure_source_font()
    subset = subset_font(src, chars + extra)

    parts_by_size: dict[int, int] = {}
    total = 0
    for size in args.sizes:
        packs = split_chars(chars, size)
        print(f"{size}px 拆成 {len(packs)} 包（预算 {PACK_BUDGET // 1024}KB/包）")
        for i, group in enumerate(packs):
            out = FONT_DIR / f"cjk_{size}_{i}.bin"
            print(f"  生成 {out.name}（{len(group)} 字）...", flush=True)
            run_conv(subset, size, group, out)
            bitmap_bytes = verify_pack(out, size)
            flag = "OK" if bitmap_bytes < 1048575 else "!!! 超出 20 位索引上限"
            print(f"    {out.stat().st_size / 1024 / 1024:.2f} MB，"
                  f"解码后位图 {bitmap_bytes / 1024:.0f} KB {flag}")
            if bitmap_bytes >= 1048575:
                raise RuntimeError(f"{out.name} 位图超出 LVGL 20 位索引上限，请调小 PACK_BUDGET")
            total += out.stat().st_size
        parts_by_size[size] = len(packs)
        report_metrics(FONT_DIR / f"cjk_{size}_0.bin")

    write_manifest(parts_by_size)
    print(f"完成，共 {total / 1024 / 1024:.2f} MB，输出目录 {FONT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
