#include "ScreenshotWriter.h"

#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

bool isBgraFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return true;
        default:
            return false;
    }
}

}  // namespace

void ScreenshotWriter::write(const std::string& path, const uint8_t* pixels, uint32_t width,
                              uint32_t height, VkFormat format) {
    size_t byteCount = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> rgba(pixels, pixels + byteCount);
    if (isBgraFormat(format)) {
        for (size_t i = 0; i < byteCount; i += 4) std::swap(rgba[i], rgba[i + 2]);
    }

    std::filesystem::path outPath(path);
    if (outPath.has_parent_path()) std::filesystem::create_directories(outPath.parent_path());

    int stride = static_cast<int>(width) * 4;
    if (!stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                         stride)) {
        throw std::runtime_error("Failed to write screenshot PNG: " + path);
    }
}
