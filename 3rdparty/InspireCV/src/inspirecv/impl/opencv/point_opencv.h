#ifndef INSPIRECV_IMPL_OPENCV_POINT_OPENCV_H
#define INSPIRECV_IMPL_OPENCV_POINT_OPENCV_H
#include <memory>
#include "inspirecv/core/point.h"
#include "opencv2/opencv.hpp"

namespace inspirecv {
template <typename T>
class Point<T>::Impl {
public:
    Impl(T x, T y) : point_(x, y) {}
    ~Impl() = default;

    T GetX() const {
        return point_.x;
    }
    T GetY() const {
        return point_.y;
    }

    void SetX(T x) {
        point_.x = x;
    }
    void SetY(T y) {
        point_.y = y;
    }

    void *GetInternalPoint() const {
        return (void *)&point_;
    }

    double Length() const {
        return std::sqrt(static_cast<double>(point_.x * point_.x + point_.y * point_.y));
    }

    double Distance(const Impl &other) const {
        T dx = GetX() - other.GetX();
        T dy = GetY() - other.GetY();
        return std::sqrt(static_cast<double>(dx * dx + dy * dy));
    }

    T Dot(const Impl &other) const {
        return GetX() * other.GetX() + GetY() * other.GetY();
    }

    T Cross(const Impl &other) const {
        return GetX() * other.GetY() - GetY() * other.GetX();
    }

    const cv::Point_<T> &GetNative() const {
        return point_;
    }
    cv::Point_<T> &GetNative() {
        return point_;
    }

    static std::unique_ptr<Impl> CreateFromOpenCV(const cv::Point_<T> &point) {
        return std::make_unique<Impl>(point.x, point.y);
    }

private:
    explicit Impl(const cv::Point_<T> &point) : point_(point) {}

private:
    cv::Point_<T> point_;

    // Friend class
    friend class Point<T>;
};  // class Impl

template <typename T>
inline Point<T> Point<T>::Create(T x, T y) {
    return Point<T>(x, y);
}

}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OPENCV_POINT_OPENCV_H