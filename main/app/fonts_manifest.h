/* 由 tools/gen_fonts.py 自动生成，请勿手工修改 */
#pragma once
#include <stdint.h>

#define FONT_SIZE_COUNT 3
#define FONT_PARTS_MAX 5

extern const uint8_t cjk_22_0_bin_start[] asm("_binary_cjk_22_0_bin_start");
extern const uint8_t cjk_22_0_bin_end[] asm("_binary_cjk_22_0_bin_end");
extern const uint8_t cjk_22_1_bin_start[] asm("_binary_cjk_22_1_bin_start");
extern const uint8_t cjk_22_1_bin_end[] asm("_binary_cjk_22_1_bin_end");
extern const uint8_t cjk_22_2_bin_start[] asm("_binary_cjk_22_2_bin_start");
extern const uint8_t cjk_22_2_bin_end[] asm("_binary_cjk_22_2_bin_end");
extern const uint8_t cjk_26_0_bin_start[] asm("_binary_cjk_26_0_bin_start");
extern const uint8_t cjk_26_0_bin_end[] asm("_binary_cjk_26_0_bin_end");
extern const uint8_t cjk_26_1_bin_start[] asm("_binary_cjk_26_1_bin_start");
extern const uint8_t cjk_26_1_bin_end[] asm("_binary_cjk_26_1_bin_end");
extern const uint8_t cjk_26_2_bin_start[] asm("_binary_cjk_26_2_bin_start");
extern const uint8_t cjk_26_2_bin_end[] asm("_binary_cjk_26_2_bin_end");
extern const uint8_t cjk_26_3_bin_start[] asm("_binary_cjk_26_3_bin_start");
extern const uint8_t cjk_26_3_bin_end[] asm("_binary_cjk_26_3_bin_end");
extern const uint8_t cjk_30_0_bin_start[] asm("_binary_cjk_30_0_bin_start");
extern const uint8_t cjk_30_0_bin_end[] asm("_binary_cjk_30_0_bin_end");
extern const uint8_t cjk_30_1_bin_start[] asm("_binary_cjk_30_1_bin_start");
extern const uint8_t cjk_30_1_bin_end[] asm("_binary_cjk_30_1_bin_end");
extern const uint8_t cjk_30_2_bin_start[] asm("_binary_cjk_30_2_bin_start");
extern const uint8_t cjk_30_2_bin_end[] asm("_binary_cjk_30_2_bin_end");
extern const uint8_t cjk_30_3_bin_start[] asm("_binary_cjk_30_3_bin_start");
extern const uint8_t cjk_30_3_bin_end[] asm("_binary_cjk_30_3_bin_end");
extern const uint8_t cjk_30_4_bin_start[] asm("_binary_cjk_30_4_bin_start");
extern const uint8_t cjk_30_4_bin_end[] asm("_binary_cjk_30_4_bin_end");

typedef struct {
    int size_px;
    int part_count;
    const uint8_t *start[FONT_PARTS_MAX];
    const uint8_t *end[FONT_PARTS_MAX];
} font_pack_t;

static const font_pack_t g_font_packs[FONT_SIZE_COUNT] = {
    { 22, 3, {
        cjk_22_0_bin_start, cjk_22_1_bin_start, cjk_22_2_bin_start, NULL, NULL
      }, {
        cjk_22_0_bin_end, cjk_22_1_bin_end, cjk_22_2_bin_end, NULL, NULL
      } },
    { 26, 4, {
        cjk_26_0_bin_start, cjk_26_1_bin_start, cjk_26_2_bin_start, cjk_26_3_bin_start, NULL
      }, {
        cjk_26_0_bin_end, cjk_26_1_bin_end, cjk_26_2_bin_end, cjk_26_3_bin_end, NULL
      } },
    { 30, 5, {
        cjk_30_0_bin_start, cjk_30_1_bin_start, cjk_30_2_bin_start, cjk_30_3_bin_start, cjk_30_4_bin_start
      }, {
        cjk_30_0_bin_end, cjk_30_1_bin_end, cjk_30_2_bin_end, cjk_30_3_bin_end, cjk_30_4_bin_end
      } },
};
