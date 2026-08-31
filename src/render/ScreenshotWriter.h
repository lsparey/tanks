#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

// Writes a captured color image's raw pixel bytes to a PNG file on disk.
// Reading straight from GPU memory this way (see Application's
// ScreenshotRequest and the F12 key) works identically on any system that
// can run the app at all -- unlike an OS-level screenshot tool, which
// depends on the window manager/compositor's own capture path and can
// silently return a black frame for a window it doesn't composite the
// normal way (e.g. an XWayland surface under a Wayland compositor).
class ScreenshotWriter {
public:
    // `pixels` is a tightly packed (no row padding) width*height*4 byte
    // buffer, read directly off the swapchain image via a GPU->host copy
    // (see the capture block in Application::drawFrame). `format` must be
    // one of Vulkan's 8-bit-per-channel BGRA/RGBA formats; BGRA is swapped
    // to RGB order for the PNG since that's what the swapchain actually
    // uses (VK_FORMAT_B8G8R8A8_SRGB, see Swapchain::create).
    static void write(const std::string& path, const uint8_t* pixels, uint32_t width,
                       uint32_t height, VkFormat format);
};
