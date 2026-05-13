#ifndef INSPIRECV_IMPL_OKCV_IMAGE_OKCV_H
#define INSPIRECV_IMPL_OKCV_IMAGE_OKCV_H

#include "okcv/okcv.h"
#include "inspirecv/core/image.h"
#include "inspirecv/core/rect.h"
#include "inspirecv/core/point.h"
#include "inspirecv/core/transform_matrix.h"
#include "logging.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  if defined(__has_include)
#    if __has_include(<arm_neon.h>)
#      include <arm_neon.h>
#    endif
#  else
#    include <arm_neon.h>
#  endif
#endif

#if defined(__AVX2__) || defined(__SSE2__)
#  if defined(__has_include)
#    if __has_include(<immintrin.h>)
#      include <immintrin.h>
#    endif
#  else
#    include <immintrin.h>
#  endif
#endif

namespace inspirecv {

class Image::Impl {
public:
    // Creation and conversion
    Impl(const okcv::Image<uint8_t>& mat) {
        image_ = mat.Clone();
    }

    Impl() : image_() {}

    Impl(int width, int height, int channels, const uint8_t* data = nullptr,
         bool copy_data = true) {
        image_.Reset(width, height, channels, data, copy_data);
    }

    void Reset(int width, int height, int channels, const uint8_t* data = nullptr,
               bool copy_data = true) {
        image_.Reset(width, height, channels, data, copy_data);
    }

    Image Clone() const {
        Image result;
        result.impl_->image_ = image_.Clone();
        return result;
    }

    ~Impl() = default;

    // Basic properties
    int Width() const {
        return image_.Width();
    }
    int Height() const {
        return image_.Height();
    }
    int Channels() const {
        return image_.Channels();
    }
    bool Empty() const {
        return image_.Empty();
    }
    const uint8_t* Data() const {
        return image_.Data();
    }

    // Get internal image implementation
    void* GetInternalImage() const {
        return static_cast<void*>(const_cast<uint8_t*>(image_.Data()));
    }

    // I/O operations
    bool Read(const std::string& filename, int channels) {
        image_.Read(filename, channels);
        return !Empty();
    }

    bool Write(const std::string& filename) const {
        image_.Write(filename);
        return true;
    }

    void Show(const std::string& window_name, int delay) const {
        image_.Show(window_name, delay);
    }

    // Basic operations
    void Fill(double value) {
        image_.Fill(static_cast<uint8_t>(value));
    }

    Image Mul(double scale) const {
        Image result;
        result.impl_->image_ = image_.Mul(static_cast<float>(scale));
        return result;
    }

    Image Add(double value) const {
        Image result;
        result.impl_->image_ = image_.MulAdd(1.0f, static_cast<float>(value));
        return result;
    }

    // Geometric transformations
    Image Resize(int width, int height, bool use_linear) const {
        Image result;
        if (use_linear) {
            result.impl_->image_ = image_.ResizeBilinear(width, height);
        } else {
            result.impl_->image_ = image_.ResizeNearest(width, height);
        }
        return result;
    }

    Image Crop(const Rect<int>& rect) const {
        Image result;
        int x2 = rect.GetX() + std::max(1, rect.GetWidth());
        int y2 = rect.GetY() + std::max(1, rect.GetHeight());

        okcv::Rect2i okcv_rect(rect.GetX(), rect.GetY(), x2, y2);

        result.impl_->image_ = image_.Crop(okcv_rect);
        return result;
    }

    Image WarpAffine(const TransformMatrix& matrix, int width, int height) const {
        Image result;
        okcv::TransformMatrix okcv_matrix =
          *static_cast<okcv::TransformMatrix*>(matrix.GetInternalMatrix());
        result.impl_->image_ = image_.AffineBilinear(width, height, okcv_matrix);
        return result;
    }

    Image Rotate90() const {
        Image result;
        result.impl_->image_ = image_.Rotate90();
        return result;
    }

    Image Rotate180() const {
        Image result;
        result.impl_->image_ = image_.Rotate180();
        return result;
    }

    Image Rotate270() const {
        Image result;
        result.impl_->image_ = image_.Rotate270();
        return result;
    }

