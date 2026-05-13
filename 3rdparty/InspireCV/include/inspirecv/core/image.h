#ifndef INSPIRECV_IMAGE_H
#define INSPIRECV_IMAGE_H

#include <memory>
#include <string>
#include "define.h"
#include "rect.h"
#include "point.h"
#include "size.h"
#include "transform_matrix.h"

namespace inspirecv {

/**
 * @brief Class representing an image with basic image processing operations
 */
class INSPIRECV_API Image {
public:
    // Disable copy constructor and copy assignment operator
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // #endif  // INSPIRECV_BACKEND_OPENCV

    /**
     * @brief Default constructor
     */
    Image();

    /**
     * @brief Destructor
     */
    ~Image();

    /**
     * @brief Move constructor
     */
    Image(Image&&) noexcept;

    /**
     * @brief Move assignment operator
     */
    Image& operator=(Image&&) noexcept;

    /**
     * @brief Constructor with width, height, channels and optional data
     * @param width Image width
     * @param height Image height
     * @param channels Number of color channels
     * @param copy_data Whether to copy data, default is true. When set to false, the image will
     * directly use the input data without copying. This is dangerous as the data lifetime must be
     * managed externally and outlive the image. The data must also remain valid and unmodified for
     * the entire lifetime of the image.
     * Note: Zero-copy mode is currently only supported with OKCV backend.
     * @param data Optional pointer to image data
     */
    Image(int width, int height, int channels, const uint8_t* data = nullptr,
          bool copy_data = true);

    /**
     * @brief Reset image with new dimensions and data
     * @param width New width
     * @param height New height
     * @param channels New number of channels
     * @param data Optional pointer to new image data
     */
    void Reset(int width, int height, int channels, const uint8_t* data = nullptr);

    /**
     * @brief Create a deep copy of the image
     * @return New image that is a copy of this one
     */
    Image Clone() const;

    /**
     * @brief Get image width
     * @return Width in pixels
     */
    int Width() const;

    /**
     * @brief Get image height
     * @return Height in pixels
     */
    int Height() const;

    /**
     * @brief Get number of color channels
     * @return Number of channels
     */
    int Channels() const;

    /**
     * @brief Check if image is empty
     * @return true if image has no data
     */
    bool Empty() const;

    /**
     * @brief Get raw image data
     * @return Pointer to image data
     */
    const uint8_t* Data() const;

    /**
     * @brief Get internal image implementation
     * @return Pointer to internal image
     */
    void* GetInternalImage() const;

    /**
     * @brief Read image from file
     * @param filename Path to image file
     * @param channels Number of channels to read (default: 3)
     * @return true if successful
     */
    bool Read(const std::string& filename, int channels = 3);

    /**
     * @brief Write image to file
     * @param filename Output file path
     * @return true if successful
     */
    bool Write(const std::string& filename) const;

    /**
     * @brief Display image in a window
     * @param window_name Name of display window
     * @param delay Wait time in milliseconds (0 = wait forever)
     */
    void Show(const std::string& window_name = std::string("win"), int delay = 0) const;

    /**
     * @brief Fill entire image with value
     * @param value Fill value
     */
    void Fill(double value);

    /**
     * @brief Multiply image by scale factor
     * @param scale Scale factor
     * @return New scaled image
     */
    Image Mul(double scale) const;

    /**
     * @brief Add value to image
     * @param value Value to add
     * @return New image with added value
     */
    Image Add(double value) const;

    /**
     * @brief Resize image to new dimensions
     * @param width New width
     * @param height New height
     * @param use_linear Use linear interpolation if true
     * @return Resized image
     */
    Image Resize(int width, int height, bool use_linear = true) const;

    /**
     * @brief Crop image to rectangle
     * @param rect Rectangle defining crop region
     * @return Cropped image
     */
    Image Crop(const Rect<int>& rect) const;

    /**
     * @brief Apply affine transformation
     * @param matrix 2x3 transformation matrix
     * @param width Output width
     * @param height Output height
     * @return Transformed image
     */
    Image WarpAffine(const TransformMatrix& matrix, int width, int height) const;

    /**
     * @brief Rotate image 90 degrees clockwise
     * @return Rotated image
     */
    Image Rotate90() const;

    /**
     * @brief Rotate image 180 degrees
     * @return Rotated image
     */
    Image Rotate180() const;

    /**
     * @brief Rotate image 270 degrees clockwise
     * @return Rotated image
     */
    Image Rotate270() const;

    /**
     * @brief Swap the red and blue channels of the image.
     * @return The swapped image.
     */
    Image SwapRB() const;

