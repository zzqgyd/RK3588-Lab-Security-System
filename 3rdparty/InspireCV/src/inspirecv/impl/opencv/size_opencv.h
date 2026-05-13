#ifndef INSPIRECV_IMPL_OPENCV_SIZE_OPENCV_H
#define INSPIRECV_IMPL_OPENCV_SIZE_OPENCV_H

#include "opencv2/opencv.hpp"
#include "inspirecv/core/size.h"

namespace inspirecv {
template <typename T>
class Size<T>::Impl {
public:
    Impl(const Impl &other) : size_(other.size_) {}
    Impl(T width, T height) : size_(width, height) {}
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOpenCV(const cv::Size_<T> &size) {
        return std::make_unique<Impl>(size.width, size.height);
    }

    // Basic getters and setters
    T GetWidth() const {
        return size_.width;
    }
    T GetHeight() const {
        return size_.height;
    }
    void SetWidth(T width) {
        size_.width = width;
    }
    void SetHeight(T height) {
        size_.height = height;
    }

    // Basic operations
    T Area() const {
        return size_.width * size_.height;
    }
    bool Empty() const {
        return size_.width == 0 || size_.height == 0;
    }
    void Scale(T sx, T sy) {
        size_.width *= sx;
        size_.height *= sy;
    }

    // Factory method
    static std::unique_ptr<Impl> Create(T width, T height) {
        return std::make_unique<Impl>(width, height);
    }

private:
    cv::Size_<T> size_;
};

}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OPENCV_SIZE_OPENCV_H