    Image SwapRB() const {
        Image result;
        result.impl_->image_ = image_.SwapRB();
        return result;
    }

    Image FlipHorizontal() const {
        Image result;
        result.impl_->image_ = image_.FlipLeftRight();
        return result;
    }

    Image FlipVertical() const {
        Image result;
        result.impl_->image_ = image_.FlipUpDown();
        return result;
    }

    Image Pad(int top, int bottom, int left, int right, const std::vector<double>& color) const {
        Image result;
        result.impl_->image_ = image_.Pad(top, bottom, left, right, static_cast<uint8_t>(color[0]));
        return result;
    }

    // Image processing
    Image GaussianBlur(int kernel_size, double sigma) const {
        Image result;
        result.impl_->image_ = image_.GaussianBlur(kernel_size, static_cast<float>(sigma));
        return result;
    }

    Image Erode(int kernel_size, int iterations) const {
        INSPIRECV_CHECK(image_.Channels() == 1) << "Erode only supports single-channel";
        int left = kernel_size / 2;
        int right = kernel_size - left - 1;
        int top = kernel_size / 2;
        int bottom = kernel_size - top - 1;
        Image result;
        okcv::Image<uint8_t> cur = image_.Clone();
        for (int it = 0; it < std::max(1, iterations); ++it) {
            okcv::Image<uint8_t> tmp = cur.MinFilter(left, right, top, bottom);
            cur = std::move(tmp);
        }
        result.impl_->image_ = std::move(cur);
        return result;
    }

    Image Dilate(int kernel_size, int iterations) const {
        INSPIRECV_CHECK(image_.Channels() == 1) << "Dilate only supports single-channel";
        int left = kernel_size / 2;
        int right = kernel_size - left - 1;
        int top = kernel_size / 2;
        int bottom = kernel_size - top - 1;
        Image result;
        okcv::Image<uint8_t> cur = image_.Clone();
        for (int it = 0; it < std::max(1, iterations); ++it) {
            okcv::Image<uint8_t> tmp = cur.MaxFilter(left, right, top, bottom);
            cur = std::move(tmp);
        }
        result.impl_->image_ = std::move(cur);
        return result;
    }

    Image Threshold(double thresh, double maxval, int type) const {
        // Support binary only: type==0 -> THRESH_BINARY
        INSPIRECV_CHECK(image_.Channels() == 1);
        Image result;
        result.impl_->image_.Reset(image_.Width(), image_.Height(), 1);
        const uint8_t* src = image_.Data();
        uint8_t* dst = result.impl_->image_.Data();
        const int total = image_.DataSize();
        const uint8_t t = static_cast<uint8_t>(thresh);
        const uint8_t mv = static_cast<uint8_t>(maxval);
        for (int i = 0; i < total; ++i) dst[i] = src[i] >= t ? mv : 0;
        return result;
    }

