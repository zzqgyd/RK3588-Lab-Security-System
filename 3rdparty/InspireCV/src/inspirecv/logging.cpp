
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "logging.h"

#if defined(ANDROID) || defined(__ANDROID__)
#include <android/log.h>
#endif

namespace inspirecv {
namespace logging {

#ifndef INSPIRECV_LOG_LEVEL
#define INSPIRECV_LOG_LEVEL 0
#endif

int ISFLogging::vlog_level_ = INSPIRECV_LOG_LEVEL;

int ISFLogging::VLogLevel() {
    return vlog_level_;
}

void ISFLogging::VLogSetLevel(int level) {
    vlog_level_ = level;
}

ISFLogging::~ISFLogging() {
    if (severity_ >= LogSeverity::INFO) {
#if defined(_WIN32)
        char sep = '\\';
#else
        char sep = '/';
#endif
        const char *const partial_name = strrchr(filename_, sep);
        std::stringstream ss;
        ss << "IWEF"[static_cast<int>(severity_)] << ' '
           << (partial_name != nullptr ? partial_name + 1 : filename_) << ':' << line_ << "] "
           << stream_.str();
        std::cerr << ss.str() << std::endl;

#if defined(ANDROID) || defined(__ANDROID__)
        int android_log_level;
        switch (severity_) {
            case LogSeverity::INFO:
                android_log_level = ANDROID_LOG_INFO;
                break;
            case LogSeverity::WARN:
                android_log_level = ANDROID_LOG_WARN;
                break;
            case LogSeverity::ERROR:
                android_log_level = ANDROID_LOG_ERROR;
                break;
            case LogSeverity::FATAL:
                android_log_level = ANDROID_LOG_FATAL;
                break;
        }
        __android_log_write(android_log_level, "InspireCV-LOG", ss.str().c_str());
#endif

        if (severity_ == LogSeverity::FATAL) {
            std::flush(std::cerr);
            std::abort();
        }
    }
}

}  // namespace logging
}  // namespace inspirecv
