#pragma once

#include <hilog/log.h>

#ifndef LOG_TAG
#define LOG_TAG "loafcore"
#endif

#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, LOG_TAG, __VA_ARGS__)
#define LOGW(...) OH_LOG_Print(LOG_APP, LOG_WARN, 0x0000, LOG_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, LOG_TAG, __VA_ARGS__)
