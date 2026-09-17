#include "PauseMenu.h"

#include <algorithm>
#include <array>
#include <string>

#include "HudCanvas.h"
#include "../scene/CrateTextureGenerator.h"
#include "../scene/PowerUp.h"

namespace PauseMenu {
using namespace HudStyle;
namespace {

// The HUD font is uppercase letters, digits and "-.:/" only -- anything
// else renders as a blank advance, so all copy here avoids commas,
// apostrophes, brackets and plus signs.
constexpr ControlLine kControls[] = {
    {"W/S", "DRIVE / REVERSE", false},
    {"A/D", "STEER", false},
    {"Q/E", "TRAVERSE TURRET", false},
    {"R/F", "GUN UP / DOWN", false},
    {"T/G", "SHOT POWER UP / DOWN", false},
    {"LMB/SPACE", "FIRE", false},
    {"V", "ARM HELD POWER-UP", false},
    {"C", "CYCLE CAMERA", false},
    {"ARROWS", "MOVE FREE CAMERA", true},
    {"SPACE/CTRL", "FREE CAMERA UP / DOWN", true},
    {"MOUSE", "LOOK IN FREE CAMERA", true},
    {"H", "CONTROLS OVERLAY", false},
    {"F3", "DIAGNOSTICS", false},
    {"F12", "SCREENSHOT", false},
    {"N", "RESTART MATCH WHEN OVER", false},
    {"ESC", "THIS MENU", false},
};

struct RootOption { const char* title; const char* description; };
constexpr RootOption kRootOptions[] = {
    {"CONTROLS", "KEY BINDINGS"},
    {"GRAPHICS", "SHADOWS - DETAIL - REFLECTIONS - ANTI-ALIASING - SOUND"},
    {"HELP", "HOW TO PLAY AND WHAT THE CRATES HOLD"},
    {"QUIT", "EXIT TO DESKTOP"},
};

const char* graphicsLabel(GraphicsRow row) {
    switch (row) {
        case GraphicsRow::Shadows: return "SHADOWS";
        case GraphicsRow::TreeShadows: return "TREE SHADOWS";
        case GraphicsRow::AmbientOcclusion: return "AMBIENT OCCLUSION";
        case GraphicsRow::TreeDetail: return "TREE DETAIL";
        case GraphicsRow::Reflections: return "REFLECTIONS";
        case GraphicsRow::AntiAliasing: return "ANTI-ALIASING";
        case GraphicsRow::Sound: return "SOUND";
        case GraphicsRow::Count: break;
    }
    return "";
}
const char* graphicsKey(GraphicsRow row) {
    switch (row) {
        case GraphicsRow::Shadows: return "F5";
        case GraphicsRow::TreeShadows: return "F6";
        case GraphicsRow::AmbientOcclusion: return "F7";
        case GraphicsRow::TreeDetail: return "F8";
        case GraphicsRow::Reflections: return "F9";
        case GraphicsRow::AntiAliasing: return "F11";
        case GraphicsRow::Sound: return "";
        case GraphicsRow::Count: break;
    }
    return "";
}
std::string graphicsValue(GraphicsRow row, const Graphics& g) {
    auto onOff = [](bool v) { return std::string(v ? "ON" : "OFF"); };
    switch (row) {
        case GraphicsRow::Shadows: return onOff(g.shadows);
        case GraphicsRow::TreeShadows:
            return g.treeShadowMode == 0 ? "RAY TRACED" : g.treeShadowMode == 1 ? "STABLE MAPS" : "SOFT MAPS";
        case GraphicsRow::AmbientOcclusion: return onOff(g.ambientOcclusion);
        case GraphicsRow::TreeDetail: return g.fullTreeDetail ? "FULL" : "REDUCED";
        case GraphicsRow::Reflections: return onOff(g.reflections);
        case GraphicsRow::AntiAliasing: return g.antiAliasing ? "TAA ON" : "OFF";
        case GraphicsRow::Sound: return onOff(g.sound);
        case GraphicsRow::Count: break;
    }
    return "";
}

struct CrateHelp { PowerUpType type; const char* name; const char* effect; };
constexpr CrateHelp kCrates[] = {
    {PowerUpType::ExtraShell, "EXTRA SHELL", "ONE MORE SHOT THIS TURN. APPLIES ON PICKUP."},
    {PowerUpType::IncreasedDamage, "INCREASED DAMAGE", "NEXT SHOT DEALS 1.5X DAMAGE. ARM WITH V."},
    {PowerUpType::LargerSplash, "LARGER SPLASH", "NEXT SHOT HAS A 1.5X BLAST RADIUS. ARM WITH V."},
    {PowerUpType::AimAssist, "AIM ASSIST", "SHOWS THE PREDICTED SHOT PATH WHILE ARMED. ARM WITH V."},
    {PowerUpType::TighterAccuracy, "TIGHTER ACCURACY", "PERMANENTLY REDUCES SHOT SCATTER."},
    {PowerUpType::MoreFuel, "MORE FUEL", "10 FUEL NOW AND 10 MORE TANK CAPACITY. PERMANENT."},
};
constexpr const char* kHowToPlay[] = {
    "TURN-BASED DUEL AGAINST ONE OPPONENT TANK.",
    "ON YOUR TURN: DRIVE - EVERY METRE AND PIVOT BURNS FUEL - THEN AIM THE",
    "GUN. RANGE COMES FROM SHOT POWER. FIRE WHENEVER YOU ARE READY.",
    "YOUR TURN ENDS WHEN YOU HAVE NO SHELLS LEFT. FUEL AND SHELLS REFILL.",
    "THREE FULL HITS DESTROY A TANK. HITS ON THE FRONT ARMOUR DO HALF DAMAGE.",
    "DRIVE OVER A CRATE TO COLLECT THE POWER-UP ITS STENCIL SHOWS:",
};

// Panel geometry shared by layout() and draw() so hit rectangles and pixels
// can never disagree.
constexpr float kPanelW = 620, kPad = 24, kTitleH = 58, kFooterH = 34;
constexpr float kButtonH = 48, kButtonGap = 10, kRowH = 30, kRowGap = 4, kLineH = 16;
// Help is the tallest page; these keep its panel clear of the bottom vitals
// row on the minimum 700-high canvas.
constexpr float kIconBox = 30, kCrateRowH = 36;

struct Frame {
    Rect panel;
    float contentY;  // first content row's top
    std::vector<Button> buttons;
};

size_t visibleControlCount(bool matchActive) {
    size_t n = 0;
    for (const auto& line : kControls) if (!(matchActive && line.freeCameraOnly)) ++n;
    return n;
}

Frame frame(glm::vec2 canvas, const State& s) {
    Frame f;
    float contentH = 0;
    switch (s.page) {
        case Page::Root: contentH = std::size(kRootOptions) * (kButtonH + kButtonGap) - kButtonGap; break;
        case Page::Controls: contentH = visibleControlCount(s.matchActive) * kLineH + kButtonGap + kButtonH; break;
        case Page::Graphics: contentH = size_t(GraphicsRow::Count) * (kRowH + kRowGap) - kRowGap + kButtonGap + kButtonH; break;
        case Page::Help: contentH = std::size(kHowToPlay) * kLineH + 10 + std::size(kCrates) * kCrateRowH + kButtonGap + kButtonH; break;
    }
    float panelH = kTitleH + contentH + kPad + kFooterH;
    f.panel = {canvas.x / 2 - kPanelW / 2, canvas.y / 2 - panelH / 2, kPanelW, panelH};
    f.contentY = f.panel.y + kTitleH;
    float bx = f.panel.x + kPad, bw = kPanelW - kPad * 2;
    auto back = [&](float y) { f.buttons.push_back({{bx, y, bw, kButtonH}, ItemBack}); };
    switch (s.page) {
        case Page::Root:
            for (size_t i = 0; i < std::size(kRootOptions); ++i)
                f.buttons.push_back({{bx, f.contentY + i * (kButtonH + kButtonGap), bw, kButtonH}, ItemControls + int(i)});
            break;
        case Page::Controls:
            back(f.contentY + visibleControlCount(s.matchActive) * kLineH + kButtonGap);
            break;
        case Page::Graphics:
            for (int i = 0; i < int(GraphicsRow::Count); ++i)
                f.buttons.push_back({{bx, f.contentY + i * (kRowH + kRowGap), bw, kRowH}, ItemGraphicsRow + i});
            back(f.contentY + int(GraphicsRow::Count) * (kRowH + kRowGap) - kRowGap + kButtonGap);
            break;
        case Page::Help:
            back(f.contentY + std::size(kHowToPlay) * kLineH + 10 + std::size(kCrates) * kCrateRowH + kButtonGap);
            break;
    }
    return f;
}

const char* pageTitle(Page page) {
    switch (page) {
        case Page::Root: return "PAUSED";
        case Page::Controls: return "CONTROLS";
        case Page::Graphics: return "GRAPHICS";
        case Page::Help: return "HELP";
    }
    return "";
}

// The crate stencil, rasterised from the same signed-distance glyph the
// crate texture is stamped with -- a grid of tiny quads with coverage-based
// alpha, since the HUD pipeline draws flat colour only, no textures.
void crateIcon(HudCanvas& c, float x, float y, float box, PowerUpType type) {
    constexpr int kCells = 18;
    const float cell = box / kCells, radius = box * .42f;
    glm::vec3 paint = CrateTextureGenerator::iconPaint(type);
    c.rect(x, y, box, box, glm::vec3(.42f, .31f, .18f));  // wood-toned backing
    for (int j = 0; j < kCells; ++j) {
        for (int i = 0; i < kCells; ++i) {
            glm::vec2 centre(x + (i + .5f) * cell, y + (j + .5f) * cell);
            glm::vec2 p = (centre - glm::vec2(x + box / 2, y + box / 2)) / radius;
            float distancePx = CrateTextureGenerator::iconDistance(type, p) * radius;
            float coverage = glm::clamp(.5f - distancePx / cell, 0.f, 1.f);
            if (coverage > .02f) c.rect(x + i * cell, y + j * cell, cell, cell, paint, coverage);
        }
    }
}

void button(HudCanvas& c, const Rect& r, bool focused, bool hovered) {
    c.rect(r.x, r.y, r.w, r.h, focused ? accent : glm::vec3(.15f, .17f, .19f));
    c.rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, hovered ? glm::vec3(.10f, .12f, .13f) : glm::vec3(.05f, .06f, .07f));
}

}  // namespace

