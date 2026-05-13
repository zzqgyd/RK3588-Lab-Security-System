#include "cv_transform_matrix.h"
#include "check.h"
namespace okcv {

TransformMatrix::TransformMatrix(const std::initializer_list<float> &v) {
    INSPIRECV_CHECK_EQ(v.size(), 6);
    int i = 0;
    for (const auto &d : v)
        data_[i++] = d;
}

TransformMatrix TransformMatrix::Inv() const {
    TransformMatrix inv;
    float det = data_[0] * data_[4] - data_[1] * data_[3];
    INSPIRECV_CHECK_NE(det, 0) << *this;
    inv[0] = data_[4] / det;
    inv[1] = -data_[1] / det;
    inv[2] = (data_[1] * data_[5] - data_[2] * data_[4]) / det;
    inv[3] = -data_[3] / det;
    inv[4] = data_[0] / det;
    inv[5] = -(data_[0] * data_[5] - data_[2] * data_[3]) / det;
    return inv;
}

bool TransformMatrix::IsIdentity(float eps) const {
    return IsNearlyEqual(data_[0], 1.f, eps) && IsNearlyEqual(data_[1], 0.f, eps) &&
           IsNearlyEqual(data_[2], 0.f, eps) && IsNearlyEqual(data_[3], 0.f, eps) &&
           IsNearlyEqual(data_[4], 1.f, eps) && IsNearlyEqual(data_[5], 0.f, eps);
}

bool TransformMatrix::IsCrop(float eps) const {
    return IsNearlyEqual(data_[0], 1.f, eps) && IsNearlyEqual(data_[1], 0.f, eps) &&
           IsNearlyEqual(data_[2], 0.f, eps) && IsNearlyEqual(data_[4], 1.f, eps);
}

bool TransformMatrix::IsResize(float eps) const {
    return IsNearlyEqual(data_[1], 0.f, eps) && IsNearlyEqual(data_[2], 0.f, eps) &&
           IsNearlyEqual(data_[3], 0.f, eps) && IsNearlyEqual(data_[5], 0.f, eps);
}

bool TransformMatrix::IsCropAndResize(float eps) const {
    return IsNearlyEqual(data_[1], 0.f, eps) && IsNearlyEqual(data_[3], 0.f, eps);
}

}  // namespace okcv
