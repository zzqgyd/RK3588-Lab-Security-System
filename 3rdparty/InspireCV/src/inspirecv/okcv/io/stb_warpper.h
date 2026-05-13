#ifndef INSPIRECV_OKCV_IO_STB_WRAPPER_H_
#define INSPIRECV_OKCV_IO_STB_WRAPPER_H_

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <cctype>
#include "check.h"

namespace okcv {

class ImageReader {
public:
    // Image data structure
    struct ImageData {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> data;

        bool isValid() const {
            return !data.empty() && width > 0 && height > 0;
        }

        void clear() {
            width = height = channels = 0;
            data.clear();
        }

        // Get total bytes
        size_t getDataSize() const {
            return static_cast<size_t>(width) * height * channels;
        }
    };

    enum class ColorOrder {
        RGB,  // Keep stb_image's default RGB order
        BGR   // Convert to BGR order
    };

    // Write configuration structure
    struct WriteConfig {
        int jpg_quality;         // JPEG quality (1-100)
        int png_compression;     // PNG compression level (0-9)
        ColorOrder color_order;  // Color order

        // Constructor to initialize members
        WriteConfig() : jpg_quality(100), png_compression(9), color_order(ColorOrder::BGR) {}
    };

    // Error codes
    enum class ErrorCode {
        OK = 0,
        FILE_NOT_FOUND,
        INVALID_CHANNEL,
        DECODE_ERROR,
        INVALID_PARAMETER
    };

private:
    // Static member variable declaration compatible with C++14
    static std::string lastError;

public:
    // Get last error message
    static std::string GetLastError() {
        return lastError;
    }

    /**
     * @brief Write image data to file
     * @param filename Output file path
     * @param data Image data pointer
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels (1 or 3)
     * @param config Write configuration
     * @return true if successful, false otherwise
     */
    static bool Write(const std::string& filename, const unsigned char* data, int width, int height,
                      int channels, const WriteConfig& config = WriteConfig()) {
        if (!data || width <= 0 || height <= 0 || (channels != 1 && channels != 3)) {
            lastError = "Invalid input parameters";
            return false;
        }

        std::string ext = GetFileExtension(filename);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool success = false;

        // Convert color order if needed
        std::vector<unsigned char> rgb_data;
        const unsigned char* write_data = data;

        // Only convert if 3 channels and input is BGR
        if (channels == 3 && config.color_order == ColorOrder::BGR) {
            rgb_data.resize(static_cast<size_t>(width) * height * channels);
            for (int i = 0; i < width * height; ++i) {
                rgb_data[i * 3 + 0] = data[i * 3 + 2];  // R <- B
                rgb_data[i * 3 + 1] = data[i * 3 + 1];  // G <- G
                rgb_data[i * 3 + 2] = data[i * 3 + 0];  // B <- R
            }
            write_data = rgb_data.data();
        }

        // Choose save format based on extension
        if (ext == "png") {
            success = stbi_write_png(filename.c_str(), width, height, channels, write_data,
                                     width * channels) != 0;
        } else if (ext == "jpg" || ext == "jpeg") {
            success = stbi_write_jpg(filename.c_str(), width, height, channels, write_data,
                                     config.jpg_quality) != 0;
        } else if (ext == "bmp") {
            success = stbi_write_bmp(filename.c_str(), width, height, channels, write_data) != 0;
        } else {
            lastError = "Unsupported image format: " + ext;
            INSPIRECV_LOG(ERROR) << lastError;
            return false;
        }

        if (!success) {
            lastError = "Failed to write image: " + filename;
            INSPIRECV_LOG(ERROR) << lastError;
        }

        return success;
    }

    /**
     * @brief Write ImageData structure to file
     * @param filename Output file path
     * @param data Image data structure
     * @param config Write configuration
     * @return true if successful, false otherwise
     */
    static bool Write(const std::string& filename, const ImageData& data,
                      const WriteConfig& config = WriteConfig()) {
        if (!data.isValid()) {
            lastError = "Invalid image data";
            INSPIRECV_LOG(ERROR) << lastError;
            return false;
        }

        std::string ext = GetFileExtension(filename);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool success = false;

        // Convert color order if needed
        // stb_image_write expects RGB order
        std::vector<unsigned char> rgb_data;
        const unsigned char* write_data = data.data.data();

        // Only convert if 3 channels and input is BGR
        if (data.channels == 3 && config.color_order == ColorOrder::BGR) {
            rgb_data.resize(data.getDataSize());
            for (size_t i = 0; i < data.width * data.height; ++i) {
                rgb_data[i * 3 + 0] = data.data[i * 3 + 2];  // R <- B
                rgb_data[i * 3 + 1] = data.data[i * 3 + 1];  // G <- G
                rgb_data[i * 3 + 2] = data.data[i * 3 + 0];  // B <- R
            }
            write_data = rgb_data.data();
        }

        // Choose save format based on extension
        if (ext == "png") {
            success = stbi_write_png(filename.c_str(), data.width, data.height, data.channels,
                                     write_data, data.width * data.channels) != 0;
        } else if (ext == "jpg" || ext == "jpeg") {
            success = stbi_write_jpg(filename.c_str(), data.width, data.height, data.channels,
                                     write_data, config.jpg_quality) != 0;
        } else if (ext == "bmp") {
            success = stbi_write_bmp(filename.c_str(), data.width, data.height, data.channels,
                                     write_data) != 0;
        } else {
            lastError = "Unsupported image format: " + ext;
            INSPIRECV_LOG(ERROR) << lastError;
            return false;
        }

        if (!success) {
            lastError = "Failed to write image: " + filename;
            INSPIRECV_LOG(ERROR) << lastError;
        }

        return success;
    }

