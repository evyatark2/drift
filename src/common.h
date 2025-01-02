#ifndef DRIFT_COMMON_H
#define DRIFT_COMMON_H

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "vulkan.h"

struct DriftConfig {
  bool force_aa;
  bool force_disable_fog;
  bool enable_pp;
  bool ignore_frame_skips;
  const char *force_resolution;
};

extern struct DriftConfig DRIFT_CONFIG;

enum LogLevel {
    LEVEL_TRACE,
    LEVEL_DEBUG,
    LEVEL_INFO,
    LEVEL_WARNING,
    LEVEL_ERROR
};

//char buf[4096];
//int len = sprintf_s(buf, 4096, "[%s] (%d) %s(): ", strlevel(level), GetCurrentThreadId(), __func__);
//sprintf_s(buf + len, 4096 - len, (format), ##__VA_ARGS__);
//OutputDebugString(buf);

extern FILE *log_file;

#define LOG(level, format, ...) \
    do { \
        if (level > LEVEL_TRACE) { \
            fprintf(log_file, "[%s] %s:%d(): ", strlevel(level), __func__, __LINE__); \
            fprintf(log_file, (format), ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_CALL() LOG(LEVEL_TRACE, "Called\n")

static void log_init()
{
    log_file = fopen("drift_log.txt", "w");
}

static inline const char *strlevel(enum LogLevel level)
{
    switch (level) {
    case LEVEL_TRACE:
        return "TRACE";

    case LEVEL_DEBUG:
        return "DEBUG";

    case LEVEL_INFO:
        return "INFO";

    case LEVEL_WARNING:
        return "WARNING";

    case LEVEL_ERROR:
        return "ERROR";
    }

    return "";
}

static inline const char *dr_vulkan_strerror(VkResult res);

#define CHECK_VULKAN_RESULT(call) do { \
    VkResult res = call; \
    if (res < VK_SUCCESS) { \
        LOG(LEVEL_ERROR, "Vulkan error %d: %s (%s)\n", res, dr_vulkan_strerror(res), #call); \
        exit(res); \
    } \
} while (0)

static inline const char *dr_vulkan_strerror(VkResult res)
{
    switch (res) {
    case VK_SUCCESS:
        return "No error";

    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "Out of memory";

    default:
        return "Unrecognized Vulkan error";
    }

    //return "Unrecognized Vulkan error";
}

#endif