    /**
     * @brief Flip image horizontally
     * @return Flipped image
     */
    Image FlipHorizontal() const;

    /**
     * @brief Flip image vertically
     * @return Flipped image
     */
    Image FlipVertical() const;

    /**
     * @brief Add padding around image
     * @param top Top padding
     * @param bottom Bottom padding
     * @param left Left padding
     * @param right Right padding
     * @param color Padding color values
     * @return Padded image
     */
    Image Pad(int top, int bottom, int left, int right, const std::vector<double>& color) const;

    /**
     * @brief Apply Gaussian blur
     * @param kernel_size Size of Gaussian kernel
     * @param sigma Gaussian standard deviation
     * @return Blurred image
     */
    Image GaussianBlur(int kernel_size, double sigma) const;

    /**
     * @brief Apply morphological erosion (only for single-channel images)
     * @param kernel_size Size of square structuring element (odd, >=1)
     * @param iterations Number of iterations (default 1)
     * @return Eroded image
     */
    Image Erode(int kernel_size, int iterations = 1) const;

    /**
     * @brief Apply morphological dilation (only for single-channel images)
     * @param kernel_size Size of square structuring element (odd, >=1)
     * @param iterations Number of iterations (default 1)
     * @return Dilated image
     */
    Image Dilate(int kernel_size, int iterations = 1) const;

    /**
     * @brief Apply threshold operation
     * @param thresh Threshold value
     * @param maxval Maximum value
     * @param type Threshold type
     * @return Thresholded image
     */
    Image Threshold(double thresh, double maxval, int type) const;

    /**
     * @brief Convert image to grayscale
     * @return Grayscale image
     */
    Image ToGray() const;

    /**
     * @brief Per-pixel absolute difference with another image (same size/channels)
     */
    Image AbsDiff(const Image& other) const;

    /**
     * @brief Average across channels to single-channel (integer mean for U8)
     */
    Image MeanChannels() const;

    /**
     * @brief Alpha blend with another image using single-channel 0..255 mask
     * out = (mask*this + (255-mask)*other)/255
     */
    Image Blend(const Image& other, const Image& mask) const;

    /**
     * @brief Draw line between two points
     * @param p1 Start point
     * @param p2 End point
     * @param color Line color
     * @param thickness Line thickness
     */
    void DrawLine(const Point<int>& p1, const Point<int>& p2, const std::vector<double>& color,
                  int thickness = 1);

    /**
     * @brief Draw rectangle
     * @param rect Rectangle to draw
     * @param color Rectangle color
     * @param thickness Line thickness
     */
    void DrawRect(const Rect<int>& rect, const std::vector<double>& color, int thickness = 1);

    /**
     * @brief Draw circle
     * @param center Circle center point
     * @param radius Circle radius
     * @param color Circle color
     * @param thickness Line thickness
     */
    void DrawCircle(const Point<int>& center, int radius, const std::vector<double>& color,
                    int thickness = 1);

    /**
     * @brief Fill rectangle with color
     * @param rect Rectangle to fill
     * @param color Fill color
     */
    void Fill(const Rect<int>& rect, const std::vector<double>& color);

    /**
     * @brief Create image with dimensions and optional data
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels
     * @param data Optional image data
     * @param copy_data Whether to copy data, default is true. When set to false, the image will
     * directly use the input data without copying. This is dangerous as the data lifetime must be
     * managed externally and outlive the image. The data must also remain valid and unmodified for
     * the entire lifetime of the image.
     * Note: Zero-copy mode is currently only supported with OKCV backend.
     * @return New image
     */
    static Image Create(int width, int height, int channels, const uint8_t* data = nullptr,
                        bool copy_data = true);

    /**
     * @brief Create empty image
     * @return New empty image
     */
    static Image Create();

    /**
     * @brief Create image from file
     * @param filename Path to image file
     * @param channels Number of channels to read
     * @return New image loaded from file
     */
    static Image Create(const std::string& filename, int channels = 3);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    /**
     * @brief Private constructor with implementation
     * @param impl Image implementation
     */
    Image(std::unique_ptr<Impl> impl);

    // Declare Impl as friend class
    friend class Impl;
    // Add this line to declare the operator as a friend
    friend std::ostream& operator<<(std::ostream& os, const Image& image);
};

/**
 * @brief Stream output operator for Image
 * @param os Output stream
 * @param image Image to output
 * @return Modified output stream
 */
INSPIRECV_API std::ostream& operator<<(std::ostream& os, const Image& image);

}  // namespace inspirecv

#endif  // INSPIRECV_IMAGE_H
