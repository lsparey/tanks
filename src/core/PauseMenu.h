#pragma once

#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "../render/HudGeometry.h"

// In-game pause menu (Escape), drawn through the same HudGeometry/HudCanvas
// path as the combat HUD so it can be laid out and previewed headlessly
// (tools/HudPreview.cpp) and structurally tested. Like CombatHud this is a
// dumb renderer of a State struct: Application owns the state, feeds input
// through layout()'s hit rectangles and applies the results (toggling
// renderer options, quitting) itself.
namespace PauseMenu {
enum class Page { Root, Controls, Graphics, Help };

// One row of the Graphics page; the order here is the order drawn.
enum class GraphicsRow { Shadows, TreeShadows, AmbientOcclusion, TreeDetail, Reflections, AntiAliasing, Sound, Count };
struct Graphics {
    bool shadows = true;
    int treeShadowMode = 2;  // 0 rays, 1 stable maps, 2 soft maps (see Application::setTreeShadowMode)
    bool ambientOcclusion = true;
    bool fullTreeDetail = false;
    bool reflections = true;
    bool antiAliasing = true;
    bool sound = false;
};

// Item ids reported by layout(). Graphics rows are ItemGraphicsRow +
// int(GraphicsRow).
enum Item : int { ItemControls, ItemGraphics, ItemHelp, ItemQuit, ItemBack, ItemGraphicsRow };

struct State {
    Page page = Page::Root;
    int focus = 0;     // index into layout()'s buttons (keyboard)
    int hovered = -1;  // likewise, for the mouse; -1 when over nothing
    bool matchActive = false;  // hides free-camera controls, same as the H overlay
    Graphics graphics;
};

struct Rect {
    float x, y, w, h;
    bool contains(glm::vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};
struct Button { Rect rect; int item; };

// Key bindings shown on the Controls page and the in-HUD H overlay -- one
// table so the two can't drift apart. freeCameraOnly lines are dropped
// under --match, where the free camera is unreachable.
struct ControlLine { const char* key; const char* action; bool freeCameraOnly; };
std::span<const ControlLine> controlLines();

// The current page's buttons in logical pixels (HudStyle::canvasScale) in
// keyboard-focus order; empty on pages with nothing to activate.
std::vector<Button> layout(glm::vec2 viewportPixels, const State& state);
void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& state);
}
