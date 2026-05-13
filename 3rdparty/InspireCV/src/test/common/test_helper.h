#ifndef INSPIRECV_UTIL_TEST_HELPER_H_
#define INSPIRECV_UTIL_TEST_HELPER_H_

#include <catch2/catch.hpp>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "inspirecv/inspirecv.h"

namespace inspirecv {

template <typename T>
double Diff(const T &a, const T &b) {
    return fabs(a - b);
}

template <typename T>
double Diff(const Point<T> &a, const Point<T> &b) {
    double diff = (a - b).Length();
    return diff;
}

// template <typename T>
// double Diff(const Point3<T> &a, const Point3<T> &b) {
//     double diff = (a - b).Length();
//     return diff;
// }

template <typename T>
bool CheckArrayNear(const T *a, const T *b, size_t size, double eps, std::string &error_msg) {
    const int max_logs = 10;
    std::stringstream ss;
    int count = 0;
    double max_diff = 0;
    for (size_t i = 0; i < size; ++i) {
        double d = Diff(a[i], b[i]);
        if (d > eps) {
            if (count <= max_logs) {
                ss << "a[" << i << "]: " << a[i] << ", b[" << i << "]: " << b[i] << ", diff: " << d
                   << ", eps: " << eps << "\n";
            }
            if (d > max_diff)
                max_diff = d;
            ++count;
        }
    }
    ss << "diff count: " << count << ", max diff: " << max_diff;
    error_msg = ss.str();
    return count == 0;
}

template <typename T>
bool CheckArrayNear(const std::vector<T> &a, const std::vector<T> &b, double eps,
                    std::string &error_msg) {
    std::stringstream ss;
    if (a.size() != b.size()) {
        ss << "a.size: " << a.size() << ", b.size: " << b.size();
        error_msg = ss.str();
        return false;
    }
    return CheckArrayNear(a.data(), b.data(), a.size(), eps, error_msg);
}

template <typename T>
bool CheckArrayEqual(const T *a, const T *b, size_t size, std::string &error_msg) {
    return CheckArrayNear(a, b, size, 0, error_msg);
}

template <typename T>
bool CheckArrayEqual(const std::vector<T> &a, const std::vector<T> &b, std::string &error_msg) {
    return CheckArrayNear(a, b, 0, error_msg);
}

}  // namespace inspirecv

#define REQUIRE_NEAR_C_ARRAY(a, b, size, eps)                     \
    do {                                                          \
        std::string msg;                                          \
        REQUIRE(inspirecv::CheckArrayNear(a, b, size, eps, msg)); \
        if (!msg.empty()) {                                       \
            INFO(msg);                                            \
        }                                                         \
    } while (0)

#define REQUIRE_NEAR_ARRAY(a, b, eps)                       \
    do {                                                    \
        std::string msg;                                    \
        REQUIRE(inspirecv::CheckArrayNear(a, b, eps, msg)); \
        if (!msg.empty()) {                                 \
            INFO(msg);                                      \
        }                                                   \
    } while (0)

#define REQUIRE_EQ_C_ARRAY(a, b, size)                        \
    do {                                                      \
        std::string msg;                                      \
        REQUIRE(inspirecv::CheckArrayEqual(a, b, size, msg)); \
        if (!msg.empty()) {                                   \
            INFO(msg);                                        \
        }                                                     \
    } while (0)

#define REQUIRE_EQ_ARRAY(a, b)                          \
    do {                                                \
        std::string msg;                                \
        REQUIRE(inspirecv::CheckArrayEqual(a, b, msg)); \
        if (!msg.empty()) {                             \
            INFO(msg);                                  \
        }                                               \
    } while (0)

#define REQUIRE_NEAR(a, b, eps)                                                         \
    do {                                                                                \
        double diff = okcv::Diff(a, b);                                                 \
        REQUIRE(diff <= eps);                                                           \
        if (diff > eps) {                                                               \
            std::stringstream ss;                                                       \
            ss << "a: " << a << ", b: " << b << ", diff: " << diff << ", eps: " << eps; \
            INFO(ss.str());                                                             \
        }                                                                               \
    } while (0)

#endif  // INSPIRECV_UTIL_TEST_HELPER_H_
