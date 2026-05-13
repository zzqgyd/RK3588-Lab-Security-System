#include "impl.h"

namespace inspirecv {

TransformMatrix::TransformMatrix() : impl_(std::make_unique<Impl>()) {}

TransformMatrix::TransformMatrix(const TransformMatrix &other)
: impl_(std::make_unique<Impl>(*other.impl_)) {}

TransformMatrix::TransformMatrix(float a11, float a12, float b1, float a21, float a22, float b2)
: impl_(std::make_unique<Impl>(a11, a12, b1, a21, a22, b2)) {}

TransformMatrix::~TransformMatrix() = default;

TransformMatrix &TransformMatrix::operator=(const TransformMatrix &other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

float TransformMatrix::Get(int row, int col) const {
    return impl_->Get(row, col);
}

void TransformMatrix::Set(int row, int col, float value) {
    impl_->Set(row, col, value);
}

std::vector<float> TransformMatrix::Squeeze() const {
    return impl_->Squeeze();
}

float TransformMatrix::operator[](int index) const {
    return (*impl_)[index];
}

float &TransformMatrix::operator[](int index) {
    return (*impl_)[index];
}

void *TransformMatrix::GetInternalMatrix() const {
    return impl_->GetInternalMatrix();
}

bool TransformMatrix::IsIdentity() const {
    return impl_->IsIdentity();
}

void TransformMatrix::SetIdentity() {
    impl_->SetIdentity();
}

void TransformMatrix::Invert() {
    impl_->Invert();
}

TransformMatrix TransformMatrix::GetInverse() const {
    auto inv = *this;
    inv.Invert();
    return inv;
}

void TransformMatrix::Translate(float dx, float dy) {
    impl_->Translate(dx, dy);
}

void TransformMatrix::Scale(float sx, float sy) {
    impl_->Scale(sx, sy);
}

void TransformMatrix::Rotate(float angle) {
    impl_->Rotate(angle);
}

TransformMatrix TransformMatrix::Multiply(const TransformMatrix &other) const {
    return impl_->Multiply(other);
}

TransformMatrix TransformMatrix::Clone() const {
    return impl_->Clone();
}

TransformMatrix TransformMatrix::Create() {
    return Impl::Create();
}

TransformMatrix TransformMatrix::Create(float a11, float a12, float b1, float a21, float a22,
                                        float b2) {
    return Impl::Create(a11, a12, b1, a21, a22, b2);
}

TransformMatrix TransformMatrix::Identity() {
    return Impl::Create();
}

TransformMatrix TransformMatrix::Rotate90() {
    auto rot = Impl::Create();
    rot.Rotate(90.0f);
    return rot;
}

TransformMatrix TransformMatrix::Rotate180() {
    auto rot = Impl::Create();
    rot.Rotate(180.0f);
    return rot;
}

TransformMatrix TransformMatrix::Rotate270() {
    auto rot = Impl::Create();
    rot.Rotate(270.0f);
    return rot;
}

std::ostream &operator<<(std::ostream &os, const TransformMatrix &matrix) {
    os << "TransformMatrix [[" << matrix.Get(0, 0) << ", " << matrix.Get(0, 1) << ", "
       << matrix.Get(0, 2) << "],\n\t\t [" << matrix.Get(1, 0) << ", " << matrix.Get(1, 1) << ", "
       << matrix.Get(1, 2) << "]]";
    return os;
}

}  // namespace inspirecv
