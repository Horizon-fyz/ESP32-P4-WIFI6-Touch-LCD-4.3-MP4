#!/usr/bin/env python3
"""生成 GBK -> Unicode 映射表（main/book/gbk_table.c）。

阅读中文 TXT 时经常遇到 GBK/GB2312 编码，ESP-IDF 的 newlib 不带 GBK 转换，
所以在固件里内置一张 126 x 190 的映射表（约 48 KB）。
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_C = ROOT / "main" / "book" / "gbk_table.c"

LEAD_MIN, LEAD_MAX = 0x81, 0xFE
TRAIL_MIN, TRAIL_MAX = 0x40, 0xFE
TRAIL_COUNT = TRAIL_MAX - TRAIL_MIN + 1  # 191，其中 0x7F 无效


def main() -> None:
    rows = []
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        row = []
        for trail in range(TRAIL_MIN, TRAIL_MAX + 1):
            if trail == 0x7F:
                row.append(0)
                continue
            try:
                ch = bytes([lead, trail]).decode("gbk")
                cp = ord(ch)
                row.append(cp if cp <= 0xFFFF else 0)
            except UnicodeDecodeError:
                row.append(0)
        rows.append(row)

    lines = [
        "/* 由 tools/gen_gbk_table.py 自动生成，请勿手工修改。 */",
        '#include "gbk_table.h"',
        "",
        "const uint16_t gbk2uni[GBK_TABLE_SIZE] = {",
    ]
    for row in rows:
        for i in range(0, len(row), 10):
            chunk = ", ".join(f"0x{v:04X}" for v in row[i : i + 10])
            lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    OUT_C.write_text("\n".join(lines), encoding="utf-8")
    print(f"写入 {OUT_C}（{len(rows)} x {TRAIL_COUNT}）")


if __name__ == "__main__":
    main()