    Image AbsDiff(const Image::Impl& other) const {
        INSPIRECV_CHECK(image_.Width() == other.Width() && image_.Height() == other.Height() &&
                        image_.Channels() == other.Channels());
        Image result;
        result.impl_->image_.Reset(image_.Width(), image_.Height(), image_.Channels());
        const uint8_t* a = image_.Data();
        const uint8_t* b = other.image_.Data();
        uint8_t* d = result.impl_->image_.Data();
        const int n = image_.DataSize();
        for (int i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(std::abs(int(a[i]) - int(b[i])));
        return result;
    }

    Image MeanChannels() const {
        INSPIRECV_CHECK(image_.Channels() >= 1);
        if (image_.Channels() == 1) return Clone();
        Image result;
        result.impl_->image_.Reset(image_.Width(), image_.Height(), 1);
        for (int y = 0; y < image_.Height(); ++y) {
            const uint8_t* row = image_.Row(y);
            uint8_t* out = result.impl_->image_.Row(y);
            for (int x = 0; x < image_.Width(); ++x) {
                int s = 0;
                for (int c = 0; c < image_.Channels(); ++c) s += row[x * image_.Channels() + c];
                out[x] = static_cast<uint8_t>(s / image_.Channels());
            }
        }
        return result;
    }

    Image Blend(const Image::Impl& other, const Image::Impl& mask) const {
        INSPIRECV_CHECK(image_.Width() == other.Width() && image_.Height() == other.Height() &&
                        image_.Channels() == other.Channels());
        INSPIRECV_CHECK(mask.Channels() == 1 && mask.Width() == image_.Width() &&
                        mask.Height() == image_.Height());

        const int width = image_.Width();
        const int height = image_.Height();
        const int cn = image_.Channels();

        Image result;
        result.impl_->image_.Reset(width, height, cn);

        const uint8_t* a = image_.Data();
        const uint8_t* b = other.image_.Data();
        const uint8_t* m = mask.image_.Data();
        uint8_t* d = result.impl_->image_.Data();

        // Scalar per-channel blender with exact round(x/255)
        auto blend_u8 = [](uint8_t va, uint8_t vb, uint8_t mm) -> uint8_t {
            // x = m*(a-b) + 255*b, then out = round(x / 255)
            int x = static_cast<int>(mm) * (static_cast<int>(va) - static_cast<int>(vb))
                    + ((static_cast<int>(vb) << 8) - static_cast<int>(vb));
            int t = x + 128;  // for rounding
            return static_cast<uint8_t>((t + (t >> 8)) >> 8);
        };

        // Parallelize rows optionally
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < height; ++y) {
            const uint8_t* pa = a + static_cast<size_t>(y) * width * cn;
            const uint8_t* pb = b + static_cast<size_t>(y) * width * cn;
            const uint8_t* pm = m + static_cast<size_t>(y) * width;
            uint8_t* pd = d + static_cast<size_t>(y) * width * cn;

            if (cn == 1) {
                int x = 0;

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__has_include) && __has_include(<arm_neon.h>)
                // NEON fast path: process 8 pixels per iteration with 32-bit accumulation
                for (; x + 8 <= width; x += 8) {
                    uint8x8_t m8 = vld1_u8(pm + x);
                    uint8x8_t a8 = vld1_u8(pa + x);
                    uint8x8_t b8 = vld1_u8(pb + x);

                    uint16x8_t m16 = vmovl_u8(m8);
                    uint16x8_t a16 = vmovl_u8(a8);
                    uint16x8_t b16 = vmovl_u8(b8);
                    uint16x8_t inv16 = vsubq_u16(vdupq_n_u16(255), m16);

                    uint16x4_t mlo16 = vget_low_u16(m16);
                    uint16x4_t mhi16 = vget_high_u16(m16);
                    uint16x4_t alo16 = vget_low_u16(a16);
                    uint16x4_t ahi16 = vget_high_u16(a16);
                    uint16x4_t blo16 = vget_low_u16(b16);
                    uint16x4_t bhi16 = vget_high_u16(b16);
                    uint16x4_t invlo16 = vget_low_u16(inv16);
                    uint16x4_t invhi16 = vget_high_u16(inv16);

                    uint32x4_t lo = vaddq_u32(vmull_u16(mlo16, alo16), vmull_u16(invlo16, blo16));
                    uint32x4_t hi = vaddq_u32(vmull_u16(mhi16, ahi16), vmull_u16(invhi16, bhi16));

                    lo = vaddq_u32(lo, vdupq_n_u32(128));
                    hi = vaddq_u32(hi, vdupq_n_u32(128));
                    lo = vaddq_u32(lo, vshrq_n_u32(lo, 8));
                    hi = vaddq_u32(hi, vshrq_n_u32(hi, 8));
                    lo = vshrq_n_u32(lo, 8);
                    hi = vshrq_n_u32(hi, 8);

                    uint16x4_t lo16 = vmovn_u32(lo);
                    uint16x4_t hi16 = vmovn_u32(hi);
                    uint16x8_t out16 = vcombine_u16(lo16, hi16);
                    uint8x8_t out8 = vmovn_u16(out16);
                    vst1_u8(pd + x, out8);
                }
#elif defined(__AVX2__)
                // AVX2 fast path: process 32 pixels per iteration with 32-bit accumulation
                for (; x + 32 <= width; x += 32) {
                    __m128i m0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pm + x));
                    __m128i m1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pm + x + 16));
                    __m128i a0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pa + x));
                    __m128i a1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pa + x + 16));
                    __m128i b0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pb + x));
                    __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pb + x + 16));

                    __m256i m16_0 = _mm256_cvtepu8_epi16(m0);
                    __m256i m16_1 = _mm256_cvtepu8_epi16(m1);
                    __m256i a16_0 = _mm256_cvtepu8_epi16(a0);
                    __m256i a16_1 = _mm256_cvtepu8_epi16(a1);
                    __m256i b16_0 = _mm256_cvtepu8_epi16(b0);
                    __m256i b16_1 = _mm256_cvtepu8_epi16(b1);

                    __m256i inv16_0 = _mm256_sub_epi16(_mm256_set1_epi16(255), m16_0);
                    __m256i inv16_1 = _mm256_sub_epi16(_mm256_set1_epi16(255), m16_1);

                    __m256i p1_0 = _mm256_mullo_epi16(m16_0, a16_0);
                    __m256i p2_0 = _mm256_mullo_epi16(inv16_0, b16_0);
                    __m256i p1_1 = _mm256_mullo_epi16(m16_1, a16_1);
                    __m256i p2_1 = _mm256_mullo_epi16(inv16_1, b16_1);

                    __m128i p1_0_lo128 = _mm256_castsi256_si128(p1_0);
                    __m128i p1_0_hi128 = _mm256_extracti128_si256(p1_0, 1);
                    __m128i p2_0_lo128 = _mm256_castsi256_si128(p2_0);
                    __m128i p2_0_hi128 = _mm256_extracti128_si256(p2_0, 1);
                    __m128i p1_1_lo128 = _mm256_castsi256_si128(p1_1);
                    __m128i p1_1_hi128 = _mm256_extracti128_si256(p1_1, 1);
                    __m128i p2_1_lo128 = _mm256_castsi256_si128(p2_1);
                    __m128i p2_1_hi128 = _mm256_extracti128_si256(p2_1, 1);

                    __m256i s0_lo = _mm256_add_epi32(_mm256_cvtepu16_epi32(p1_0_lo128), _mm256_cvtepu16_epi32(p2_0_lo128));
                    __m256i s0_hi = _mm256_add_epi32(_mm256_cvtepu16_epi32(p1_0_hi128), _mm256_cvtepu16_epi32(p2_0_hi128));
                    __m256i s1_lo = _mm256_add_epi32(_mm256_cvtepu16_epi32(p1_1_lo128), _mm256_cvtepu16_epi32(p2_1_lo128));
                    __m256i s1_hi = _mm256_add_epi32(_mm256_cvtepu16_epi32(p1_1_hi128), _mm256_cvtepu16_epi32(p2_1_hi128));

                    __m256i t0_lo = _mm256_add_epi32(s0_lo, _mm256_set1_epi32(128));
                    __m256i t0_hi = _mm256_add_epi32(s0_hi, _mm256_set1_epi32(128));
                    __m256i t1_lo = _mm256_add_epi32(s1_lo, _mm256_set1_epi32(128));
                    __m256i t1_hi = _mm256_add_epi32(s1_hi, _mm256_set1_epi32(128));

                    t0_lo = _mm256_add_epi32(t0_lo, _mm256_srli_epi32(t0_lo, 8));
                    t0_hi = _mm256_add_epi32(t0_hi, _mm256_srli_epi32(t0_hi, 8));
                    t1_lo = _mm256_add_epi32(t1_lo, _mm256_srli_epi32(t1_lo, 8));
                    t1_hi = _mm256_add_epi32(t1_hi, _mm256_srli_epi32(t1_hi, 8));

                    t0_lo = _mm256_srli_epi32(t0_lo, 8);
                    t0_hi = _mm256_srli_epi32(t0_hi, 8);
                    t1_lo = _mm256_srli_epi32(t1_lo, 8);
                    t1_hi = _mm256_srli_epi32(t1_hi, 8);

                    __m256i pack16_0 = _mm256_packs_epi32(t0_lo, t0_hi);
                    __m256i pack16_1 = _mm256_packs_epi32(t1_lo, t1_hi);
                    __m256i out8 = _mm256_packus_epi16(pack16_0, pack16_1);

                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(pd + x), out8);
                }
