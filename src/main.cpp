#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

#include "core/Application.h"

namespace {

// --screenshot <path> [--screenshot-frame <n>]: save a PNG of the rendered
// frame and exit, instead of running interactively -- see
// Application::ScreenshotRequest for why this reads GPU memory directly
// rather than going through any OS/window-manager screenshot tool. Meant
// for scripted/agent-driven verification of rendering changes on any
// system that can run the app at all.
std::optional<Application::ScreenshotRequest> parseScreenshotRequest(int argc, char** argv) {
    std::optional<Application::ScreenshotRequest> request;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            Application::ScreenshotRequest r;
            r.path = argv[++i];
            request = r;
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc && request) {
            request->atFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }
    return request;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Application app(parseScreenshotRequest(argc, argv));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
