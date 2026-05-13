#ifndef INSPIRECV_IMPL_OKCV_SIZE_OKCV_H
#define INSPIRECV_IMPL_OKCV_SIZE_OKCV_H

#include "okcv/okcv.h"
#include "inspirecv/core/size.h"

namespace inspirecv {
template <typename T>
class Size<T>::Impl {
public:
    Impl(const Impl &other) : size_(other.size_) {}
    Impl(T width, T height) : size_(width, height) {}
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOKCV(const okcv::Size<T> &size) {
        return std::make_unique<Impl>(size.Width(), size.Height());
    }

    // Basic getters and setters
    T GetWidth() const {
        return size_.Width();
    }
    T GetHeight() const {
        return size_.Height();
    }
    void SetWidth(T width) {
        size_.SetWidth(width);
    }
    void SetHeight(T height) {
        size_.SetHeight(height);
    }

    // Basic operations
    T Area() const {
        return size_.GetArea();
    }
    bool Empty() const {
        return size_.IsEmpty();
    }
    void Scale(T sx, T sy) {
        size_.Scale(sx, sy);
    }

    // Factory method
    static std::unique_ptr<Impl> Create(T width, T height) {
        return std::make_unique<Impl>(width, height);
    }

private:
    okcv::Size<T> size_;
};
}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OKCV_SIZE_OKCV_H
