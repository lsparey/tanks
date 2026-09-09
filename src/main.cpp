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
        int treeShadowMode = 2;
        bool shadowPreview = false, freezeWind = false;
        auto treeLodMode = Application::TreeLodMode::Reduced;
        bool treeLodBenchmark = false;
        std::optional<uint32_t> seed;
        std::string view;
        bool originalTankModel = false;
        bool animateTracks = true;
        bool weaponPreview = false;
        bool valleyTerrain = false;
        bool showTerrainMenu = false;
        std::optional<MacroTerrain::Landform> landform;
        uint32_t terrainAttempts = 1;
        std::optional<int> terrainResolution;
        int refinementPasses = 0;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--tree-lod-benchmark") == 0) treeLodBenchmark = true;
            if (std::strcmp(argv[i], "--tree-lod") == 0) {
                if (++i >= argc) throw std::runtime_error("--tree-lod requires reduced, previous, far, hidden, or full");
                if (std::strcmp(argv[i], "reduced") == 0) treeLodMode = Application::TreeLodMode::Reduced;
                else if (std::strcmp(argv[i], "previous") == 0) treeLodMode = Application::TreeLodMode::Previous;
                else if (std::strcmp(argv[i], "far") == 0) treeLodMode = Application::TreeLodMode::Far;
                else if (std::strcmp(argv[i], "hidden") == 0) treeLodMode = Application::TreeLodMode::Hidden;
                else if (std::strcmp(argv[i], "full") == 0) treeLodMode = Application::TreeLodMode::Full;
                else throw std::runtime_error("--tree-lod requires reduced, previous, far, hidden, or full");
            }
            if (std::strcmp(argv[i], "--shadow-preview") == 0) shadowPreview = true;
            if (std::strcmp(argv[i], "--freeze-wind") == 0) freezeWind = true;
            if (std::strcmp(argv[i], "--tree-shadows") == 0) {
                if (++i >= argc) throw std::runtime_error("--tree-shadows requires maps, soft, or rays");
                if (std::strcmp(argv[i], "maps") == 0) treeShadowMode = 1;
                else if (std::strcmp(argv[i], "soft") == 0) treeShadowMode = 2;
                else if (std::strcmp(argv[i], "rays") == 0) treeShadowMode = 0;
                else throw std::runtime_error("--tree-shadows requires maps, soft, or rays");
            }
            if (std::strcmp(argv[i], "--menu") == 0) showTerrainMenu = true;
            if (std::strcmp(argv[i], "--profile") == 0) profile = true;
            if (std::strcmp(argv[i], "--static-tracks") == 0) animateTracks = false;
            if (std::strcmp(argv[i], "--weapon-preview") == 0) weaponPreview = true;
            if (std::strcmp(argv[i], "--seed") == 0) {
                if (++i >= argc) throw std::runtime_error("--seed requires an unsigned integer");
                uint32_t value;
                const char* end = argv[i] + std::strlen(argv[i]);
                auto parsed = std::from_chars(argv[i], end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != end)
                    throw std::runtime_error("invalid --seed");
                seed = value;
            } else if (std::strcmp(argv[i], "--terrain") == 0) {
                if (++i >= argc || (std::strcmp(argv[i], "legacy") != 0 && std::strcmp(argv[i], "drained-valley") != 0))
                    throw std::runtime_error("--terrain requires legacy or drained-valley");
                valleyTerrain = std::strcmp(argv[i], "drained-valley") == 0;
            } else if (std::strcmp(argv[i], "--landform") == 0) {
                if (++i >= argc) throw std::runtime_error("--landform requires a name");
                landform = MacroTerrain::parseLandform(argv[i]);
            } else if (std::strcmp(argv[i], "--terrain-refinement") == 0) {
                if (++i >= argc) throw std::runtime_error("--terrain-refinement requires off, on, 2x or 4x");
                refinementPasses = TerrainRefinement::parsePasses(argv[i]);
            } else if (std::strcmp(argv[i], "--terrain-resolution") == 0) {
                if (++i >= argc) throw std::runtime_error("--terrain-resolution requires 257 or 513");
                int value;
                const char* end = argv[i] + std::strlen(argv[i]);
                auto parsed = std::from_chars(argv[i], end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != end || (value != 257 && value != 513))
                    throw std::runtime_error("--terrain-resolution requires 257 or 513");
                terrainResolution = value;
            } else if (std::strcmp(argv[i], "--terrain-attempts") == 0) {
                if (++i >= argc) throw std::runtime_error("--terrain-attempts requires an integer from 1 to 8");
                const char* end = argv[i] + std::strlen(argv[i]);
                auto parsed = std::from_chars(argv[i], end, terrainAttempts);
                if (parsed.ec != std::errc{} || parsed.ptr != end || terrainAttempts < 1 || terrainAttempts > 8)
                    throw std::runtime_error("--terrain-attempts requires an integer from 1 to 8");
            } else if (std::strcmp(argv[i], "--model") == 0) {
                if (++i >= argc) throw std::runtime_error("--model requires original or refined");
                if (std::strcmp(argv[i], "original") != 0 && std::strcmp(argv[i], "refined") != 0)
                    throw std::runtime_error("--model requires original or refined");
                originalTankModel = std::strcmp(argv[i], "original") == 0;
            } else if (std::strcmp(argv[i], "--view") == 0) {
                if (++i >= argc) throw std::runtime_error("--view requires tank, tank-side, tank-front, tank-rear, tank-top, landscape, terrain, trees, or water");
                view = argv[i];
                if (view != "tank" && view != "tank-side" && view != "tank-front" &&
                    view != "tank-rear" && view != "tank-top" &&
                    view != "landscape" && view != "terrain" && view != "trees" && view != "water")
                    throw std::runtime_error("unknown reference view");
            }
        }
        if (!valleyTerrain && terrainAttempts != 1)
            throw std::runtime_error("--terrain-attempts above 1 requires --terrain drained-valley");
        if (!valleyTerrain && landform)
            throw std::runtime_error("--landform requires --terrain drained-valley");
        if (!valleyTerrain && terrainResolution)
            throw std::runtime_error("--terrain-resolution requires --terrain drained-valley");
        if (refinementPasses && (!valleyTerrain || terrainResolution.value_or(257) != 257))
            throw std::runtime_error("terrain refinement requires --terrain drained-valley with the 257 erosion grid");
        if (treeLodBenchmark && (weaponPreview || showTerrainMenu || parseScreenshotRequest(argc,argv)))
            throw std::runtime_error("--tree-lod-benchmark cannot be combined with menu, weapon preview or screenshot capture");
        if (treeLodBenchmark && view.empty()) view = "trees";
        if (!view.empty() && !seed) seed = 7331;
        Application app(parseScreenshotRequest(argc, argv), profile, seed, view, originalTankModel, animateTracks, weaponPreview, valleyTerrain, terrainAttempts,
                        landform.value_or(MacroTerrain::Landform::Mixed), terrainResolution.value_or(257), refinementPasses, showTerrainMenu);
        app.setTreeShadowMode(treeShadowMode);
        app.setShadowPreview(shadowPreview,freezeWind);
        app.setTreeLodMode(treeLodMode);
        if (treeLodBenchmark) app.beginTreeLodBenchmark();
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
