#ifndef OKCV_IMAGE_H_
#define OKCV_IMAGE_H_

#include <cstddef>
#include <string>
#include <utility>
#include <functional>
#include <memory>
#include <vector>

#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV
#include <opencv2/opencv.hpp>
#endif  // INSPIRECV_BACKEND_OKCV_USE_OPENCV

#if (defined(__ARM_NEON__) || defined(__ARM_NEON))
#include <arm_neon.h>
#endif

#include <okcv/geometry/geom.h>
#include <okcv/base/base.h>

namespace okcv {

// Border mode
enum BorderMode {
    BORDER_MODE_CONSTANT = 0,   // Constant value
    BORDER_MODE_REPLICATE = 1,  // Replicate
};

/*

/**
 * @brief Image class template for handling image data.
 *
 * This class provides functionality for image manipulation, including:
 * - Image creation and memory management
 * - Data type conversion
 * - Image I/O operations (read/write)
 * - Basic image processing operations (fill, multiply, add)
 * - Image display
 *
 * The Image class supports various data types through the template parameter.
 *
 * @tparam dtype The data type of the image pixels (e.g., uint8_t, float)
 */
template <typename D>
class OKCV_API Image {
public:
    Image() : width_(0), height_(0), channels_(0), data_(nullptr) {}

    Image(Image &&image);
    Image &operator=(Image &&image);

    // Add destructor or modify existing one
    ~Image() {
        if (is_external_) {
            // Don't delete external data
            data_.reset();
            external_data_ = nullptr;
        }
    }

    inline bool Empty() const {
        return height_ == 0 || width_ == 0;
    }

    /**
     * @brief Resets the image to an empty state.
     */
    void Reset();

    /**
     * @brief Resets the image to a new size and data.
     * @param width The width of the image.
     * @param height The height of the image.
     * @param channels The number of channels in the image.
     * @param data The data for the image.
     */
    void Reset(int width, int height, int channels, const D *data = nullptr, bool copy_data = true);

    /**
     * @brief Converts the image to a new data type.
     * @tparam T The new data type.
     * @return The converted image.
     */
    template <typename T>
    Image<T> As() const {
        Image<T> dst;
        dst.Reset(width_, height_, channels_);
        auto src_iter = Data();
        auto dst_iter = dst.Data();
        int size = height_ * width_ * channels_;
        for (int i = 0; i < size; ++i)
            *(dst_iter++) = static_cast<T>(*(src_iter++));
        return dst;
    }

    /**
     * @brief Clones the image.
     * @return A deep copy of the image.
     */
    Image<D> Clone() const;

    /**
     * @brief Copies the image to another image.
     * @param dst The destination image.
     */
    void CopyTo(Image<D> &dst) const;

    /**
     * @brief Reads an image from a file using either OpenCV or STB image library.
     * @param filename The path to the image file.
     * @param channels The number of channels in the image.
     * @note When INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO is defined, OpenCV's imread() will be used.
     *       Otherwise STB image library will be used for reading images.
     */
    void Read(const char *filename, int channels = 3);

    /**
     * @brief Reads an image from a file using either OpenCV or STB image library.
     * @param filename The path to the image file.
     * @param channels The number of channels in the image.
     * @note When INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO is defined, OpenCV's imread() will be used.
     *       Otherwise STB image library will be used for reading images.
     */
    void Read(const std::string &filename, int channels = 3);

    /**
     * @brief Writes the image to a file using either OpenCV or STB image library.
     * @param filename The path to the image file.
     * @note When INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO is defined, OpenCV's imwrite() will be used.
     *       Otherwise STB image library will be used for writing images.
     *       Supported formats include: PNG, JPEG, BMP.
     */
    void Write(const char *filename) const;

    /**
     * @brief Writes the image to a file using either OpenCV or STB image library.
     * @param filename The path to the image file.
     * @note When INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO is defined, OpenCV's imwrite() will be used.
     *       Otherwise STB image library will be used for writing images.
     *       Supported formats include: PNG, JPEG, BMP.
     */
    void Write(const std::string &filename) const;

    /**
     * @brief Creates an image from a buffer.
     * @param buffer The buffer containing the image data.
     * @param channels The number of channels in the image.
     */
    void FromImageBuffer(const std::vector<char> &buffer, int channels = 3);

    /**
     * @brief Displays the image using OpenCV's window.
     * @param name The name of the window.
     * @param time The time to display the image.
     */
    void Show(const std::string &name = "", int time = -1) const;

