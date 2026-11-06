#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 时间是否已经通过 SNTP 校时（未校时则只有开机计时） */
bool clock_is_synced(void);

/* 取本地时间（CST-8） */
void clock_now(struct tm *out);

/* "14:23" 或 "14:23:08" */
void clock_format_time(char *buf, unsigned len, bool with_seconds);

/* "2026-11-06 周五" */
void clock_format_date(char *buf, unsigned len);

#ifdef __cplusplus
}
#endif
