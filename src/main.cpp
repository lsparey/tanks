#include <cstdlib>
#include <cstring>
#include <charconv>
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
        bool profile = false;
        std::optional<uint32_t> seed;
        std::string view;
        bool originalTankModel = false;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--profile") == 0) profile = true;
            if (std::strcmp(argv[i], "--seed") == 0) {
                if (++i >= argc) throw std::runtime_error("--seed requires an unsigned integer");
                uint32_t value;
                const char* end = argv[i] + std::strlen(argv[i]);
                auto parsed = std::from_chars(argv[i], end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != end)
                    throw std::runtime_error("invalid --seed");
                seed = value;
            } else if (std::strcmp(argv[i], "--model") == 0) {
                if (++i >= argc) throw std::runtime_error("--model requires original or refined");
                if (std::strcmp(argv[i], "original") != 0 && std::strcmp(argv[i], "refined") != 0)
                    throw std::runtime_error("--model requires original or refined");
                originalTankModel = std::strcmp(argv[i], "original") == 0;
            } else if (std::strcmp(argv[i], "--view") == 0) {
                if (++i >= argc) throw std::runtime_error("--view requires tank, tank-side, tank-front, tank-rear, tank-top, landscape, water, or cliffs");
                view = argv[i];
                if (view != "tank" && view != "tank-side" && view != "tank-front" &&
                    view != "tank-rear" && view != "tank-top" &&
                    view != "landscape" && view != "water" && view != "cliffs")
                    throw std::runtime_error("unknown reference view");
            }
        }
        if (!view.empty() && !seed) seed = 7331;
        Application app(parseScreenshotRequest(argc, argv), profile, seed, view, originalTankModel);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
