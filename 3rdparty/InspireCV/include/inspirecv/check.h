#ifndef INSPIRECV_CHECK_H_
#define INSPIRECV_CHECK_H_
#include "logging.h"

#define INSPIRECV_RETURN_IF_ERROR(...)                 \
    do {                                               \
        const okcv::Status _status = (__VA_ARGS__);    \
        if (!_status.ok()) {                           \
            INSPIRECV_LOG(ERROR) << _status.message(); \
            return _status;                            \
        }                                              \
    } while (0)

#define INSPIRECV_LOG_IF(severity, condition) \
    if (condition)                            \
    INSPIRECV_LOG(severity)

#define INSPIRECV_CHECK(condition) \
    INSPIRECV_LOG_IF(FATAL, !(condition)) << "Check failed: (" #condition ") "

#define INSPIRECV_CHECK_EQ(a, b) INSPIRECV_CHECK((a) == (b))
#define INSPIRECV_CHECK_NE(a, b) INSPIRECV_CHECK((a) != (b))
#define INSPIRECV_CHECK_LE(a, b) INSPIRECV_CHECK((a) <= (b))
#define INSPIRECV_CHECK_LT(a, b) INSPIRECV_CHECK((a) < (b))
#define INSPIRECV_CHECK_GE(a, b) INSPIRECV_CHECK((a) >= (b))
#define INSPIRECV_CHECK_GT(a, b) INSPIRECV_CHECK((a) > (b))

#endif  // INSPIRECV_CHECK_H_
