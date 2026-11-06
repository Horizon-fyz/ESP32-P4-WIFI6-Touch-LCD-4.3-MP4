#!/usr/bin/env python3
"""完全按 LVGL 9.5 lv_binfont_loader.c 语义模拟解析 .bin 字体，
提取指定字符的字形并保存为 PNG，用于和设备实际渲染对比。

用法: python tools/check_glyph.py fonts/cjk_22.bin 书库刷新设置
"""

import os
import struct
import sys

from PIL import Image

CMAP_FORMAT0_FULL = 0
CMAP_SPARSE_FULL = 1
CMAP_FORMAT0_TINY = 2
CMAP_SPARSE_TINY = 3


class BitReader:
    """LVGL read_bits：字节内 MSB 优先读取，结果同样 MSB 优先组装
    （第一个读到的 bit 是结果的最高位）。"""

    def __init__(self, data):
        self.data = data
        self.pos = 0  # bit 位置

    def read(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos >> 3]
            b = (byte >> (7 - (self.pos & 7))) & 1
            v = (v << 1) | b
            self.pos += 1
        return v

    def read_signed(self, n):
        v = self.read(n)
        return v - (1 << n) if v >= (1 << (n - 1)) else v


def lvgl_decompress(data, w, h, bpp, prefilter):
    """逐行复刻 lv_font_fmt_txt.c 的 decompress/rle_next。"""
    state = [0]      # 0=SINGLE 1=REPEATED 2=COUNTER
    rdp = [0]        # bit 位置
    prev_v = [0]
    count = [0]

    def get_bits(bit_pos, n):
        byte_pos = bit_pos >> 3
        bp = bit_pos & 7
        if bp + n >= 8:
            in16 = (data[byte_pos] << 8) | data[byte_pos + 1]
            return (in16 >> (16 - bp - n)) & ((1 << n) - 1)
        return (data[byte_pos] >> (8 - bp - n)) & ((1 << n) - 1)

    def rle_next():
        if state[0] == 0:  # SINGLE
            ret = get_bits(rdp[0], bpp)
            if rdp[0] != 0 and prev_v[0] == ret:
                count[0] = 0
                state[0] = 1
            prev_v[0] = ret
            rdp[0] += bpp
            return ret
        if state[0] == 1:  # REPEATED
            v = get_bits(rdp[0], 1)
            count[0] += 1
            rdp[0] += 1
            if v:
                ret = prev_v[0]
                if count[0] == 11:
                    count[0] = get_bits(rdp[0], 6)
                    rdp[0] += 6
                    if count[0] != 0:
                        state[0] = 2
                    else:
                        ret = get_bits(rdp[0], bpp)
                        prev_v[0] = ret
                        rdp[0] += bpp
                        state[0] = 0
                return ret
            ret = get_bits(rdp[0], bpp)
            prev_v[0] = ret
            rdp[0] += bpp
            state[0] = 0
            return ret
        # COUNTER
        ret = prev_v[0]
        count[0] -= 1
        if count[0] == 0:
            ret = get_bits(rdp[0], bpp)
            prev_v[0] = ret
            rdp[0] += bpp
            state[0] = 0
        return ret

    out = []
    prev_line = [0] * w
    for y in range(h):
        line = [rle_next() for _ in range(w)]
        if prefilter and y > 0:
            line = [line[x] ^ prev_line[x] for x in range(w)]
        prev_line = line
        out.extend(line)
    return out


