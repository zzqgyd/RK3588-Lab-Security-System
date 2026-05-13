#include <memory>
#include <cmath>
#include "core/rect.h"
#include "impl.h"

namespace inspirecv {

template <typename T>
Rect<T>::Rect(const Rect &other) : impl_(std::make_unique<Impl>(*other.impl_)) {}

template <typename T>
Rect<T> &Rect<T>::operator=(const Rect &other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

template <typename T>
Rect<T>::Rect() : impl_(std::make_unique<Impl>(0, 0, 0, 0)) {}

template <typename T>
Rect<T>::Rect(T x, T y, T width, T height) : impl_(std::make_unique<Impl>(x, y, width, height)) {}

template <typename T>
Rect<T>::~Rect() = default;

template <typename T>
T Rect<T>::GetX() const {
    return impl_->GetX();
}

template <typename T>
T Rect<T>::GetY() const {
    return impl_->GetY();
}

template <typename T>
T Rect<T>::GetWidth() const {
    return impl_->GetWidth();
}

template <typename T>
T Rect<T>::GetHeight() const {
    return impl_->GetHeight();
}

template <typename T>
void Rect<T>::SetX(T x) {
    impl_->SetX(x);
}

template <typename T>
void Rect<T>::SetY(T y) {
    impl_->SetY(y);
}

template <typename T>
void Rect<T>::SetWidth(T width) {
    impl_->SetWidth(width);
}

template <typename T>
void Rect<T>::SetHeight(T height) {
    impl_->SetHeight(height);
}

template <typename T>
void *Rect<T>::GetInternalRect() const {
    return impl_->GetInternalRect();
}

template <typename T>
Point<T> Rect<T>::TopLeft() const {
    return impl_->TopLeft();
}

template <typename T>
Point<T> Rect<T>::TopRight() const {
    return impl_->TopRight();
}

template <typename T>
Point<T> Rect<T>::BottomLeft() const {
    return impl_->BottomLeft();
}

template <typename T>
Point<T> Rect<T>::BottomRight() const {
    return impl_->BottomRight();
}

template <typename T>
Point<T> Rect<T>::Center() const {
    return impl_->Center();
}

template <typename T>
std::vector<Point<T>> Rect<T>::ToFourVertices() const {
    return impl_->ToFourVertices();
}

template <typename T>
Rect<T> Rect<T>::SafeRect(T width, T height) const {
    return impl_->SafeRect(width, height);
}

template <typename T>
T Rect<T>::Area() const {
    return impl_->Area();
}

template <typename T>
bool Rect<T>::Empty() const {
    return impl_->Empty();
}

template <typename T>
bool Rect<T>::Contains(const Point<T> &point) const {
    return impl_->Contains(point);
}

template <typename T>
bool Rect<T>::Contains(const Rect<T> &rect) const {
    return impl_->Contains(rect);
}

template <typename T>
Rect<T> Rect<T>::Intersect(const Rect<T> &other) const {
    return impl_->Intersect(other);
}

template <typename T>
Rect<T> Rect<T>::Union(const Rect<T> &other) const {
    return impl_->Union(other);
}

template <typename T>
double Rect<T>::IoU(const Rect<T> &other) const {
    return impl_->IoU(other);
}

template <typename T>
void Rect<T>::Scale(T sx, T sy) {
    impl_->Scale(sx, sy);
}

template <typename T>
void Rect<T>::Translate(T dx, T dy) {
    impl_->Translate(dx, dy);
}

template <typename T>
Rect<T> Rect<T>::Square(float scale) const {
    return impl_->Square(scale);
}

template <typename T>
Rect<T> Rect<T>::Create(T x, T y, T width, T height) {
    return Rect<T>(x, y, width, height);
}

template <typename T>
Rect<T> Rect<T>::Create(const Point<T> &left_top, const Point<T> &right_bottom) {
    return Rect<T>(left_top.GetX(), left_top.GetY(), right_bottom.GetX() - left_top.GetX(),
                   right_bottom.GetY() - left_top.GetY());
}

template <typename T>
template <typename U>
Rect<U> Rect<T>::As() const {
    return Rect<U>::Create(static_cast<U>(GetX()), static_cast<U>(GetY()),
                           static_cast<U>(GetWidth()), static_cast<U>(GetHeight()));
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const Rect<T> &rect) {
    os << "Rect[" << rect.GetX() << ", " << rect.GetY() << ", " << rect.GetWidth() << " x "
       << rect.GetHeight() << "]";
    return os;
}

template class Rect<int>;
template class Rect<float>;
template class Rect<double>;

template Rect<int> Rect<int>::As<int>() const;
template Rect<float> Rect<int>::As<float>() const;
template Rect<double> Rect<int>::As<double>() const;

template Rect<int> Rect<float>::As<int>() const;
template Rect<float> Rect<float>::As<float>() const;
template Rect<double> Rect<float>::As<double>() const;

template Rect<int> Rect<double>::As<int>() const;
template Rect<float> Rect<double>::As<float>() const;
template Rect<double> Rect<double>::As<double>() const;

template std::ostream &operator<<(std::ostream &os, const Rect<int> &rect);
template std::ostream &operator<<(std::ostream &os, const Rect<float> &rect);
template std::ostream &operator<<(std::ostream &os, const Rect<double> &rect);

template <typename T>
Rect<T> MinBoundingRect(const std::vector<Point<T>> &points) {
    INSPIRECV_CHECK(points.size() >= 2);

    T xmin = points[0].GetX();
    T ymin = points[0].GetY();
    T xmax = xmin;
    T ymax = ymin;
    for (const auto &p : points) {
        xmin = std::min(p.GetX(), xmin);
        ymin = std::min(p.GetY(), ymin);
        xmax = std::max(p.GetX(), xmax);
        ymax = std::max(p.GetY(), ymax);
    }
    return Rect<T>::Create(xmin, ymin, xmax - xmin, ymax - ymin);
}

template <typename T>
Rect<T> ApplyTransformToRect(const Rect<T> &rect, const TransformMatrix &transform) {
    auto vertices = rect.ToFourVertices();
    auto transformed_vertices = ApplyTransformToPoints(vertices, transform);
    return MinBoundingRect(transformed_vertices);
}

template Rect<int> MinBoundingRect(const std::vector<Point<int>> &points);
template Rect<float> MinBoundingRect(const std::vector<Point<float>> &points);
template Rect<double> MinBoundingRect(const std::vector<Point<double>> &points);

template Rect<int> ApplyTransformToRect(const Rect<int> &rect, const TransformMatrix &transform);
template Rect<float> ApplyTransformToRect(const Rect<float> &rect,
                                          const TransformMatrix &transform);
template Rect<double> ApplyTransformToRect(const Rect<double> &rect,
                                           const TransformMatrix &transform);

}  // namespace inspirecv