std::span<const ControlLine> controlLines() { return kControls; }

std::vector<Button> layout(glm::vec2 viewportPixels, const State& s) {
    if (viewportPixels.x <= 0 || viewportPixels.y <= 0) return {};
    return frame(viewportPixels / canvasScale(viewportPixels), s).buttons;
}

void draw(HudGeometry& hud, glm::vec2 viewportPixels, const State& s) {
    if (viewportPixels.x <= 0 || viewportPixels.y <= 0) return;
    HudCanvas c{hud, viewportPixels / canvasScale(viewportPixels)};
    Frame f = frame(c.size, s);
    c.rect(0, 0, c.size.x, c.size.y, scrim, .55f);
    c.rect(f.panel.x, f.panel.y, f.panel.w, f.panel.h, glm::vec3(.05f, .06f, .07f), .94f);
    c.rect(f.panel.x, f.panel.y, f.panel.w, 2, accent, .8f);  // same dash-trim edge as the vitals panels
    c.line({f.panel.x, f.panel.y + 12}, {f.panel.x + 12, f.panel.y}, 1.4f, accent);
    c.text(pageTitle(s.page), f.panel.x + kPad, f.panel.y + 18, 2.2f, white);
    for (size_t i = 0; i < f.buttons.size(); ++i)
        button(c, f.buttons[i].rect, int(i) == s.focus, int(i) == s.hovered);

    float x = f.panel.x + kPad, y = f.contentY;
    switch (s.page) {
        case Page::Root:
            for (size_t i = 0; i < std::size(kRootOptions); ++i) {
                const Rect& r = f.buttons[i].rect;
                bool active = int(i) == s.focus;
                if (active) c.rect(r.x + 1, r.y + 1, 4, r.h - 2, accent);
                c.text(kRootOptions[i].title, r.x + 18, r.y + 9, 1.5f, active ? accent : white);
                c.text(kRootOptions[i].description, r.x + 18, r.y + 27, 1.1f, grey);
            }
            break;
        case Page::Controls:
            for (const auto& line : kControls) {
                if (s.matchActive && line.freeCameraOnly) continue;
                c.text(line.key, x, y + 2, 1.2f, accent);
                c.text(line.action, x + 96, y + 2, 1.2f, white);
                y += kLineH;
            }
            break;
        case Page::Graphics:
            for (int i = 0; i < int(GraphicsRow::Count); ++i) {
                auto row = GraphicsRow(i);
                const Rect& r = f.buttons[i].rect;
                c.text(graphicsLabel(row), r.x + 14, r.y + 9, 1.25f, white);
                std::string value = graphicsValue(row, s.graphics);
                bool on = value != "OFF" && value != "REDUCED";
                c.text(value, r.x + r.w - 14 - textWidth(value.size(), 1.25f), r.y + 9, 1.25f, on ? accent : grey);
                c.text(graphicsKey(row), r.x + r.w - 150, r.y + 10, 1.0f, grey);
            }
            break;
        case Page::Help:
            for (const char* line : kHowToPlay) { c.text(line, x, y + 2, 1.15f, white); y += kLineH; }
            y += 10;
            for (const auto& crate : kCrates) {
                crateIcon(c, x, y + (kCrateRowH - kIconBox) / 2, kIconBox, crate.type);
                c.text(crate.name, x + kIconBox + 14, y + 6, 1.25f, CrateTextureGenerator::iconPaint(crate.type));
                c.text(crate.effect, x + kIconBox + 14, y + 23, 1.05f, grey);
                y += kCrateRowH;
            }
            break;
    }
    const Rect& back = f.buttons.back().rect;
    if (s.page != Page::Root) c.centered("BACK", back.x + back.w / 2, back.y + 17, 1.4f, white);
    const char* hint = s.page == Page::Root ? "ESC: RESUME   UP/DOWN: FOCUS   ENTER: SELECT   CLICK: SELECT"
                                            : "ESC: BACK   UP/DOWN: FOCUS   ENTER: SELECT   CLICK: SELECT";
    c.text(hint, f.panel.x + kPad, f.panel.y + f.panel.h - kFooterH + 10, 1.05f, grey);
}
}