    /**
     * @brief Read image from file
     * @param filename Input file path
     * @param outData Output image data structure
     * @param requestedChannels Number of channels to read (1 or 3)
     * @param order Color order (default BGR to match OpenCV)
     * @return true if successful, false otherwise
     */
    static bool Read(const std::string& filename, ImageData& outData, int requestedChannels,
                     ColorOrder order = ColorOrder::BGR) {  // Default BGR to match OpenCV
        // Clear output data
        outData.clear();
        lastError.clear();

        // Validate requested channels
        if (requestedChannels != 1 && requestedChannels != 3) {
            lastError =
              "Only 1 or 3 channels supported (requested: " + std::to_string(requestedChannels) +
              ")";
            INSPIRECV_LOG(ERROR) << lastError;
            return false;
        }

        // Read image data
        int width, height, channels;
        unsigned char* data =
          stbi_load(filename.c_str(), &width, &height, &channels, requestedChannels);

        if (!data) {
            lastError = "Decode failed: ";
            lastError += stbi_failure_reason();
            return false;
        }

        // Set output data
        outData.width = width;
        outData.height = height;
        outData.channels = requestedChannels;

        // Calculate data size and copy data
        size_t dataSize = static_cast<size_t>(width) * height * requestedChannels;
        outData.data.resize(dataSize);

        // Direct copy for grayscale or if no color conversion needed
        if (requestedChannels == 1 || order == ColorOrder::RGB) {
            std::memcpy(outData.data.data(), data, dataSize);
        }
        // Convert to BGR if needed
        else if (order == ColorOrder::BGR && requestedChannels == 3) {
            for (int i = 0; i < width * height; ++i) {
                outData.data[i * 3 + 0] = data[i * 3 + 2];  // B <- R
                outData.data[i * 3 + 1] = data[i * 3 + 1];  // G <- G
                outData.data[i * 3 + 2] = data[i * 3 + 0];  // R <- B
            }
        }

        // Free stb allocated memory
        stbi_image_free(data);

        return true;
    }

    /**
     * @brief Read image information without loading pixel data
     * @param filename Input file path
     * @param width Output image width
     * @param height Output image height
     * @param channels Output number of channels
     * @return true if successful, false otherwise
     */
    static bool ReadInfo(const std::string& filename, int& width, int& height, int& channels) {
        lastError.clear();

        if (!stbi_info(filename.c_str(), &width, &height, &channels)) {
            lastError = "Failed to read image info: ";
            lastError += stbi_failure_reason();
            return false;
        }

        return true;
    }

    /**
     * @brief Check if file is a supported image format
     * @param filename Input file path
     * @return true if supported, false otherwise
     */
    static bool IsSupportedImage(const std::string& filename) {
        int width, height, channels;
        return stbi_info(filename.c_str(), &width, &height, &channels) != 0;
    }

    /**
     * @brief Get original number of channels in image
     * @param filename Input file path
     * @return Number of channels if successful, -1 on error
     */
    static int GetOriginalChannels(const std::string& filename) {
        int width, height, channels;
        if (stbi_info(filename.c_str(), &width, &height, &channels)) {
            return channels;
        }
        return -1;  // Error case
    }

    /**
     * @brief Get file extension
     * @param filename Input file path
     * @return File extension string
     */
    static std::string GetFileExtension(const std::string& filename) {
        size_t pos = filename.find_last_of('.');
        if (pos != std::string::npos && pos + 1 < filename.length()) {
            return filename.substr(pos + 1);
        }
        return "";
    }
};

// Static member variable definition compatible with C++14
std::string ImageReader::lastError;

}  // namespace okcv

#endif  // INSPIRECV_OKCV_IO_STB_WRAPPER_H_