#include <memory>
#include <cmath>
#include "impl.h"

namespace inspirecv {
template <typename T>
Size<T>::Size(const Size &other) : impl_(std::make_unique<Impl>(*other.impl_)) {}

template <typename T>
Size<T> &Size<T>::operator=(const Size &other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

template <typename T>
Size<T>::Size() : impl_(std::make_unique<Impl>(0, 0)) {}

template <typename T>
Size<T>::Size(T width, T height) : impl_(std::make_unique<Impl>(width, height)) {}

template <typename T>
Size<T>::~Size() = default;

template <typename T>
T Size<T>::GetWidth() const {
    return impl_->GetWidth();
}

template <typename T>
T Size<T>::GetHeight() const {
    return impl_->GetHeight();
}

template <typename T>
void Size<T>::SetWidth(T width) {
    impl_->SetWidth(width);
}

template <typename T>
void Size<T>::SetHeight(T height) {
    impl_->SetHeight(height);
}

template <typename T>
T Size<T>::Area() const {
    return impl_->Area();
}

template <typename T>
bool Size<T>::Empty() const {
    return impl_->Empty();
}

template <typename T>
void Size<T>::Scale(T sx, T sy) {
    impl_->Scale(sx, sy);
}

template <typename T>
Size<T> Size<T>::Create(T width, T height) {
    return Size<T>(width, height);
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const Size<T> &size) {
    os << "Size[" << size.GetWidth() << " x " << size.GetHeight() << "]";
    return os;
}

template class Size<int>;
template class Size<float>;
template class Size<double>;

template std::ostream &operator<<(std::ostream &os, const Size<int> &size);
template std::ostream &operator<<(std::ostream &os, const Size<float> &size);
template std::ostream &operator<<(std::ostream &os, const Size<double> &size);

}  // namespace inspirecv
