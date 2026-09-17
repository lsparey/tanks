#include "Application.h"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <random>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

bool Application::selectTerrain(bool& valleyTerrain, MacroTerrain::Landform& landform,
                                int& terrainResolution, int& refinementPasses, bool& matchEnabled) {
    constexpr float width = 1024, height = 720;
    struct Mode { const char* title; const char* description; int erosion, passes; };
    constexpr Mode modes[] = {
        {"ORIGINAL TERRAIN - FAST", "CLASSIC HEIGHTMAP. NO EROSION SIMULATION.", 257, 0},
        {"ADVANCED TERRAIN", "ERODED LANDFORMS, RIVERS, LAKES AND CLASSIFIED GROUND.", 257, 1},
    };
    constexpr MacroTerrain::Landform forms[] = {
        MacroTerrain::Landform::Mixed, MacroTerrain::Landform::Hills,
        MacroTerrain::Landform::Ridges, MacroTerrain::Landform::Plain,
        MacroTerrain::Landform::Basin, MacroTerrain::Landform::Valley
    };
    constexpr const char* formNames[] = {"MIXED", "HILLS", "RIDGES", "PLAIN", "BASIN", "VALLEY"};
    // 1v1 Match needs a second spawn (see hasOpponent_'s own comment), which
    // only the advanced/valley generator's navigation result produces --
    // legacy terrain would silently make the match mode a no-op, so it's
    // only selectable alongside advanced terrain (same restriction LANDFORM
    // already has, one control below).
    struct MatchModeOption { const char* title; const char* description; };
    constexpr MatchModeOption matchModes[] = {
        {"FREEROAM", "DRIVE AND EXPLORE. NO OPPONENT AI OR TURN RULES."},
        {"1V1 MATCH", "TURN-BASED DUEL. FUEL, AIMING, DAMAGE, POWER-UPS."},
    };
    struct Rect {
        float x, y, w, h;
        bool contains(float px, float py) const { return px >= x && px < x+w && py >= y && py < y+h; }
    };
    constexpr int kControlCount = 11;
    std::array<Rect, kControlCount> controls{};
    for (int i = 0; i < 2; ++i) controls[i] = {232, 118.f + i * 56, 560, 48};
    controls[2] = {232, 428, 272, 40};
    controls[3] = {512, 428, 168, 40};
    controls[4] = {688, 428, 104, 40};
    controls[5] = {232, 506, 272, 44};
    controls[6] = {688, 506, 104, 44};
    controls[7] = {512, 506, 168, 44};  // SHADOWS toggle, between start and quit
    controls[8] = {512, 562, 168, 44};  // SOUND toggle, directly below shadows
    // MATCH MODE buttons: same shape as the terrain-mode buttons above,
    // dropped into the empty gap between them (ending y=222) and the
    // LANDFORM row (starting y=410).
    for (int i = 0; i < 2; ++i) controls[9 + i] = {232, 250.f + i * 56, 560, 48};
    // 1v1 Match is the default: the menu opens on it regardless of the CLI
    // flag (which only automation runs set anyway). Freeroam is one click
    // away for rendering/terrain work.
    int selected = 1, focus = 5, form = 0, matchModeSelected = 1;
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
            focus = (focus + (backwards ? kControlCount - 1 : 1)) % kControlCount;
            if (focus == 3) selectSeed = true;
        }
        if (focus == 2 && selected != 0 && (key(GLFW_KEY_LEFT) || key(GLFW_KEY_RIGHT)))
            form = (form + (key(GLFW_KEY_LEFT) ? 5 : 1)) % 6;
        int windowWidth, windowHeight;
        glfwGetWindowSize(window_, &windowWidth, &windowHeight);
        float scale = std::min(windowWidth / width, windowHeight / height);
        double mx, my;
        glfwGetCursorPos(window_, &mx, &my);
        float x = (float(mx) - (windowWidth - width * scale) * .5f) / scale;
        float y = (float(my) - (windowHeight - height * scale) * .5f) / scale;
        int hovered = -1;
        for (int i = 0; i < kControlCount; ++i) if (controls[i].contains(x, y)) hovered = i;
        bool mouseDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int activated = -1;
        if (mouseDown && !mouseWasDown && hovered >= 0) {
            focus = activated = hovered;
            if (focus == 3) selectSeed = true;
        }
        if (key(GLFW_KEY_ENTER) || key(GLFW_KEY_KP_ENTER)) activated = focus;
        if (activated >= 0 && activated < 2) selected = activated;
        if (activated == 2 && selected != 0) form = (form + 1) % 6;
        if (activated == 4) { seed = std::to_string(std::random_device{}()); selectSeed = false; }
        if (activated == 7) shadowsEnabled_ = !shadowsEnabled_;
        if (activated == 9) matchModeSelected = 0;
        if (activated == 10 && selected != 0) matchModeSelected = 1;
        if (activated == 8) {
            // Enabling can fail (no audio device), so read the actual state
            // back rather than assuming the flip took effect. The shot is an
            // audible confirmation that output really works.
            audio_->setEnabled(!soundEnabled_);
            soundEnabled_ = audio_->enabled();
            if (soundEnabled_) audio_->playShot();
        }
        if (focus == 3) {
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
        if (activated == 6) { glfwSetWindowShouldClose(window_, GLFW_TRUE); break; }
        if (activated == 5 && validSeed) {
            valleyTerrain = selected != 0;
            terrainResolution = modes[selected].erosion;
            refinementPasses = modes[selected].passes;
            landform = forms[form];
            worldSeed_ = parsedSeed;
            // Safety net rather than trusting intermediate UI state: if the
            // player picked 1v1 Match and then switched to legacy terrain
            // (or vice versa), the final choice always wins, not whatever
            // matchModeSelected happened to be left at.
            matchEnabled = valleyTerrain && matchModeSelected == 1;
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
            const glm::vec3 white(.95f,.96f,.97f), muted(.62f,.66f,.70f);
            const glm::vec3 accent(.38f,.79f,.76f), warn(.96f,.56f,.36f);
            text("SELECT TERRAIN", 232, 52, 2.2f, white);
            text("ORIGINAL IS FASTEST. ADVANCED SIMULATES EROSION AND WATER.", 232, 84, 1.2f, muted);
            text("MATCH MODE", 232, 232, 1.5f, white);
            for (int i = 0; i < kControlCount; ++i) {
                bool disabled = (i == 2 && selected == 0) || (i == 5 && !validSeed) || (i == 10 && selected == 0);
                Rect r = controls[i];
                glm::vec3 border = !disabled && focus == i ? accent : glm::vec3(.15f,.17f,.19f);
                quad(r, border);
                quad({r.x+1,r.y+1,r.w-2,r.h-2}, !disabled && hovered == i ? glm::vec3(.10f,.12f,.13f) : glm::vec3(.05f,.06f,.07f));
                if (i < 2) {
                    if (selected == i) quad({r.x+1,r.y+1,4,r.h-2},accent);
                    text(modes[i].title,r.x+18,r.y+9,1.5f,selected == i ? accent : white);
                    text(modes[i].description,r.x+18,r.y+27,1.1f,muted);
                }
                if (i >= 9) {
                    int m = i - 9;
                    if (matchModeSelected == m) quad({r.x+1,r.y+1,4,r.h-2},accent);
                    text(matchModes[m].title,r.x+18,r.y+9,1.5f,disabled ? muted : (matchModeSelected == m ? accent : white));
                    text(m == 1 && disabled ? "REQUIRES ADVANCED TERRAIN" : matchModes[m].description,
                         r.x+18,r.y+27,1.1f,muted);
                }
            }
            text("LANDFORM",232,410,1.1f,muted);
            text(selected == 0 ? "NOT USED" : formNames[form],246,441,1.4f,selected == 0 ? muted : white);
            text("SEED",512,410,1.1f,muted);
            if (focus == 3 && selectSeed) quad({516,434,152,28},{.14f,.28f,.27f});
            text(seed.empty() ? "-" : seed,520,441,1.4f,white);
            text("RANDOM",712,442,1.3f,white);
            text(validSeed ? "TAB / ARROWS: FOCUS   ENTER: ACTIVATE   ESC: QUIT" : "ENTER A SEED FROM 0 TO 4294967295",232,478,1.2f,validSeed ? muted : warn);
            text("START GAME",250,520,1.5f,validSeed ? accent : muted);
            text(shadowsEnabled_ ? "SHADOWS: ON" : "SHADOWS: OFF",526,523,1.2f,shadowsEnabled_ ? accent : muted);
            text(soundEnabled_ ? "SOUND: ON" : "SOUND: OFF",526,579,1.2f,soundEnabled_ ? accent : muted);
            text("QUIT",723,520,1.5f,white);
        });
        glfwWaitEventsTimeout(1.0 / 30.0);
    }
    glfwSetCharCallback(window_, nullptr);
    return false;
}