    /**
     * @brief Fills the image with a constant value.
     * @param v The value to fill the image with.
     */
    void Fill(D v);

    /**
     * @brief Multiplies the image by a scalar.
     * @param a The scalar to multiply the image by.
     * @return The resulting image.
     */
    Image<D> Mul(float a) const;

    /**
     * @brief Multiplies the image by a scalar and adds another scalar.
     * @param a The scalar to multiply the image by.
     * @param b The scalar to add to the image.
     * @return The resulting image.
     */
    Image<D> MulAdd(float a, float b) const;

    /**
     * @brief Applies an element-wise operation to the image.
     * @param image The image to apply the operation to.
     * @param op The operation to apply.
     * @return The resulting image.
     */
    Image<D> ElementWiseOperate(const Image<D> &image, const std::function<D(D, D)> &op) const;

    /**
     * @brief Applies a pixel-wise operation to the image.
     * @param func The operation to apply.
     */
    void ApplyPixelwiseOperation(const std::function<D(D)> &func);

    /**
     * @brief Resizes the image using bilinear interpolation.
     * @param width The width of the resized image.
     * @param height The height of the resized image.
     * @return The resized image.
     */
    Image<D> ResizeBilinear(int width, int height) const;

    /**
     * @brief Resizes the image using nearest neighbor interpolation.
     * @param width The width of the resized image.
     * @param height The height of the resized image.
     * @return The resized image.
     */
    Image<D> ResizeNearest(int width, int height) const;

    /**
     * @brief Pads the image with a constant value.
     * @param top The number of pixels to pad on the top.
     * @param down The number of pixels to pad on the bottom.
     * @param left The number of pixels to pad on the left.
     * @param right The number of pixels to pad on the right.
     * @param value The value to pad the image with.
     * @return The padded image.
     */
    Image<D> Pad(int top, int down, int left, int right, D value = 0) const;

    /**
     * @brief Adds an alpha channel to the image.
     * @param dst The destination image.
     * @param index The index of the alpha channel.
     * @param alpha The value of the alpha channel.
     */
    void AddAlphaChannel(Image<D> &dst, int index, D alpha) const;

    /**
     * @brief Crops the image.
     * @param rect The rectangle to crop the image to.
     * @param allow_padding Whether to allow padding.
     * @return The cropped image.
     */
    Image<D> Crop(const Rect<int> &rect, bool allow_padding = false) const;

    /**
     * @brief Crops the image and resizes it using bilinear interpolation.
     * @param dst The destination image.
     * @param rect The rectangle to crop the image to.
     * @param resize_width The width of the resized image.
     * @param resize_height The height of the resized image.
     */
    void CropAndResizeBilinear(Image<D> &dst, const Rect<int> &rect, int resize_width,
                               int resize_height) const;

    /**
     * @brief Crops the image and resizes it using nearest neighbor interpolation.
     * @param dst The destination image.
     * @param rect The rectangle to crop the image to.
     * @param resize_width The width of the resized image.
     * @param resize_height The height of the resized image.
     */
    void CropAndResizeNearest(Image<D> &dst, const Rect<int> &rect, int resize_width,
                              int resize_height) const;

    /**
     * @brief Gets the transformation matrix for the image.
     * @param width The width of the image.
     * @param height The height of the image.
     * @param rect The rectangle to crop the image to.
     * @param matrix The transformation matrix.
     */
    void GetTransformMatrix(int width, int height, const Rect<int> &rect,
                            TransformMatrix &matrix) const;

    /**
     * @brief Applies an affine transformation to the image using bilinear interpolation.
     * @param width The width of the image.
     * @param height The height of the image.
     * @param matrix The transformation matrix.
     * @param border_mode The border mode.
     * @param border_value The border value.
     * @return The transformed image.
     */
    Image<D> AffineBilinear(int width, int height, const TransformMatrix &matrix,
                            BorderMode border_mode = BORDER_MODE_CONSTANT,
                            D border_value = 0) const;

    /**
     * @brief Applies an affine transformation to the image using bilinear interpolation.
     * @param width The width of the image.
     * @param height The height of the image.
     * @param matrix The transformation matrix.
     * @param border_mode The border mode.
     * @param border_value The border value.
     * @return The transformed image.
     */
    Image<D> AffineBilinearReference(int width, int height, const TransformMatrix &matrix,
                                     BorderMode border_mode = BORDER_MODE_CONSTANT,
                                     D border_value = 0) const;

