#include "clock.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_sntp.h"

/* SNTP 校时完成的判定阈值：2020-09-13 之后的 Unix 时间戳 */
#define CLOCK_VALID_AFTER 1600000000L

static const char *const WEEKDAYS[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

bool clock_is_synced(void)
{
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        return true;
    }
    return time(NULL) > CLOCK_VALID_AFTER;
}

void clock_now(struct tm *out)
{
    if (!out) {
        return;
    }
    time_t now = time(NULL);
    if (now < CLOCK_VALID_AFTER) {
        /* 未校时时显示开机计时，而不是 1970 年 */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = (time_t)tv.tv_sec;
    }
    localtime_r(&now, out);
}

void clock_format_time(char *buf, unsigned len, bool with_seconds)
{
    if (!buf || len == 0) {
        return;
    }
    struct tm tm_now;
    clock_now(&tm_now);
    if (clock_is_synced()) {
        if (with_seconds) {
            snprintf(buf, len, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        } else {
            snprintf(buf, len, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        }
    } else {
        /* 未校时：用 mm:ss 显示已运行时长，便于确认设备在工作 */
        snprintf(buf, len, "--:--");
    }
}

void clock_format_date(char *buf, unsigned len)
{
    if (!buf || len == 0) {
        return;
    }
    if (!clock_is_synced()) {
        snprintf(buf, len, "未校时");
        return;
    }
    struct tm tm_now;
    clock_now(&tm_now);
    snprintf(buf, len, "%04d-%02d-%02d %s", tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             WEEKDAYS[tm_now.tm_wday % 7]);
}
