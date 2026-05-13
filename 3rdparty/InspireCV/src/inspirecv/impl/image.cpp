#include <memory>
#include <cmath>
#include "impl.h"
#include "logging.h"

namespace inspirecv {

// Default destructor
Image::~Image() = default;

// Default constructor
Image::Image() {
    impl_ = std::make_unique<Impl>(0, 0, 0);
}

// Move constructor
Image::Image(Image&& other) noexcept = default;

// Move assignment operator
Image& Image::operator=(Image&& other) noexcept = default;

// Construct image with given parameters
Image::Image(int width, int height, int channels, const uint8_t* data, bool copy_data) {
    impl_ = std::make_unique<Impl>(width, height, channels, data, copy_data);
}

// Reset image
void Image::Reset(int width, int height, int channels, const uint8_t* data) {
    impl_->Reset(width, height, channels, data);
}

// Implementation of Clone method
Image Image::Clone() const {
    Image new_image = impl_->Clone();
    return new_image;
}

// Implementation of other methods remain unchanged
int Image::Width() const {
    return impl_->Width();
}

int Image::Height() const {
    return impl_->Height();
}

int Image::Channels() const {
    return impl_->Channels();
}

bool Image::Empty() const {
    return impl_->Empty();
}

const uint8_t* Image::Data() const {
    return impl_->Data();
}

void* Image::GetInternalImage() const {
    return impl_->GetInternalImage();
}

bool Image::Read(const std::string& filename, int channels) {
    return impl_->Read(filename, channels);
}

bool Image::Write(const std::string& filename) const {
    return impl_->Write(filename);
}

void Image::Show(const std::string& window_name, int delay) const {
    impl_->Show(window_name, delay);
}

void Image::Fill(double value) {
    impl_->Fill(value);
}

Image Image::Mul(double scale) const {
    return impl_->Mul(scale);
}

Image Image::Add(double value) const {
    return impl_->Add(value);
}

Image Image::Resize(int width, int height, bool use_linear) const {
    return impl_->Resize(width, height, use_linear);
}

Image Image::Crop(const Rect<int>& rect) const {
    return impl_->Crop(rect);
}

Image Image::WarpAffine(const TransformMatrix& matrix, int width, int height) const {
    return impl_->WarpAffine(matrix, width, height);
}

Image Image::Rotate90() const {
    return impl_->Rotate90();
}

Image Image::Rotate180() const {
    return impl_->Rotate180();
}

Image Image::Rotate270() const {
    return impl_->Rotate270();
}

Image Image::SwapRB() const {
    return impl_->SwapRB();
}

Image Image::FlipHorizontal() const {
    return impl_->FlipHorizontal();
}

Image Image::FlipVertical() const {
    return impl_->FlipVertical();
}

Image Image::Pad(int top, int bottom, int left, int right, const std::vector<double>& color) const {
    return impl_->Pad(top, bottom, left, right, color);
}

void Image::DrawLine(const Point<int>& p1, const Point<int>& p2, const std::vector<double>& color,
                     int thickness) {
    impl_->DrawLine(p1, p2, color, thickness);
}

void Image::DrawRect(const Rect<int>& rect, const std::vector<double>& color, int thickness) {
    impl_->DrawRect(rect, color, thickness);
}

void Image::DrawCircle(const Point<int>& center, int radius, const std::vector<double>& color,
                       int thickness) {
    impl_->DrawCircle(center, radius, color, thickness);
}

void Image::Fill(const Rect<int>& rect, const std::vector<double>& color) {
    impl_->Fill(rect, color);
}

Image Image::GaussianBlur(int kernel_size, double sigma) const {
    return impl_->GaussianBlur(kernel_size, sigma);
}

Image Image::Erode(int kernel_size, int iterations) const {
    return impl_->Erode(kernel_size, iterations);
}

Image Image::Dilate(int kernel_size, int iterations) const {
    return impl_->Dilate(kernel_size, iterations);
}

Image Image::Threshold(double thresh, double maxval, int type) const {
    return impl_->Threshold(thresh, maxval, type);
}

Image Image::ToGray() const {
    return impl_->ToGray();
}

Image Image::AbsDiff(const Image& other) const {
    return impl_->AbsDiff(*other.impl_);
}

Image Image::MeanChannels() const {
    return impl_->MeanChannels();
}

Image Image::Blend(const Image& other, const Image& mask) const {
    return impl_->Blend(*other.impl_, *mask.impl_);
}

Image Image::Create(int width, int height, int channels, const uint8_t* data, bool copy_data) {
    return Image(width, height, channels, data, copy_data);
}

Image Image::Create() {
    return Image(0, 0, 0);
}

Image Image::Create(const std::string& filename, int channels) {
    Image image;
    bool success = image.Read(filename, channels);
    if (!success) {
        INSPIRECV_LOG(WARN) << "Failed to read image from " << filename << " with channels "
                            << channels << " : " << (success ? "success" : "failure");
    }
    return image;
}

// Private constructor
Image::Image(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::ostream& operator<<(std::ostream& os, const Image& image) {
    image.impl_->Print(os);
    return os;
}

}  // namespace inspirecv
