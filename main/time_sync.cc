#include "time_sync.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/time.h>
#include <time.h>

#define TAG "TimeSync"

namespace {

std::string FormatPosixTimezone(int offset_minutes)
{
    const char sign = offset_minutes >= 0 ? '-' : '+';
    const int abs_offset = offset_minutes >= 0 ? offset_minutes : -offset_minutes;
    const int hours = abs_offset / 60;
    const int minutes = abs_offset % 60;

    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%c%02d:%02d", sign, hours, minutes);
    return tz;
}

void ApplyTimezoneOffsetIfPresent(const cJSON* server_time)
{
    const cJSON* timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
    if (!cJSON_IsNumber(timezone_offset)) {
        return;
    }

    const int offset_minutes = timezone_offset->valueint;
    const std::string tz = FormatPosixTimezone(offset_minutes);
    setenv("TZ", tz.c_str(), 1);
    tzset();
}

} // namespace

bool SyncServerTimeFromJson(const cJSON* server_time, const char* source)
{
    if (!cJSON_IsObject(server_time)) {
        ESP_LOGW(TAG, "Invalid %s time_sync message: missing server_time", source);
        return false;
    }

    const cJSON* timestamp = cJSON_GetObjectItem(server_time, "timestamp");
    if (!cJSON_IsNumber(timestamp)) {
        ESP_LOGW(TAG, "Invalid %s time_sync message: missing timestamp", source);
        return false;
    }

    const int64_t timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
    if (timestamp_ms <= 0) {
        ESP_LOGW(TAG, "Invalid %s time_sync timestamp: sec=%ld ms_part=%03ld",
            source,
            static_cast<long>(timestamp_ms / 1000),
            static_cast<long>(timestamp_ms % 1000));
        return false;
    }

    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(timestamp_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timestamp_ms % 1000) * 1000);
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(TAG, "Failed to set system time from %s time_sync: %s", source, strerror(errno));
        return false;
    }

    ApplyTimezoneOffsetIfPresent(server_time);
    ESP_LOGI(TAG, "%s system time synced: sec=%ld ms_part=%03ld",
        source,
        static_cast<long>(timestamp_ms / 1000),
        static_cast<long>(timestamp_ms % 1000));
    return true;
}