#elif defined(__SSE2__)
                // SSE2 fast path: process 16 pixels per iteration
                for (; x + 16 <= width; x += 16) {
                    __m128i m8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pm + x));
                    __m128i a8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pa + x));
                    __m128i b8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pb + x));

                    __m128i zero = _mm_setzero_si128();
                    __m128i mlo = _mm_unpacklo_epi8(m8, zero);
                    __m128i mhi = _mm_unpackhi_epi8(m8, zero);
                    __m128i alo = _mm_unpacklo_epi8(a8, zero);
                    __m128i ahi = _mm_unpackhi_epi8(a8, zero);
                    __m128i blo = _mm_unpacklo_epi8(b8, zero);
                    __m128i bhi = _mm_unpackhi_epi8(b8, zero);

                    __m128i invlo = _mm_sub_epi16(_mm_set1_epi16(255), mlo);
                    __m128i invhi = _mm_sub_epi16(_mm_set1_epi16(255), mhi);

                    __m128i p1lo = _mm_mullo_epi16(mlo, alo);
                    __m128i p2lo = _mm_mullo_epi16(invlo, blo);
                    __m128i p1hi = _mm_mullo_epi16(mhi, ahi);
                    __m128i p2hi = _mm_mullo_epi16(invhi, bhi);

                    __m128i p1lo_lo32 = _mm_unpacklo_epi16(p1lo, zero);
                    __m128i p1lo_hi32 = _mm_unpackhi_epi16(p1lo, zero);
                    __m128i p2lo_lo32 = _mm_unpacklo_epi16(p2lo, zero);
                    __m128i p2lo_hi32 = _mm_unpackhi_epi16(p2lo, zero);
                    __m128i p1hi_lo32 = _mm_unpacklo_epi16(p1hi, zero);
                    __m128i p1hi_hi32 = _mm_unpackhi_epi16(p1hi, zero);
                    __m128i p2hi_lo32 = _mm_unpacklo_epi16(p2hi, zero);
                    __m128i p2hi_hi32 = _mm_unpackhi_epi16(p2hi, zero);

                    __m128i s0_lo = _mm_add_epi32(p1lo_lo32, p2lo_lo32);
                    __m128i s0_hi = _mm_add_epi32(p1lo_hi32, p2lo_hi32);
                    __m128i s1_lo = _mm_add_epi32(p1hi_lo32, p2hi_lo32);
                    __m128i s1_hi = _mm_add_epi32(p1hi_hi32, p2hi_hi32);

                    __m128i t0_lo = _mm_add_epi32(s0_lo, _mm_set1_epi32(128));
                    __m128i t0_hi = _mm_add_epi32(s0_hi, _mm_set1_epi32(128));
                    __m128i t1_lo = _mm_add_epi32(s1_lo, _mm_set1_epi32(128));
                    __m128i t1_hi = _mm_add_epi32(s1_hi, _mm_set1_epi32(128));

                    t0_lo = _mm_add_epi32(t0_lo, _mm_srli_epi32(t0_lo, 8));
                    t0_hi = _mm_add_epi32(t0_hi, _mm_srli_epi32(t0_hi, 8));
                    t1_lo = _mm_add_epi32(t1_lo, _mm_srli_epi32(t1_lo, 8));
                    t1_hi = _mm_add_epi32(t1_hi, _mm_srli_epi32(t1_hi, 8));

                    t0_lo = _mm_srli_epi32(t0_lo, 8);
                    t0_hi = _mm_srli_epi32(t0_hi, 8);
                    t1_lo = _mm_srli_epi32(t1_lo, 8);
                    t1_hi = _mm_srli_epi32(t1_hi, 8);

                    __m128i pack16_0 = _mm_packs_epi32(t0_lo, t0_hi);
                    __m128i pack16_1 = _mm_packs_epi32(t1_lo, t1_hi);
                    __m128i out8 = _mm_packus_epi16(pack16_0, pack16_1);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(pd + x), out8);
                }
