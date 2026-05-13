#ifndef OKCV_GEOM_TRANSFORM_MATRIX_H_
#define OKCV_GEOM_TRANSFORM_MATRIX_H_

#include <okcv/geometry/geom_common.h>

namespace okcv {

/**
 * @brief A class representing a 2D transformation matrix.
 */
class OKCV_API TransformMatrix {
public:
    TransformMatrix() = default;
    TransformMatrix(const std::initializer_list<float> &v);

    inline const float &operator[](int index) const {
        return data_[index];
    }

    inline float &operator[](int index) {
        return data_[index];
    }

    TransformMatrix(const TransformMatrix &other) {
        std::memcpy(data_, other.data_, sizeof(data_));
    };

    TransformMatrix &operator=(const TransformMatrix &other) {
        if (this != &other) {
            std::memcpy(data_, other.data_, sizeof(data_));
        }
        return *this;
    }

    inline const float *Data() const {
        return data_;
    }

    inline float *Data() {
        return data_;
    }

    TransformMatrix Inv() const;

    bool IsIdentity(float eps = 1e-6) const;

    bool IsCrop(float eps = 1e-6) const;

    bool IsResize(float eps = 1e-6) const;

    bool IsCropAndResize(float eps = 1e-6) const;

    static TransformMatrix Identity() {
        return TransformMatrix({1.f, 0.f, 0.f, 0.f, 1.f, 0.f});
    }

private:
    float data_[6];
};

inline std::ostream &operator<<(std::ostream &stream, const TransformMatrix &m) {
    stream << "[" << m[0];
    for (int i = 1; i < 6; ++i)
        stream << ", " << m[i];
    stream << "]";
    return stream;
}

}  // namespace okcv

#endif  // OKCV_GEOM_TRANSFORM_MATRIX_H_
