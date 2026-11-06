#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 解码一个 UTF-8 字符，返回消耗的字节数（1-4）；非法字节返回 1，*cp 置为 '?'。 */
int utf8_next(const char *s, uint32_t *cp);

/* 编码一个 Unicode 码点到 UTF-8，返回写入字节数（最多 4）。 */
int utf8_encode(uint32_t cp, char *out);

bool utf8_is_valid(const uint8_t *data, size_t len);

/* 是否为 CJK 字符（含全角标点）。 */
bool cp_is_cjk(uint32_t cp);

/* 不能出现在行首 / 行尾的标点（用于中文断行）。 */
bool cp_cannot_start_line(uint32_t cp);
bool cp_cannot_end_line(uint32_t cp);

/* 把 UTF-8 / UTF-16(LE/BE) / GBK 文本转成 UTF-8，返回堆内存字符串（需 free），
 * 失败返回 NULL。分配优先使用 PSRAM。 */
char *text_to_utf8(const uint8_t *data, size_t len, size_t *out_len);

/* HTML/XHTML 转纯文本：去掉标签、解码实体、保留段落换行。 */
char *html_to_text(const char *html, size_t len, size_t *out_len);

/* 统一换行为 '\n'，去掉 BOM，保证以 '\n' 结尾。 */
void text_normalize(char *text, size_t *len);

/* 去掉首尾空白，返回新的起始指针，长度写入 out_len。 */
char *text_trim(char *s, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif
