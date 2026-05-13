#ifndef INSPIRECV_POINT_H
#define INSPIRECV_POINT_H

#include "define.h"
#include "transform_matrix.h"
#include <memory>

namespace inspirecv {

/**
 * @brief A template class representing a 2D point with x and y coordinates
 * @tparam T The coordinate type (int, float, double)
 */
template <typename T>
class INSPIRECV_API Point {
public:
    /**
     * @brief Default constructor
     */
    Point();

    /**
     * @brief Constructor with x and y coordinates
     * @param x The x coordinate
     * @param y The y coordinate
     */
    Point(T x, T y);

    /**
     * @brief Destructor
     */
    ~Point();

    // Move semantics
    /**
     * @brief Move constructor
     */
    Point(Point &&) noexcept;

    /**
     * @brief Move assignment operator
     */
    Point &operator=(Point &&) noexcept;

    // Disable copy
    /**
     * @brief Copy constructor
     */
    Point(const Point &other);

    /**
     * @brief Copy assignment operator
     */
    Point &operator=(const Point &other);

    /**
     * @brief Equality operator
     * @param other Point to compare with
     * @return true if points are equal
     */
    bool operator==(const Point &other) const;

    /**
     * @brief Inequality operator
     * @param other Point to compare with
     * @return true if points are not equal
     */
    bool operator!=(const Point &other) const;

    /**
     * @brief Convert point coordinates to another type
     * @tparam U Target type for conversion
     * @return New point with converted coordinates
     */
    template <typename U>
    Point<U> As() const;

    // Basic getters and setters
    /**
     * @brief Get x coordinate
     * @return The x coordinate
     */
    T GetX() const;

    /**
     * @brief Get y coordinate
     * @return The y coordinate
     */
    T GetY() const;

    /**
     * @brief Set x coordinate
     * @param x New x coordinate value
     */
    void SetX(T x);

    /**
     * @brief Set y coordinate
     * @param y New y coordinate value
     */
    void SetY(T y);

    /**
     * @brief Get internal point implementation
     * @return Pointer to internal point implementation
     */
    void *GetInternalPoint() const;

    // Basic operations
    /**
     * @brief Calculate length (magnitude) of vector from origin to point
     * @return Length of vector
     */
    double Length() const;

    /**
     * @brief Calculate Euclidean distance to another point
     * @param other Point to calculate distance to
     * @return Distance between points
     */
    double Distance(const Point &other) const;

    /**
     * @brief Calculate dot product with another point
     * @param other Point to calculate dot product with
     * @return Dot product result
     */
    T Dot(const Point &other) const;

    /**
     * @brief Calculate cross product with another point
     * @param other Point to calculate cross product with
     * @return Cross product result
     */
    T Cross(const Point &other) const;

    /**
     * @brief Factory method to create a new point
     * @param x The x coordinate
     * @param y The y coordinate
     * @return New point instance
     */
    static Point Create(T x, T y);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};  // class Point

/** @brief Type alias for Point<int> */
using Point2i = Point<int>;
/** @brief Type alias for Point<float> */
using Point2f = Point<float>;
/** @brief Type alias for Point<double> */
using Point2d = Point<double>;
/** @brief Type alias for Point<int> */
using Point2 = Point2i;

/**
 * @brief Stream output operator for Point
 * @param os Output stream
 * @param point Point to output
 * @return Reference to output stream
 */
template <typename T>
INSPIRECV_API std::ostream &operator<<(std::ostream &os, const Point<T> &point);

/**
 * @brief Stream output operator for vector of Points
 * @param os Output stream
 * @param points Vector of points to output
 * @return Reference to output stream
 */
template <typename T>
INSPIRECV_API std::ostream &operator<<(std::ostream &os, const std::vector<Point<T>> &points);

/**
 * @brief Apply transformation matrix to vector of points
 * @param points Vector of points to transform
 * @param transform Transform matrix to apply
 * @return Vector of transformed points
 */
template <typename T>
INSPIRECV_API std::vector<Point<T>> ApplyTransformToPoints(const std::vector<Point<T>> &points,
                                                           const TransformMatrix &transform);

/**
 * @brief Estimate similarity transform between two sets of corresponding points
 * @param src_points Source points
 * @param dst_points Destination points
 * @return Estimated transform matrix
 */
template <typename T>
INSPIRECV_API TransformMatrix SimilarityTransformEstimate(const std::vector<Point<T>> &src_points,
                                                          const std::vector<Point<T>> &dst_points);

/**
 * @brief Estimate similarity transform between two sets of corresponding points using Umeyama
 * algorithm
 * @param src_points Source points
 * @param dst_points Destination points
 * @return Estimated transform matrix
 */
template <typename T>
INSPIRECV_API TransformMatrix SimilarityTransformEstimateUmeyama(
  const std::vector<Point<T>> &src_points, const std::vector<Point<T>> &dst_points);

}  // namespace inspirecv

#endif  // INSPIRECV_POINT_H