    /**
     * @brief Blurs the image using a Gaussian kernel.
     * @param kernel The size of the kernel.
     * @return The blurred image.
     */
    Image<D> Blur(int kernel) const;

    /**
     * @brief Applies Gaussian blur with kernel size and sigma.
     * @param ksize Kernel size (odd, >=1). If <=1 returns clone.
     * @param sigmaX Gaussian sigma in X (and Y). If <=0, computed from ksize.
     * @return The blurred image.
     */
    Image<D> GaussianBlur(int ksize, float sigmaX) const;

    /**
     * @brief Applies a minimum filter to the image.
     * @param kernel_left The size of the kernel on the left.
     * @param kernel_right The size of the kernel on the right.
     * @param kernel_top The size of the kernel on the top.
     * @param kernel_bottom The size of the kernel on the bottom.
     * @return The filtered image.
     */
    Image<D> MinFilter(int kernel_left, int kernel_right, int kernel_top, int kernel_bottom) const;

    /**
     * @brief Applies a maximum filter to the image.
     * @param kernel_left The size of the kernel on the left.
     * @param kernel_right The size of the kernel on the right.
     * @param kernel_top The size of the kernel on the top.
     * @param kernel_bottom The size of the kernel on the bottom.
     * @return The filtered image.
     */
    Image<D> MaxFilter(int kernel_left, int kernel_right, int kernel_top, int kernel_bottom) const;

    /**
     * @brief Flips the image horizontally.
     * @return The flipped image.
     */
    Image<D> FlipLeftRight() const;

    /**
     * @brief Flips the image vertically.
     * @return The flipped image.
     */
    Image<D> FlipUpDown() const;

    /**
     * @brief Flips the image channels.
     * @return The flipped image.
     */
    Image<D> FlipChannels() const;

    /**
     * @brief Rotates the image 90 degrees clockwise.
     * @return The rotated image.
     */
    Image<D> Rotate90() const;

    /**
     * @brief Rotates the image 180 degrees clockwise.
     * @return The rotated image.
     */
    Image<D> Rotate180() const;

    /**
     * @brief Rotates the image 270 degrees clockwise.
     * @return The rotated image.
     */
    Image<D> Rotate270() const;

    /**
     * @brief Converts the image from RGB to grayscale.
     * @return The grayscale image.
     */
    Image<D> RgbToGray() const;

    /**
     * @brief Swaps the red and blue channels of the image.
     * @return The swapped image.
     */
    Image<D> SwapRB() const;

    /**
     * @brief Gets the mask rectangle of the image.
     * @param threshold The threshold value.
     * @return The mask rectangle.
     */
    Rect2i GetMaskRect(D threshold = 0) const;

    /**
     * @brief Fills a rectangle with a color.
     * @param rect The rectangle to fill.
     * @param color The color to fill the rectangle with.
     */
    Status FillRect(const Rect2i &rect, const std::vector<D> &color);

    /**
     * @brief Fills a circle with a color.
     * @param center The center of the circle.
     * @param radius The radius of the circle.
     * @param color The color to fill the circle with.
     */
    Status FillCircle(const Point2f &center, float radius, const std::vector<D> &color);

    /**
     * @brief Draws a point on the image.
     * @param point The point to draw.
     * @param thinkness The thickness of the point.
     * @param color The color of the point.
     */
    Status DrawPoint(const Point2f &point, float thinkness, const std::vector<D> &color);

    /**
     * @brief Draws a line on the image.
     * @param p0 The start point of the line.
     * @param p1 The end point of the line.
     * @param color The color of the line.
     * @param thickness The thickness of the line.
     */
    Status DrawLine(const Point2i &p0, const Point2i &p1, const std::vector<D> &color,
                    int thickness = 1);

    /**
     * @brief Draws a rectangle on the image.
     * @param rect The rectangle to draw.
     * @param color The color of the rectangle.
     * @param thickness The thickness of the rectangle.
     */
    Status DrawRect(const Rect2i &rect, const std::vector<D> &color, int thickness = 1);

    /**
     * @brief Gets the safe rectangle of the image.
     * @param rect The rectangle to get the safe rectangle of.
     * @return The safe rectangle.
     */
    Rect2i GetSafeRect(const Rect2i &rect) const;

    /**
     * @brief Gets the data pointer of the image.
     * @return The data pointer.
     */
    inline const D *Data() const {
        return is_external_ ? external_data_ : data_.get();
    }

    /**
     * @brief Gets the data pointer of the image.
     * @return The data pointer.
     */
    inline D *Data() {
        return is_external_ ? const_cast<D *>(external_data_) : data_.get();
    }

