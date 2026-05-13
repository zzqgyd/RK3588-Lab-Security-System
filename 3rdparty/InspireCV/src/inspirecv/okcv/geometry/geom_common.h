#ifndef OKCV_GEOM_COMMON_H_
#define OKCV_GEOM_COMMON_H_

#include <cstring>  // for std::memcpy
#include <memory>
#include <iostream>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <ostream>
#include <vector>
#include <okcv/base/base.h>

namespace okcv {

/**
 * @brief Check if two floats are near each other.
 * @param x The first float.
 * @param y The second float.
 * @param eps The epsilon value.
 * @return True if the floats are near each other, false otherwise.
 */
inline bool IsNearlyEqual(float x, float y, float eps) {
    float d = x - y;
    return -eps <= d && d <= eps;
}

/**
 * @brief Calculate the square of a number.
 * @param x The number to square.
 * @return The square of the number.
 */
template <typename T>
inline T Square(T x) {
    return x * x;
}

}  // namespace okcv

#endif  // OKCV_GEOM_COMMON_H_