def main():
    path, text = sys.argv[1], sys.argv[2]
    d = open(path, "rb").read()

    # ---- head ----
    head_len = struct.unpack_from("<I", d, 0)[0]
    assert d[4:8] == b"head"
    h = 8
    (ver, tbls, fsize, ascent, descent, tasc, tdesc, tgap, miny, maxy,
     dadv, kscale, loc_fmt, gid_fmt, adv_fmt, bpp, xy, wh, adv_bits,
     comp, subpx, _pad) = struct.unpack_from("<IHHHhHhHhhHH10B", d, h)
    print(f"head_len={head_len} size={fsize} ascent={ascent} descent={descent} "
          f"bpp={bpp} xy={xy} wh={wh} adv_bits={adv_bits} loc_fmt={loc_fmt} "
          f"adv_fmt={adv_fmt} comp={comp} subpx={subpx}")

    # ---- cmap ----
    cmap_start = head_len
    cmap_len = struct.unpack_from("<I", d, cmap_start)[0]
    assert d[cmap_start + 4:cmap_start + 8] == b"cmap"
    count = struct.unpack_from("<I", d, cmap_start + 8)[0]
    recs = []
    p = cmap_start + 12
    for _ in range(count):
        off, rs, rl, gs, n, fmt = struct.unpack_from("<IIHHHB", d, p)
        recs.append((off, rs, rl, gs, n, fmt))
        p += 16
    print(f"cmap: {count} records, section {cmap_len} bytes")

    def lookup(cp):
        for off, rs, rl, gs, n, fmt in recs:
            rcp = cp - rs
            if rcp < 0 or rcp >= rl:
                continue
            base = cmap_start + off
            if fmt == CMAP_FORMAT0_TINY:
                return gs + rcp
            if fmt == CMAP_FORMAT0_FULL:
                o = d[base + rcp]
                if o == 0 and rcp != 0:
                    continue
                return gs + o
            chars = [struct.unpack_from("<H", d, base + i * 2)[0] for i in range(n)]
            if rcp not in chars:
                continue
            idx = chars.index(rcp)
            if fmt == CMAP_SPARSE_FULL:
                return gs + struct.unpack_from("<H", d, base + n * 2 + idx * 2)[0]
            return gs + idx  # SPARSE_TINY
        return 0

    # ---- loca ----
    loca_start = cmap_start + cmap_len
    loca_len = struct.unpack_from("<I", d, loca_start)[0]
    assert d[loca_start + 4:loca_start + 8] == b"loca"
    nglyphs = struct.unpack_from("<I", d, loca_start + 8)[0]
    offs = []
    for i in range(nglyphs):
        if loc_fmt == 0:
            offs.append(struct.unpack_from("<H", d, loca_start + 12 + i * 2)[0])
        else:
            offs.append(struct.unpack_from("<I", d, loca_start + 12 + i * 4)[0])
    print(f"loca: {nglyphs} glyphs, last offset={offs[-1]}")

    # ---- glyf ----
    glyf_start = loca_start + loca_len
    glyf_len = struct.unpack_from("<I", d, glyf_start)[0]
    assert d[glyf_start + 4:glyf_start + 8] == b"glyf"
    print(f"glyf: section {glyf_len} bytes @ {glyf_start}")

    os.makedirs("tools/.cache", exist_ok=True)
    nbits = adv_bits + 2 * xy + 2 * wh

    for ch in text:
        cp = ord(ch)
        gid = lookup(cp)
        print(f"{ch} U+{cp:04X} -> gid {gid}", end="")
        if not gid or gid >= nglyphs:
            print("  (缺字)")
            continue

        # 偏移相对 glyf 节起始（含 8 字节头）
        g = d[glyf_start + offs[gid]:]
        br = BitReader(g)
        adv = br.read(adv_bits) if adv_bits else dadv
        if adv_fmt == 0:
            adv *= 16
        ox = br.read_signed(xy)
        oy = br.read_signed(xy)
        bw = br.read(wh)
        bh = br.read(wh)
        next_off = offs[gid + 1] if gid + 1 < nglyphs else glyf_len
        bmp_size = next_off - offs[gid] - nbits // 8
        print(f"  adv={adv} ofs=({ox},{oy}) box={bw}x{bh} bmp={bmp_size}B")

        if bw * bh == 0:
            continue
        # 位图字节：位流紧接位字段
        bmp = bytearray()
        if nbits % 8 == 0:
            for _ in range(bmp_size):
                bmp.append(br.read(8))
        else:
            for _ in range(bmp_size - 1):
                bmp.append(br.read(8))
            last = br.read(8 - nbits % 8) << (nbits % 8)
            bmp.append(last)

        if comp == 0:
            # 像素解包：MSB 优先，每像素 bpp 位
            pixels = []
            for i in range(bw * bh):
                bitpos = i * bpp
                byte = bmp[bitpos >> 3]
                shift = 8 - bpp - (bitpos & 7)
                pixels.append((byte >> shift) & ((1 << bpp) - 1))
        else:
            # 完全复刻 LVGL rle_next 状态机 + comp==1 时的行间 XOR prefilter
            pixels = lvgl_decompress(bytes(bmp), bw, bh, bpp, prefilter=(comp == 1))

        img = Image.new("L", (bw, bh), 0)
        px = img.load()
        for i, v in enumerate(pixels[:bw * bh]):
            px[i % bw, i // bw] = v * 255 // ((1 << bpp) - 1)
        out = f"tools/.cache/chk_{cp:04x}.png"
        img.resize((bw * 4, bh * 4), Image.NEAREST).save(out)
        print("    ->", out)


if __name__ == "__main__":
    main()
