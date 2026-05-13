#ifndef INSPIRECV_DEFINE_H
#define INSPIRECV_DEFINE_H

#include <array>

#ifndef INSPIRECV_API
#define INSPIRECV_API
#endif

namespace inspirecv {

template <typename T, size_t N>
using Vec = std::array<T, N>;

template <typename T>
using Vec2 = Vec<T, 2>;
template <typename T>
using Vec3 = Vec<T, 3>;
template <typename T>
using Vec4 = Vec<T, 4>;

// Float vectors
using Vec2f = Vec<float, 2>;
using Vec3f = Vec<float, 3>;
using Vec4f = Vec<float, 4>;

// Double vectors
using Vec2d = Vec<double, 2>;
using Vec3d = Vec<double, 3>;
using Vec4d = Vec<double, 4>;

// Integer vectors
using Vec2i = Vec<int, 2>;
using Vec3i = Vec<int, 3>;
using Vec4i = Vec<int, 4>;

}  // namespace inspirecv

#endif  // INSPIRECV_DEFINE_H