    /**
     * @brief Gets the width of the image.
     * @return The width of the image.
     */
    inline int Width() const {
        return width_;
    }

    /**
     * @brief Gets the height of the image.
     * @return The height of the image.
     */
    inline int Height() const {
        return height_;
    }

    /**
     * @brief Gets the number of channels of the image.
     * @return The number of channels of the image.
     */
    inline int Channels() const {
        return channels_;
    }

    /**
     * @brief Gets the data size of the image.
     * @return The data size of the image.
     */
    inline int DataSize() const {
        return height_ * width_ * channels_;
    }

    /**
     * @brief Gets the data pointer of the image at a specific pixel.
     * @param i The row index.
     * @param j The column index.
     * @return The data pointer.
     */
    inline const D *at(int i, int j) const {
        return Data() + (i * width_ + j) * channels_;
    }

    /**
     * @brief Gets the data pointer of the image at a specific pixel.
     * @param i The row index.
     * @param j The column index.
     * @return The data pointer.
     */
    inline D *at(int i, int j) {
        return Data() + (i * width_ + j) * channels_;
    }

    /**
     * @brief Gets the data pointer of the image at a specific row.
     * @param i The row index.
     * @return The data pointer.
     */
    inline const D *Row(int i) const {
        return Data() + i * width_ * channels_;
    }

    /**
     * @brief Gets the data pointer of the image at a specific row.
     * @param i The row index.
     * @return The data pointer.
     */
    inline D *Row(int i) {
        return Data() + i * width_ * channels_;
    }

#ifdef INSPIRECV_BACKEND_OKCV_USE_OPENCV
    /**
     * @brief Converts the image from a OpenCV Mat.
     * @param mat The OpenCV Mat.
     */
    void FromCVMat(const cv::Mat &mat, bool swap_channels = true);

    /**
     * @brief Converts the image to a OpenCV Mat.
     * @param mat The OpenCV Mat.
     */
    void ToCVMat(cv::Mat &mat, bool swap_channels = true) const;
#endif

protected:
    /**
     * @brief Interpolates the image using bilinear interpolation.
     * @param top_left The top left pixel value.
     * @param top_right The top right pixel value.
     * @param bottom_left The bottom left pixel value.
     * @param bottom_right The bottom right pixel value.
     * @param x_lerp The x interpolation factor.
     * @param y_lerp The y interpolation factor.
     * @return The interpolated pixel value.
     */
    inline D InterpolateBilinear(const D top_left, const D top_right, const D bottom_left,
                                 const D bottom_right, const float x_lerp,
                                 const float y_lerp) const;

    /**
     * @brief Interpolates the image using bilinear interpolation.
     * @param top_left_0 The top left pixel value.
     * @param top_left_1 The top left pixel value.
     * @param top_left_2 The top left pixel value.
     * @param top_left_3 The top left pixel value.
     * @param top_right_0 The top right pixel value.
     * @param top_right_1 The top right pixel value.
     * @param top_right_2 The top right pixel value.
     * @param top_right_3 The top right pixel value.
     * @param bottom_left_0 The bottom left pixel value.
     * @param bottom_left_1 The bottom left pixel value.
     * @param bottom_left_2 The bottom left pixel value.
     * @param bottom_left_3 The bottom left pixel value.
     * @param bottom_right_0 The bottom right pixel value.
     * @param bottom_right_1 The bottom right pixel value.
     * @param bottom_right_2 The bottom right pixel value.
     * @param bottom_right_3 The bottom right pixel value.
     * @param x_lerp The x interpolation factor.
     * @param y_lerp The y interpolation factor.
     * @param out The output pixel value.
     */
    inline void OutInterpolateBilinearFloatx4x1Neon(
      const D *top_left_0, const D *top_left_1, const D *top_left_2, const D *top_left_3,
      const D *top_right_0, const D *top_righ_1, const D *top_righ_2, const D *top_righ_3,
      const D *bottom_left_0, const D *bottom_left_1, const D *bottom_left_2,
      const D *bottom_left_3, const D *bottom_right_0, const D *bottom_right_1,
      const D *bottom_right_2, const D *bottom_right_3, const float *x_lerp, const float *y_lerp,
      D *out) const;

    int width_;
    int height_;
    int channels_;
    std::unique_ptr<D> data_;
    const D *external_data_ = nullptr;  // Add this
    bool is_external_ = false;          // Add this
};

}  // namespace okcv

#endif  // OKCV_IMAGE_H_