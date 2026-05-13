#ifndef INSPIRECV_TRANSFORM_MATRIX_H
#define INSPIRECV_TRANSFORM_MATRIX_H

#include "define.h"
#include <memory>
#include <vector>

namespace inspirecv {

/**
 * @brief Class representing a 2D transformation matrix
 */
class INSPIRECV_API TransformMatrix {
public:
    /**
     * @brief Copy constructor
     * @param other Matrix to copy from
     */
    TransformMatrix(const TransformMatrix &other);

    /**
     * @brief Copy assignment operator
     * @param other Matrix to copy from
     * @return Reference to this matrix
     */
    TransformMatrix &operator=(const TransformMatrix &other);

    /**
     * @brief Default constructor
     */
    TransformMatrix();

    /**
     * @brief Destructor
     */
    ~TransformMatrix();

    /**
     * @brief Constructor with matrix elements
     * @param a11 Element at row 1, col 1
     * @param a12 Element at row 1, col 2
     * @param b1 Element at row 1, col 3
     * @param a21 Element at row 2, col 1
     * @param a22 Element at row 2, col 2
     * @param b2 Element at row 2, col 3
     */
    TransformMatrix(float a11, float a12, float b1, float a21, float a22, float b2);

    // Basic getters and setters
    /**
     * @brief Get matrix element at specified position
     * @param row Row index (0-based)
     * @param col Column index (0-based)
     * @return Value at specified position
     */
    float Get(int row, int col) const;

    /**
     * @brief Set matrix element at specified position
     * @param row Row index (0-based)
     * @param col Column index (0-based)
     * @param value New value to set
     */
    void Set(int row, int col, float value);

    /**
     * @brief Convert matrix to vector form
     * @return Vector containing matrix elements in row-major order
     */
    std::vector<float> Squeeze() const;

    /**
     * @brief Array subscript operator for const access
     * @param index Element index in row-major order
     * @return Value at specified index
     */
    float operator[](int index) const;

    /**
     * @brief Array subscript operator for modifying access
     * @param index Element index in row-major order
     * @return Reference to value at specified index
     */
    float &operator[](int index);

    /**
     * @brief Get internal matrix implementation
     * @return Pointer to internal matrix implementation
     */
    void *GetInternalMatrix() const;

    // Basic operations
    /**
     * @brief Check if matrix is identity matrix
     * @return true if matrix is identity
     */
    bool IsIdentity() const;

    /**
     * @brief Set matrix to identity matrix
     */
    void SetIdentity();

    /**
     * @brief Invert this matrix in-place
     */
    void Invert();

    /**
     * @brief Get inverse of this matrix
     * @return New matrix that is inverse of this one
     */
    TransformMatrix GetInverse() const;

    // Transform operations
    /**
     * @brief Apply translation transformation
     * @param dx Translation in x direction
     * @param dy Translation in y direction
     */
    void Translate(float dx, float dy);

    /**
     * @brief Apply scaling transformation
     * @param sx Scale factor in x direction
     * @param sy Scale factor in y direction
     */
    void Scale(float sx, float sy);

    /**
     * @brief Apply rotation transformation
     * @param angle Rotation angle in radians
     */
    void Rotate(float angle);

    // Matrix operations
    /**
     * @brief Multiply this matrix with another
     * @param other Matrix to multiply with
     * @return Result of matrix multiplication
     */
    TransformMatrix Multiply(const TransformMatrix &other) const;

    /**
     * @brief Create a deep copy of this matrix
     * @return New matrix that is a copy of this one
     */
    TransformMatrix Clone() const;

    // Factory methods
    /**
     * @brief Create default transformation matrix
     * @return New default matrix
     */
    static TransformMatrix Create();

    /**
     * @brief Create matrix with specified elements
     * @param a11 Element at row 1, col 1
     * @param a12 Element at row 1, col 2
     * @param b1 Element at row 1, col 3
     * @param a21 Element at row 2, col 1
     * @param a22 Element at row 2, col 2
     * @param b2 Element at row 2, col 3
     * @return New matrix with specified elements
     */
    static TransformMatrix Create(float a11, float a12, float b1, float a21, float a22, float b2);

    /**
     * @brief Create identity matrix
     * @return New identity matrix
     */
    static TransformMatrix Identity();

    /**
     * @brief Create 90-degree rotation matrix
     * @return New matrix for 90-degree rotation
     */
    static TransformMatrix Rotate90();

    /**
     * @brief Create 180-degree rotation matrix
     * @return New matrix for 180-degree rotation
     */
    static TransformMatrix Rotate180();

    /**
     * @brief Create 270-degree rotation matrix
     * @return New matrix for 270-degree rotation
     */
    static TransformMatrix Rotate270();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Stream output operator for TransformMatrix
 * @param os Output stream
 * @param matrix Matrix to output
 * @return Reference to output stream
 */
INSPIRECV_API std::ostream &operator<<(std::ostream &os, const TransformMatrix &matrix);

}  // namespace inspirecv

#endif  // INSPIRECV_TRANSFORM_MATRIX_H
