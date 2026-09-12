#include "HudGeometry.h"

#include <array>
#include <stdexcept>

void HudGeometry::begin() { pending_.clear(); }

void HudGeometry::addQuad(glm::vec2 centerNDC, glm::vec2 halfSizeNDC, glm::vec3 color, float opacity) {
    const glm::vec4 rgba(color, opacity);
    if (pending_.size() + 6 > kMaxVertices) throw std::runtime_error("HUD geometry capacity exceeded");
    glm::vec2 tl = centerNDC + glm::vec2(-halfSizeNDC.x, -halfSizeNDC.y);
    glm::vec2 tr = centerNDC + glm::vec2(halfSizeNDC.x, -halfSizeNDC.y);
    glm::vec2 br = centerNDC + glm::vec2(halfSizeNDC.x, halfSizeNDC.y);
    glm::vec2 bl = centerNDC + glm::vec2(-halfSizeNDC.x, halfSizeNDC.y);

    pending_.push_back({tl, rgba});
    pending_.push_back({tr, rgba});
    pending_.push_back({br, rgba});
    pending_.push_back({tl, rgba});
    pending_.push_back({br, rgba});
    pending_.push_back({bl, rgba});
}

void HudGeometry::addText(std::string_view text, glm::vec2 origin, glm::vec2 pixelSize, glm::vec3 color) {
    // Antialiased stroke font on the same 5x7 cell and 6-pixel advance as the
    // old bitmap, so every caller's width arithmetic is unchanged. Each glyph
    // is a set of polylines on a half-pixel grid: character pairs are '0'+x
    // (0-8) and '0'+y (0-12, top down) waypoints and '|' lifts the pen.
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/";
    constexpr const char* glyphs[] = {
        "0< 40 8<|28 68",                          // A
        "00 0<|00 60 82 84 66 06|66 88 8: 6< 0<",  // B
        "82 60 20 02 0: 2< 6< 8:",                 // C
        "00 0<|00 50 83 89 5< 0<",                 // D
        "80 00 0< 8<|06 66",                       // E
        "80 00 0<|06 66",                          // F
        "82 60 20 02 0: 2< 6< 8: 86 46",           // G
        "00 0<|80 8<|06 86",                       // H
        "20 60|40 4<|2< 6<",                       // I
        "60 6: 4< 2< 0:",                          // J
        "00 0<|80 06 8<",                          // K
        "00 0< 8<",                                // L
        "0< 00 46 80 8<",                          // M
        "0< 00 8< 80",                             // N
        "20 60 82 8: 6< 2< 0: 02 20",              // O
        "0< 00 60 82 84 66 06",                    // P
        "20 60 82 8: 6< 2< 0: 02 20|48 8<",        // Q
        "0< 00 60 82 84 66 06|46 8<",              // R
        "82 60 20 02 04 26 66 88 8: 6< 2< 0:",     // S
        "00 80|40 4<",                             // T
        "00 0: 2< 6< 8: 80",                       // U
        "00 4< 80",                                // V
        "00 2< 46 6< 80",                          // W
        "00 8<|80 0<",                             // X
        "00 46 80|46 4<",                          // Y
        "00 80 0< 8<",                             // Z
        "20 60 82 8: 6< 2< 0: 02 20|28 64",        // 0
        "22 40 4<|2< 6<",                          // 1
        "02 20 60 82 84 0< 8<",                    // 2
        "02 20 60 82 84 66 26|66 88 8: 6< 2< 0:",  // 3
        "6< 60 08 88",                             // 4
        "80 00 06 66 88 8: 6< 2< 0:",              // 5
        "82 60 20 02 0: 2< 6< 8: 88 66 06",        // 6
        "00 80 2<",                                // 7
        "20 60 82 84 66 26 04 02 20|26 66 88 8: 6< 2< 0: 08 26", // 8
        "0: 2< 6< 8: 82 60 20 02 04 26 86",        // 9
        "26 66",                                   // -
        "4: 4<",                                   // .
        "42 44|48 4:",                             // :
        "0< 80",                                   // /
    };
    static_assert(std::size(glyphs) == alphabet.size());
    // Core stroke plus a soft falloff on both sides. Overlaps at joints are
    // invisible because every quad of a glyph blends the same color.
    constexpr float kHalf = .32f, kFeather = .42f;
    for (char ch : text) {
        auto index = alphabet.find(ch);
        if (index != std::string_view::npos) {
            // Glyph space is square in screen pixels, so widths and normals
            // are computed there and only the corners map through pixelSize.
            auto place = [&](glm::vec2 g) {
                return origin + glm::vec2((g.x + .5f) * pixelSize.x, -(g.y + .5f) * pixelSize.y);
            };
            auto emit = [&](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float a01, float a23) {
                if (pending_.size() + 6 > kMaxVertices)
                    throw std::runtime_error("HUD geometry capacity exceeded");
                const glm::vec4 c01(color, a01), c23(color, a23);
                pending_.insert(pending_.end(), {{place(p0), c01}, {place(p1), c01}, {place(p2), c23},
                                                 {place(p0), c01}, {place(p2), c23}, {place(p3), c23}});
            };
            glm::vec2 previous{};
            bool pen = false;
            for (const char* p = glyphs[index]; *p; ++p) {
                if (*p == '|') { pen = false; continue; }
                if (*p == ' ') continue;
                glm::vec2 point = glm::vec2(p[0] - '0', p[1] - '0') * .5f;
                ++p;
                if (pen && glm::length(point - previous) > .01f) {
                    glm::vec2 d = glm::normalize(point - previous);
                    glm::vec2 n(-d.y, d.x);
                    // Square caps extended by the half width fill the notch
                    // where consecutive segments meet at an angle.
                    glm::vec2 a = previous - d * kHalf, b = point + d * kHalf;
                    glm::vec2 N = n * kHalf, F = n * (kHalf + kFeather), D = d * kFeather;
                    emit(a + N, b + N, b - N, a - N, 1, 1);
                    emit(a + N, b + N, b + F, a + F, 1, 0);
                    emit(a - N, b - N, b - F, a - F, 1, 0);
                    emit(a + N, a - N, a - N - D, a + N - D, 1, 0);
                    emit(b + N, b - N, b - N + D, b + N + D, 1, 0);
                }
                previous = point;
                pen = true;
            }
        }
        origin.x += 6 * pixelSize.x;
    }
}

void HudGeometry::addTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3 color, float opacity) {
    if (pending_.size() + 3 > kMaxVertices) throw std::runtime_error("HUD geometry capacity exceeded");
    const glm::vec4 rgba(color, opacity);
    pending_.insert(pending_.end(), {{a,rgba},{b,rgba},{c,rgba}});
}
