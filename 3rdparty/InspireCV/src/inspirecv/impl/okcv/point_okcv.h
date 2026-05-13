#ifndef INSPIRECV_IMPL_OKCV_POINT_OKCV_H
#define INSPIRECV_IMPL_OKCV_POINT_OKCV_H
#include <memory>
#include <vector>
#include "inspirecv/core/point.h"
#include "okcv/okcv.h"

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
        return point_.Length();
    }

    double Distance(const Impl &other) const {
        return point_.Distance(other.point_);
    }

    T Dot(const Impl &other) const {
        return point_.Dot(other.point_);
    }
    T Cross(const Impl &other) const {
        return point_.Cross(other.point_);
    }

    const okcv::Point<T> &GetNative() const {
        return point_;
    }
    okcv::Point<T> &GetNative() {
        return point_;
    }

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOKCV(const okcv::Point<T> &point) {
        return std::make_unique<Impl>(point.x, point.y);
    }



private:
    explicit Impl(const okcv::Point<T> &point) : point_(point) {}

private:
    okcv::Point<T> point_;

    // Friend class
    friend class Point<T>;
};

template <typename T>
inline Point<T> Point<T>::Create(T x, T y) {
    return Point<T>(x, y);
}

}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OKCV_POINT_OKCV_H