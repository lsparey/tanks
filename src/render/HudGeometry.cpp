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
    // Five columns, seven rows. Horizontal runs keep the menu geometry small.
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/";
    constexpr uint8_t rows[][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},
        {0,12,12,0,12,12,0},{1,2,2,4,8,8,16}
    };
    static_assert(std::size(rows) == alphabet.size());
    for (char c : text) {
        auto index = alphabet.find(c);
        if (index != std::string_view::npos) for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 5;) {
                if (!(rows[index][y] & (16 >> x))) { ++x; continue; }
                int start = x++;
                while (x < 5 && (rows[index][y] & (16 >> x))) ++x;
                addQuad(origin + glm::vec2((start + (x - start) * .5f) * pixelSize.x,
                                          -(y + .5f) * pixelSize.y),
                        glm::vec2((x - start) * .5f * pixelSize.x, .5f * pixelSize.y), color);
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
