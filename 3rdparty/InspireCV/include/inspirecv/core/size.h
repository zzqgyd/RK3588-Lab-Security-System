#ifndef INSPIRECV_SIZE_H
#define INSPIRECV_SIZE_H

#include "define.h"
#include <memory>

namespace inspirecv {

/**
 * @brief A template class representing a 2D size with width and height
 * @tparam T The size type (int, float, double)
 */
template <typename T>
class INSPIRECV_API Size {
public:
    /**
     * @brief Copy constructor
     * @param other Size to copy from
     */
    Size(const Size &other);

    /**
     * @brief Copy assignment operator
     * @param other Size to copy from
     * @return Reference to this size
     */
    Size &operator=(const Size &other);

    /**
     * @brief Default constructor
     */
    Size();

    /**
     * @brief Constructor with width and height
     * @param width Width value
     * @param height Height value
     */
    Size(T width, T height);

    /**
     * @brief Destructor
     */
    ~Size();

    // Basic getters and setters
    /**
     * @brief Get width value
     * @return The width
     */
    T GetWidth() const;

    /**
     * @brief Get height value
     * @return The height
     */
    T GetHeight() const;

    /**
     * @brief Set width value
     * @param width New width value
     */
    void SetWidth(T width);

    /**
     * @brief Set height value
     * @param height New height value
     */
    void SetHeight(T height);

    // Basic operations
    /**
     * @brief Calculate area (width * height)
     * @return Area value
     */
    T Area() const;

    /**
     * @brief Check if size is empty (width or height is 0)
     * @return true if empty
     */
    bool Empty() const;

    /**
     * @brief Scale width and height by given factors
     * @param sx Scale factor for width
     * @param sy Scale factor for height
     */
    void Scale(T sx, T sy);

    // Factory method
    /**
     * @brief Create a new Size instance
     * @param width Width value
     * @param height Height value
     * @return New Size instance
     */
    static Size Create(T width, T height);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Type alias for Size with integer values
 */
using Size2i = Size<int>;

/**
 * @brief Type alias for Size with float values
 */
using Size2f = Size<float>;

/**
 * @brief Type alias for Size with double values
 */
using Size2d = Size<double>;

/** @brief Type alias for Size<int> */
using Size2 = Size2i;

/**
 * @brief Stream output operator for Size
 * @param os Output stream
 * @param size Size to output
 * @return Reference to output stream
 */
template <typename T>
INSPIRECV_API std::ostream &operator<<(std::ostream &os, const Size<T> &size);

}  // namespace inspirecv

#endif  // INSPIRECV_SIZE_H