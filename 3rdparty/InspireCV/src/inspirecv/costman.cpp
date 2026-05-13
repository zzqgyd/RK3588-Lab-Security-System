#include "time_spend.h"
#include <ostream>

#if defined(_MSC_VER)
#include <chrono>  // NOLINT
#else
#include <sys/time.h>
#endif

namespace inspirecv {

#if defined(_MSC_VER)

uint64_t INSPIRECV_API _now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

#else

uint64_t INSPIRECV_API _now() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

#endif  // defined(_MSC_VER)

int TimeSpend::is_enable = true;

std::ostream &INSPIRECV_API operator<<(std::ostream &os, const TimeSpend &timer) {
    os << timer.Report();
    return os;
}

}  // namespace inspirecv