#endif

                // Scalar tail (and scalar-only path)
                for (; x < width; ++x) {
                    pd[x] = blend_u8(pa[x], pb[x], pm[x]);
                }
            } else if (cn == 3) {
                for (int x = 0; x < width; ++x) {
                    uint8_t mm = pm[x];
                    int base = x * 3;
                    pd[base + 0] = blend_u8(pa[base + 0], pb[base + 0], mm);
                    pd[base + 1] = blend_u8(pa[base + 1], pb[base + 1], mm);
                    pd[base + 2] = blend_u8(pa[base + 2], pb[base + 2], mm);
                }
            } else if (cn == 4) {
                int x = 0;
#if defined(__AVX2__)
                const __m256i shuf = _mm256_setr_epi8(
                  0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3,
                  4,4,4,4, 5,5,5,5, 6,6,6,6, 7,7,7,7);
                for (; x + 8 <= width; x += 8) {
                    __m128i m8_128 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(pm + x));
                    __m256i m8_256 = _mm256_broadcastsi128_si256(m8_128);
                    __m256i mb = _mm256_shuffle_epi8(m8_256, shuf); // 8 masks -> 32 bytes (RGBA replicated)

                    const uint8_t* pa4 = pa + x * 4;
                    const uint8_t* pb4 = pb + x * 4;
                    uint8_t* pd4 = pd + x * 4;

                    __m256i a8 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pa4));
                    __m256i b8 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pb4));
                    __m256i zero = _mm256_setzero_si256();
                    __m256i a_lo16 = _mm256_unpacklo_epi8(a8, zero);
                    __m256i a_hi16 = _mm256_unpackhi_epi8(a8, zero);
                    __m256i b_lo16 = _mm256_unpacklo_epi8(b8, zero);
                    __m256i b_hi16 = _mm256_unpackhi_epi8(b8, zero);
                    __m256i m_lo16 = _mm256_unpacklo_epi8(mb, zero);
                    __m256i m_hi16 = _mm256_unpackhi_epi8(mb, zero);
                    __m256i inv_lo16 = _mm256_sub_epi16(_mm256_set1_epi16(255), m_lo16);
                    __m256i inv_hi16 = _mm256_sub_epi16(_mm256_set1_epi16(255), m_hi16);

                    __m256i p1_lo16 = _mm256_mullo_epi16(m_lo16, a_lo16);
                    __m256i p2_lo16 = _mm256_mullo_epi16(inv_lo16, b_lo16);
                    __m256i p1_hi16 = _mm256_mullo_epi16(m_hi16, a_hi16);
                    __m256i p2_hi16 = _mm256_mullo_epi16(inv_hi16, b_hi16);

                    __m256i p1_lo_lo32 = _mm256_unpacklo_epi16(p1_lo16, zero);
                    __m256i p1_lo_hi32 = _mm256_unpackhi_epi16(p1_lo16, zero);
                    __m256i p2_lo_lo32 = _mm256_unpacklo_epi16(p2_lo16, zero);
                    __m256i p2_lo_hi32 = _mm256_unpackhi_epi16(p2_lo16, zero);
                    __m256i p1_hi_lo32 = _mm256_unpacklo_epi16(p1_hi16, zero);
                    __m256i p1_hi_hi32 = _mm256_unpackhi_epi16(p1_hi16, zero);
                    __m256i p2_hi_lo32 = _mm256_unpacklo_epi16(p2_hi16, zero);
                    __m256i p2_hi_hi32 = _mm256_unpackhi_epi16(p2_hi16, zero);

                    __m256i s0_lo = _mm256_add_epi32(p1_lo_lo32, p2_lo_lo32);
                    __m256i s0_hi = _mm256_add_epi32(p1_lo_hi32, p2_lo_hi32);
                    __m256i s1_lo = _mm256_add_epi32(p1_hi_lo32, p2_hi_lo32);
                    __m256i s1_hi = _mm256_add_epi32(p1_hi_hi32, p2_hi_hi32);

                    __m256i c128 = _mm256_set1_epi32(128);
                    s0_lo = _mm256_add_epi32(s0_lo, c128);
                    s0_hi = _mm256_add_epi32(s0_hi, c128);
                    s1_lo = _mm256_add_epi32(s1_lo, c128);
                    s1_hi = _mm256_add_epi32(s1_hi, c128);

                    s0_lo = _mm256_add_epi32(s0_lo, _mm256_srli_epi32(s0_lo, 8));
                    s0_hi = _mm256_add_epi32(s0_hi, _mm256_srli_epi32(s0_hi, 8));
                    s1_lo = _mm256_add_epi32(s1_lo, _mm256_srli_epi32(s1_lo, 8));
                    s1_hi = _mm256_add_epi32(s1_hi, _mm256_srli_epi32(s1_hi, 8));

                    __m256i q0_lo = _mm256_srli_epi32(s0_lo, 8);
                    __m256i q0_hi = _mm256_srli_epi32(s0_hi, 8);
                    __m256i q1_lo = _mm256_srli_epi32(s1_lo, 8);
                    __m256i q1_hi = _mm256_srli_epi32(s1_hi, 8);

                    __m256i pack16_0 = _mm256_packs_epi32(q0_lo, q0_hi);
                    __m256i pack16_1 = _mm256_packs_epi32(q1_lo, q1_hi);
                    __m256i out8 = _mm256_packus_epi16(pack16_0, pack16_1);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(pd4), out8);
                }
