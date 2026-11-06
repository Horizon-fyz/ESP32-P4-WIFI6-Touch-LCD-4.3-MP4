#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBK_LEAD_MIN 0x81
#define GBK_TRAIL_MIN 0x40
#define GBK_TRAIL_COUNT 191
#define GBK_LEAD_COUNT 126
#define GBK_TABLE_SIZE (GBK_LEAD_COUNT * GBK_TRAIL_COUNT)

extern const uint16_t gbk2uni[GBK_TABLE_SIZE];

/* 查询 GBK 双字节字符对应的 Unicode 码点，未收录返回 0。 */
static inline uint16_t gbk_lookup(uint8_t lead, uint8_t trail)
{
    if (lead < GBK_LEAD_MIN || trail < GBK_TRAIL_MIN) {
        return 0;
    }
    int row = lead - GBK_LEAD_MIN;
    int col = trail - GBK_TRAIL_MIN;
    return gbk2uni[row * GBK_TRAIL_COUNT + col];
}

#ifdef __cplusplus
}
#endif
