#ifndef INSPIRECV_IMPL_OKCV_TRANSFORM_MATRIX_OKCV_H
#define INSPIRECV_IMPL_OKCV_TRANSFORM_MATRIX_OKCV_H

#include "okcv/okcv.h"
#include "inspirecv/core/transform_matrix.h"

namespace inspirecv {
class TransformMatrix::Impl {
public:
    Impl(const Impl &other) : matrix_(other.matrix_) {}
    Impl(const okcv::TransformMatrix &matrix) : matrix_(matrix) {}
    Impl() {
        matrix_ = okcv::TransformMatrix::Identity();
    }
    Impl(float a11, float a12, float b1, float a21, float a22, float b2) {
        matrix_ = okcv::TransformMatrix({a11, a12, b1, a21, a22, b2});
    }
    ~Impl() = default;

    // Internal methods
    static std::unique_ptr<Impl> CreateFromOKCV(const okcv::TransformMatrix &matrix) {
        return std::make_unique<Impl>(matrix);
    }

    // Basic getters and setters
    const okcv::TransformMatrix &GetNative() const {
        return matrix_;
    }
    okcv::TransformMatrix &GetNative() {
        return matrix_;
    }

    // Basic getters and setters
    float Get(int row, int col) const {
        return matrix_[row * 3 + col];
    }
    void Set(int row, int col, float value) {
        matrix_[row * 3 + col] = value;
    }

    // Squeeze the matrix into a vector
    std::vector<float> Squeeze() const {
        std::vector<float> result;
        for (int i = 0; i < 6; i++) {
            result.push_back(matrix_[i]);
        }
        return result;
    }

    // Operator overloading for array-style access
    float operator[](int index) const {
        return matrix_[index];
    }

    float &operator[](int index) {
        return matrix_[index];
    }

    void *GetInternalMatrix() const {
        return (void *)(&matrix_);
    }

    // Basic operations
    bool IsIdentity() const {
        return matrix_.IsIdentity();
    }

    void SetIdentity() {
        matrix_ = okcv::TransformMatrix::Identity();
    }
    void Invert() {
        matrix_ = matrix_.Inv();
    }
    TransformMatrix GetInverse() const {
        auto result = matrix_.Inv();
        return TransformMatrix(result[0], result[1], result[2], result[3], result[4], result[5]);
    }

    // Transform operations
    void Translate(float dx, float dy) {
        okcv::TransformMatrix trans({1.0f, 0.0f, dx, 0.0f, 1.0f, dy});
        matrix_ = trans;
    }

    void Scale(float sx, float sy) {
        okcv::TransformMatrix scale({sx, 0.0f, 0.0f, 0.0f, sy, 0.0f});
        matrix_ = scale;
    }

    void Rotate(float angle) {
        float rad = angle * M_PI / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        okcv::TransformMatrix rot({c, -s, 0.0f, s, c, 0.0f});
        matrix_ = rot;
    }

    // Matrix operations
    TransformMatrix Multiply(const TransformMatrix &other) const {
        auto okcv_other = *static_cast<const okcv::TransformMatrix *>(other.GetInternalMatrix());
        okcv::TransformMatrix result;
        const float *a = matrix_.Data();
        const float *b = okcv_other.Data();
        float *c = result.Data();
        // Perform 2x3 matrix multiplication
        c[0] = a[0] * b[0] + a[1] * b[3];
        c[1] = a[0] * b[1] + a[1] * b[4];
        c[2] = a[0] * b[2] + a[1] * b[5] + a[2];
        c[3] = a[3] * b[0] + a[4] * b[3];
        c[4] = a[3] * b[1] + a[4] * b[4];
        c[5] = a[3] * b[2] + a[4] * b[5] + a[5];
        return TransformMatrix(result[0], result[1], result[2], result[3], result[4], result[5]);
    }

    TransformMatrix Clone() const {
        return TransformMatrix(matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4],
                               matrix_[5]);
    }

    static TransformMatrix Create() {
        auto identity = okcv::TransformMatrix::Identity();
        return TransformMatrix(identity[0], identity[1], identity[2], identity[3], identity[4],
                               identity[5]);
    }

    static TransformMatrix Create(float a11, float a12, float b1, float a21, float a22, float b2) {
        return TransformMatrix(a11, a12, b1, a21, a22, b2);
    }

private:
    okcv::TransformMatrix matrix_;
};
}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OKCV_TRANSFORM_MATRIX_OKCV_H
