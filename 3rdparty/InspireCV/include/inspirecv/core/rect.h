#ifndef INSPIRECV_RECT_H
#define INSPIRECV_RECT_H

#include "define.h"
#include "point.h"
#include <memory>
#include <vector>

namespace inspirecv {

/**
 * @brief A template class representing a 2D rectangle with position and size
 * @tparam T The coordinate type (int, float, double)
 */
template <typename T>
class INSPIRECV_API Rect {
public:
    /**
     * @brief Copy constructor
     * @param other Rectangle to copy from
     */
    Rect(const Rect &other);

    /**
     * @brief Copy assignment operator
     * @param other Rectangle to copy from
     * @return Reference to this rectangle
     */
    Rect &operator=(const Rect &other);

    /**
     * @brief Default constructor
     */
    Rect();

    /**
     * @brief Constructor with position and size
     * @param x X coordinate of top-left corner
     * @param y Y coordinate of top-left corner
     * @param width Width of rectangle
     * @param height Height of rectangle
     */
    Rect(T x, T y, T width, T height);

    /**
     * @brief Destructor
     */
    ~Rect();

    // Basic getters and setters
    /**
     * @brief Convert rectangle coordinates to another type
     * @tparam U Target type for conversion
     * @return New rectangle with converted coordinates
     */
    template <typename U>
    Rect<U> As() const;

    /**
     * @brief Get x coordinate of top-left corner
     * @return The x coordinate
     */
    T GetX() const;

    /**
     * @brief Get y coordinate of top-left corner
     * @return The y coordinate
     */
    T GetY() const;

    /**
     * @brief Get width of rectangle
     * @return The width
     */
    T GetWidth() const;

    /**
     * @brief Get height of rectangle
     * @return The height
     */
    T GetHeight() const;

    /**
     * @brief Set x coordinate of top-left corner
     * @param x New x coordinate value
     */
    void SetX(T x);

    /**
     * @brief Set y coordinate of top-left corner
     * @param y New y coordinate value
     */
    void SetY(T y);

    /**
     * @brief Set width of rectangle
     * @param width New width value
     */
    void SetWidth(T width);

    /**
     * @brief Set height of rectangle
     * @param height New height value
     */
    void SetHeight(T height);

    /**
     * @brief Get internal rectangle implementation
     * @return Pointer to internal rectangle implementation
     */
    void *GetInternalRect() const;

    // Boundary points
    /**
     * @brief Get top-left corner point
     * @return Point at top-left corner
     */
    Point<T> TopLeft() const;

    /**
     * @brief Get top-right corner point
     * @return Point at top-right corner
     */
    Point<T> TopRight() const;

    /**
     * @brief Get bottom-left corner point
     * @return Point at bottom-left corner
     */
    Point<T> BottomLeft() const;

    /**
     * @brief Get bottom-right corner point
     * @return Point at bottom-right corner
     */
    Point<T> BottomRight() const;

    /**
     * @brief Get center point of rectangle
     * @return Point at center of rectangle
     */
    Point<T> Center() const;

    /**
     * @brief Convert rectangle to four corner vertices
     * @return Vector of four corner points
     */
    std::vector<Point<T>> ToFourVertices() const;

    /**
     * @brief Get a safe rectangle bounded by given dimensions
     * @param width Maximum width bound
     * @param height Maximum height bound
     * @return New rectangle within bounds
     */
    Rect<T> SafeRect(T width, T height) const;

    // Basic operations
    /**
     * @brief Calculate area of rectangle
     * @return Area value
     */
    T Area() const;

    /**
     * @brief Check if rectangle is empty
     * @return true if width or height is zero
     */
    bool Empty() const;

    /**
     * @brief Check if point is inside rectangle
     * @param point Point to check
     * @return true if point is inside
     */
    bool Contains(const Point<T> &point) const;

    /**
     * @brief Check if another rectangle is fully contained
     * @param rect Rectangle to check
     * @return true if rect is fully inside
     */
    bool Contains(const Rect<T> &rect) const;

    // Geometric operations
    /**
     * @brief Calculate intersection with another rectangle
     * @param other Rectangle to intersect with
     * @return Intersection rectangle
     */
    Rect<T> Intersect(const Rect<T> &other) const;

    /**
     * @brief Calculate union with another rectangle
     * @param other Rectangle to unite with
     * @return Union rectangle
     */
    Rect<T> Union(const Rect<T> &other) const;

    /**
     * @brief Calculate Intersection over Union (IoU)
     * @param other Rectangle to calculate IoU with
     * @return IoU value between 0 and 1
     */
    double IoU(const Rect<T> &other) const;

    // Transformation operations
    /**
     * @brief Scale rectangle dimensions
     * @param sx Scale factor for width
     * @param sy Scale factor for height
     */
    void Scale(T sx, T sy);

    /**
     * @brief Translate rectangle position
     * @param dx Translation in x direction
     * @param dy Translation in y direction
     */
    void Translate(T dx, T dy);

    /**
     * @brief Create a square rectangle centered on current rectangle
     * @param scale Scale factor for square size
     * @return Square rectangle
     */
    Rect<T> Square(float scale = 1.0) const;

    /**
     * @brief Create rectangle from coordinates and dimensions
     * @param x X coordinate of top-left corner
     * @param y Y coordinate of top-left corner
     * @param width Width of rectangle
     * @param height Height of rectangle
     * @return New rectangle
     */
    static Rect<T> Create(T x, T y, T width, T height);

    /**
     * @brief Create rectangle from two corner points
     * @param left_top Top-left corner point
     * @param right_bottom Bottom-right corner point
     * @return New rectangle
     */
    static Rect<T> Create(const Point<T> &left_top, const Point<T> &right_bottom);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Type alias for integer rectangle
 */
using Rect2i = Rect<int>;

/**
 * @brief Type alias for float rectangle
 */
using Rect2f = Rect<float>;

/**
 * @brief Type alias for double rectangle
 */
using Rect2d = Rect<double>;

/** @brief Type alias for Rect<int> */
using Rect2 = Rect2i;

/**
 * @brief Stream output operator for rectangle
 * @param os Output stream
 * @param rect Rectangle to output
 * @return Reference to output stream
 */
template <typename T>
INSPIRECV_API std::ostream &operator<<(std::ostream &os, const Rect<T> &rect);

/**
 * @brief Calculate minimum bounding rectangle for set of points
 * @param points Vector of points
 * @return Minimum bounding rectangle
 */
template <typename T>
INSPIRECV_API Rect<T> MinBoundingRect(const std::vector<Point<T>> &points);

/**
 * @brief Apply transformation matrix to rectangle
 * @param rect Rectangle to transform
 * @param transform Transformation matrix to apply
 * @return Transformed rectangle
 */
template <typename T>
INSPIRECV_API Rect<T> ApplyTransformToRect(const Rect<T> &rect, const TransformMatrix &transform);

}  // namespace inspirecv

#endif  // INSPIRECV_RECT_H