#endif
                for (; x < width; ++x) {
                    uint8_t mm = pm[x];
                    int base = x * 4;
                    pd[base + 0] = blend_u8(pa[base + 0], pb[base + 0], mm);
                    pd[base + 1] = blend_u8(pa[base + 1], pb[base + 1], mm);
                    pd[base + 2] = blend_u8(pa[base + 2], pb[base + 2], mm);
                    pd[base + 3] = blend_u8(pa[base + 3], pb[base + 3], mm);
                }
            } else {
                for (int x = 0; x < width; ++x) {
                    uint8_t mm = pm[x];
                    int base = x * cn;
                    for (int c = 0; c < cn; ++c) {
                        pd[base + c] = blend_u8(pa[base + c], pb[base + c], mm);
                    }
                }
            }
        }

        return result;
    }

    // Drawing operations
    void DrawLine(const Point<int>& p1, const Point<int>& p2, const std::vector<double>& color,
                  int thickness = 1) {
        okcv::Point2i start = *static_cast<okcv::Point2i*>(p1.GetInternalPoint());
        okcv::Point2i end = *static_cast<okcv::Point2i*>(p2.GetInternalPoint());
        std::vector<uint8_t> okcv_color;
        for (const auto& c : color) {
            okcv_color.push_back(static_cast<uint8_t>(c));
        }
        image_.DrawLine(start, end, okcv_color, thickness);
    }

    void DrawRect(const Rect<int>& rect, const std::vector<double>& color, int thickness = 1) {
        okcv::Rect2i okcv_rect = *static_cast<okcv::Rect2i*>(rect.GetInternalRect());
        std::vector<uint8_t> okcv_color;
        for (const auto& c : color) {
            okcv_color.push_back(static_cast<uint8_t>(c));
        }
        image_.DrawRect(okcv_rect, okcv_color, thickness);
    }

    void DrawCircle(const Point<int>& center, int radius, const std::vector<double>& color,
                    int thickness = 1) {
        okcv::Point2f center_point =
          okcv::Point2f(static_cast<okcv::Point2i*>(center.GetInternalPoint())->x,
                        static_cast<okcv::Point2i*>(center.GetInternalPoint())->y);
        std::vector<uint8_t> okcv_color;
        for (const auto& c : color) {
            okcv_color.push_back(static_cast<uint8_t>(c));
        }
        image_.DrawPoint(center_point, static_cast<float>(thickness), okcv_color);
    }

    void Fill(const Rect<int>& rect, const std::vector<double>& color) {
        okcv::Rect2i okcv_rect = *static_cast<okcv::Rect2i*>(rect.GetInternalRect());
        std::vector<uint8_t> okcv_color;
        for (const auto& c : color) {
            okcv_color.push_back(static_cast<uint8_t>(c));
        }
        image_.FillRect(okcv_rect, okcv_color);
    }

    // Image format conversion
    Image ToGray() const {
        Image result;
        result.impl_->image_ = image_.RgbToGray();
        return result;
    }

    void Print(std::ostream& os) const {
        const int N = 10;  // Threshold for truncated display
        if (image_.Height() > N || image_.Width() > N) {
            // For large matrices, show truncated view
            os << "[";
            for (int i = 0; i < std::min(3, image_.Height()); i++) {
                if (i > 0)
                    os << " ";
                os << "[";
                // Show first 3 elements
                for (int j = 0; j < std::min(3, image_.Width()); j++) {
                    if (j > 0)
                        os << " ";
                    if (image_.Channels() == 1) {
                        os << (int)*(image_.at(i, j));
                    } else {
                        os << "[";
                        for (int c = 0; c < image_.Channels(); c++) {
                            if (c > 0)
                                os << " ";
                            os << (int)*(image_.at(i, j) + c);
                        }
                        os << "]";
                    }
                }
                if (image_.Width() > 3)
                    os << " ... ";
                // Show last 3 elements if there are more columns
                if (image_.Width() > 6) {
                    for (int j = image_.Width() - 3; j < image_.Width(); j++) {
                        if (image_.Channels() == 1) {
                            os << (int)*(image_.at(i, j)) << " ";
                        } else {
                            os << "[";
                            for (int c = 0; c < image_.Channels(); c++) {
                                if (c > 0)
                                    os << " ";
                                os << (int)*(image_.at(i, j) + c);
                            }
                            os << "] ";
                        }
                    }
                }
                os << "]\n";
            }
            if (image_.Height() > 6) {
                os << "...\n";
                // Show last 3 rows
                for (int i = image_.Height() - 3; i < image_.Height(); i++) {
                    os << "[";
                    for (int j = 0; j < std::min(3, image_.Width()); j++) {
                        if (j > 0)
                            os << " ";
                        if (image_.Channels() == 1) {
                            os << (int)*(image_.at(i, j));
                        } else {
                            os << "[";
                            for (int c = 0; c < image_.Channels(); c++) {
                                if (c > 0)
                                    os << " ";
                                os << (int)*(image_.at(i, j) + c);
                            }
                            os << "]";
                        }
                    }
                    if (image_.Width() > 3)
                        os << " ... ";
                    if (image_.Width() > 6) {
                        for (int j = image_.Width() - 3; j < image_.Width(); j++) {
                            if (image_.Channels() == 1) {
                                os << (int)*(image_.at(i, j)) << " ";
                            } else {
                                os << "[";
                                for (int c = 0; c < image_.Channels(); c++) {
                                    if (c > 0)
                                        os << " ";
                                    os << (int)*(image_.at(i, j) + c);
                                }
                                os << "] ";
                            }
                        }
                    }
                    os << "]\n";
                }
            }
            os << "]\n";
        } else {
            // For small matrices, show full content
            os << "[";
            for (int i = 0; i < image_.Height(); i++) {
                if (i > 0)
                    os << " ";
                os << "[";
                for (int j = 0; j < image_.Width(); j++) {
                    if (j > 0)
                        os << " ";
                    if (image_.Channels() == 1) {
                        os << (int)*(image_.at(i, j));
                    } else {
                        os << "[";
                        for (int c = 0; c < image_.Channels(); c++) {
                            if (c > 0)
                                os << " ";
                            os << (int)*(image_.at(i, j) + c);
                        }
                        os << "]";
                    }
                }
                os << "]\n";
            }
            os << "]\n";
        }
        os << "Size(H x W x C): " << image_.Height() << " x " << image_.Width() << " x "
           << image_.Channels() << "\n";
    }

private:
    okcv::Image<uint8_t> image_;
};

}  // namespace inspirecv

#endif  // INSPIRECV_IMPL_OKCV_IMAGE_OKCV_H
