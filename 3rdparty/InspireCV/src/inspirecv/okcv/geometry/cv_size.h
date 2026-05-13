#ifndef OKCV_GEOM_SIZE_H_
#define OKCV_GEOM_SIZE_H_

#include <okcv/geometry/geom_common.h>

namespace okcv {

/**
 * @brief A class representing a size with width and height.
 * @tparam T The type of the dimensions.
 */
template <typename T>
class Size {
public:
    Size() : width_(0), height_(0) {}
    Size(const T &width, const T &height) : width_(width), height_(height) {}

    template <typename S>
    inline Size<S> As() const {
        return Size<S>(width_, height_);
    }

    inline void Set(T width, T height) {
        width_ = width;
        height_ = height;
    }

    inline T GetArea() const {
        return width_ * height_;
    }

    inline bool IsEmpty() const {
        return width_ <= 0 || height_ <= 0;
    }

    inline void Scale(float scale_x, float scale_y) {
        width_ *= scale_x;
        height_ *= scale_y;
    }

    inline void Scale(float scale) {
        Scale(scale, scale);
    }

    inline Size<int> Round() const {
        return Size<int>(std::round(width_), std::round(height_));
    }

    T Width() const {
        return width_;
    }
    T Height() const {
        return height_;
    }

    inline void SetWidth(const T &width) {
        width_ = width;
    }
    inline void SetHeight(const T &height) {
        height_ = height;
    }

private:
    T width_;
    T height_;
};

/**
 * @brief Outputs a size to a stream.
 * @tparam T The type of the dimensions.
 * @param stream The stream to output to.
 * @param size The size to output.
 * @return The stream.
 */
template <typename T>
inline std::ostream &operator<<(std::ostream &stream, const Size<T> &size) {
    stream << "[" << size.Width() << "x" << size.height() << "]";
    return stream;
}

typedef Size<int> Size2i;
typedef Size<float> Size2f;
typedef Size<double> Size2d;

}  // namespace okcv

#endif  // OKCV_GEOM_SIZE_H_