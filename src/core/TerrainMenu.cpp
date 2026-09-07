#include "Application.h"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <random>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

bool Application::selectTerrain(bool& valleyTerrain, MacroTerrain::Landform& landform,
                                int& terrainResolution, int& refinementPasses) {
    constexpr float width = 1024, height = 720;
    struct Mode { const char* title; const char* description; int erosion, passes; };
    constexpr Mode modes[] = {
        {"ORIGINAL TERRAIN - FAST", "CLASSIC TERRAIN. NO EROSION SIMULATION.", 257, 0},
        {"ERODED LANDSCAPES - 257", "EXPERIMENTAL HILLS, RIDGES, PLAINS, BASINS AND VALLEYS.", 257, 0},
        {"REFINED TERRAIN - 513", "FINER SURFACE AND SMOOTHER BANKS. KEEPS THE 257 EROSION GRID.", 257, 1},
        {"REFINED TERRAIN - 1025", "EXTRA SURFACE DETAIL. HIGHER MEMORY USE AND LOWER FRAME RATE.", 257, 2},
        {"FINE EROSION - 513 / SLOW", "MUCH SLOWER GENERATION. SOME MAPS MAY FAIL TO GENERATE.", 513, 0},
    };
    constexpr MacroTerrain::Landform forms[] = {
        MacroTerrain::Landform::Mixed, MacroTerrain::Landform::Hills,
        MacroTerrain::Landform::Ridges, MacroTerrain::Landform::Plain,
        MacroTerrain::Landform::Basin, MacroTerrain::Landform::Valley
    };
    constexpr const char* formNames[] = {"MIXED", "HILLS", "RIDGES", "PLAIN", "BASIN", "VALLEY"};
    struct Rect {
        float x, y, w, h;
        bool contains(float px, float py) const { return px >= x && px < x+w && py >= y && py < y+h; }
    };
    std::array<Rect, 10> controls{};
    for (int i = 0; i < 5; ++i) controls[i] = {48, 122.f + i * 70, 928, 62};
    controls[5] = {48, 500, 440, 48};
    controls[6] = {520, 500, 278, 48};
    controls[7] = {814, 500, 162, 48};
    controls[8] = {48, 620, 748, 58};
    controls[9] = {814, 620, 162, 58};
    int selected = 0, focus = 8, form = 0;
    std::string seed = std::to_string(worldSeed_);
    bool selectSeed = false, mouseWasDown = false;
    std::array<bool, GLFW_KEY_LAST + 1> keys{};
    auto key = [&](int code) { return glfwGetKey(window_, code) == GLFW_PRESS && !keys[code]; };
    glfwSetCharCallback(window_, [](GLFWwindow* window, unsigned int c) {
        if (c >= '0' && c <= '9')
            static_cast<Application*>(glfwGetWindowUserPointer(window))->menuTextInput_.push_back(char(c));
    });
    std::cout << "Terrain menu ready\n" << std::flush;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window_)) break;
        if (key(GLFW_KEY_ESCAPE)) { glfwSetWindowShouldClose(window_, GLFW_TRUE); break; }
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
        if (!fbWidth || !fbHeight) { glfwWaitEventsTimeout(.05); continue; }
        if (framebufferResized_) {
            recreateSwapchainDependentResources();
            framebufferResized_ = false;
        }
        if (key(GLFW_KEY_TAB) || key(GLFW_KEY_DOWN) || key(GLFW_KEY_UP)) {
            bool backwards = key(GLFW_KEY_UP) || (key(GLFW_KEY_TAB) &&
                (glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
            focus = (focus + (backwards ? 9 : 1)) % 10;
            if (focus == 6) selectSeed = true;
        }
        if (focus == 5 && selected != 0 && (key(GLFW_KEY_LEFT) || key(GLFW_KEY_RIGHT)))
            form = (form + (key(GLFW_KEY_LEFT) ? 5 : 1)) % 6;
        int windowWidth, windowHeight;
        glfwGetWindowSize(window_, &windowWidth, &windowHeight);
        float scale = std::min(windowWidth / width, windowHeight / height);
        double mx, my;
        glfwGetCursorPos(window_, &mx, &my);
        float x = (float(mx) - (windowWidth - width * scale) * .5f) / scale;
        float y = (float(my) - (windowHeight - height * scale) * .5f) / scale;
        int hovered = -1;
        for (int i = 0; i < 10; ++i) if (controls[i].contains(x, y)) hovered = i;
        bool mouseDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int activated = -1;
        if (mouseDown && !mouseWasDown && hovered >= 0) {
            focus = activated = hovered;
            if (focus == 6) selectSeed = true;
        }
        if (key(GLFW_KEY_ENTER) || key(GLFW_KEY_KP_ENTER)) activated = focus;
        if (activated >= 0 && activated < 5) selected = activated;
        if (activated == 5 && selected != 0) form = (form + 1) % 6;
        if (activated == 7) { seed = std::to_string(std::random_device{}()); selectSeed = false; }
        if (focus == 6) {
            if (key(GLFW_KEY_A) && (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                   glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) selectSeed = true;
            if (key(GLFW_KEY_BACKSPACE) || key(GLFW_KEY_DELETE)) {
                if (selectSeed) seed.clear();
                else if (!seed.empty()) seed.pop_back();
                selectSeed = false;
            }
            for (char c : menuTextInput_) {
                if (selectSeed) { seed.clear(); selectSeed = false; }
                if (seed.size() < 10) seed.push_back(c);
            }
        }
        menuTextInput_.clear();
        uint32_t parsedSeed = 0;
        auto parsed = std::from_chars(seed.data(), seed.data() + seed.size(), parsedSeed);
        bool validSeed = parsed.ec == std::errc{} && parsed.ptr == seed.data() + seed.size();
        if (activated == 9) { glfwSetWindowShouldClose(window_, GLFW_TRUE); break; }
        if (activated == 8 && validSeed) {
            valleyTerrain = selected != 0;
            terrainResolution = modes[selected].erosion;
            refinementPasses = modes[selected].passes;
            landform = forms[form];
            worldSeed_ = parsedSeed;
            glfwSetCharCallback(window_, nullptr);
            return true;
        }
        // Snapshot before presenting: presentation pumps events too, so keys
        // arriving there must remain available to process on the next iteration.
        for (int i : {GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_UP, GLFW_KEY_DOWN,
                      GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_ENTER, GLFW_KEY_KP_ENTER,
                      GLFW_KEY_A, GLFW_KEY_BACKSPACE, GLFW_KEY_DELETE})
            keys[i] = glfwGetKey(window_, i) == GLFW_PRESS;
        mouseWasDown = mouseDown;
        presentLoadingProgress(0, [&] {
            auto extent = swapchain_->extent();
            float s = std::min(extent.width / width, extent.height / height);
            glm::vec2 unit(2*s/extent.width, 2*s/extent.height);
            auto point = [&](float px, float py) { return glm::vec2((px-width*.5f)*unit.x, (height*.5f-py)*unit.y); };
            auto quad = [&](Rect r, glm::vec3 color) { hud_->addQuad(point(r.x+r.w*.5f, r.y+r.h*.5f), {r.w*.5f*unit.x, r.h*.5f*unit.y}, color); };
            auto text = [&](std::string_view label, float px, float py, float size, glm::vec3 color) {
                hud_->addText(label, point(px, py), unit * size, color);
            };
            const glm::vec3 white(.91f,.94f,.91f), muted(.55f,.62f,.59f), green(.42f,.84f,.53f);
            text("CHOOSE YOUR TERRAIN", 48, 40, 3.4f, white);
            text("ORIGINAL IS FASTEST. EXPERIMENTAL MODES MAY TAKE LONGER.", 48, 84, 1.6f, muted);
            for (int i = 0; i < 10; ++i) {
                bool disabled = (i == 5 && selected == 0) || (i == 8 && !validSeed);
                Rect r = controls[i];
                glm::vec3 border = !disabled && focus == i ? green : glm::vec3(.20f,.27f,.24f);
                quad(r, border);
                quad({r.x+2,r.y+2,r.w-4,r.h-4}, !disabled && hovered == i ? glm::vec3(.16f,.23f,.19f) : glm::vec3(.09f,.13f,.11f));
                if (i < 5) {
                    if (selected == i) quad({r.x+2,r.y+2,5,r.h-4},green);
                    text(modes[i].title,r.x+22,r.y+11,2,selected == i ? green : white);
                    text(modes[i].description,r.x+22,r.y+36,1.45f,muted);
                }
            }
            text("LANDFORM",48,480,1.4f,muted);
            text(selected == 0 ? "NOT USED FOR ORIGINAL TERRAIN" : std::string("LANDFORM: ")+formNames[form],64,518,1.65f,selected == 0 ? muted : white);
            text("SEED",520,480,1.4f,muted);
            if (focus == 6 && selectSeed) quad({532,511,250,26},{.18f,.32f,.24f});
            text(seed.empty() ? "-" : seed,536,516,2,white);
            text("RANDOM",839,518,2,white);
            text(validSeed ? "TAB / ARROWS: FOCUS   ENTER: ACTIVATE   ESC: QUIT" : "ENTER A SEED FROM 0 TO 4294967295",48,570,1.65f,validSeed ? muted : glm::vec3(1,.65f,.35f));
            text("START GAME",320,640,2.3f,validSeed ? green : muted);
            text("QUIT",860,640,2.3f,white);
        });
        glfwWaitEventsTimeout(1.0 / 30.0);
    }
    glfwSetCharCallback(window_, nullptr);
    return false;
}
