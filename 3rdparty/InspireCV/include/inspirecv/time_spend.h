#ifndef INSPIRECV_TIME_SPEND_H_
#define INSPIRECV_TIME_SPEND_H_

#include <cstdint>
#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>
#ifndef INSPIRECV_API
#define INSPIRECV_API
#endif

namespace inspirecv {

// Get the current time in microseconds.
uint64_t INSPIRECV_API _now();

/**
 * @brief A class to measure the cost of a block of code.
 */
class INSPIRECV_API TimeSpend {
public:
    TimeSpend() {
        Reset();
    }

    explicit TimeSpend(const std::string &name) {
        name_ = name;
        Reset();
    }

    void Start() {
        start_ = _now();
    }

    void Stop() {
        stop_ = _now();
        uint64_t d = stop_ - start_;
        total_ += d;
        ++count_;
        min_ = std::min(min_, d);
        max_ = std::max(max_, d);
    }

    void Reset() {
        start_ = 0;
        stop_ = 0;
        total_ = 0;
        count_ = 0;
        min_ = UINT64_MAX;
        max_ = 0;
    }

    inline uint64_t Get() const {
        return stop_ - start_;
    }

    inline uint64_t Average() const {
        return count_ == 0 ? 0 : total_ / count_;
    }

    inline uint64_t Total() const {
        return total_;
    }

    inline uint64_t Count() const {
        return count_;
    }

    inline uint64_t Min() const {
        return count_ == 0 ? 0 : min_;
    }

    inline uint64_t Max() const {
        return max_;
    }

    const std::string &name() const {
        return name_;
    }

    std::string Report() const {
        std::stringstream ss;
        if (is_enable) {
            ss << "[Time(us) total:" << Total() << " ave:" << Average() << " min:" << Min()
               << " max:" << Max() << " count:" << Count() << " " << name_ << "]";
        } else {
            ss << "Timer Disabled.";
        }
        return ss.str();
    }

    static void Disable() {
        is_enable = false;
    }

protected:
    uint64_t start_;
    uint64_t stop_;
    uint64_t total_;
    uint64_t count_;
    uint64_t min_;
    uint64_t max_;

    std::string name_;

    static int is_enable;
};

INSPIRECV_API std::ostream &operator<<(std::ostream &os, const TimeSpend &timer);

}  // namespace inspirecv

#define TIME_NOW inspirecv::_now()

#endif  // INSPIRECV_TIME_SPEND_H_