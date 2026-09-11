#pragma once

#include <string_view>
#include <vector>
#include <glm/glm.hpp>

// CPU geometry shared by the live renderer, layout tests and preview exporter.
// NDC uses +Y up. Text origins are top-left; sizes are NDC bitmap pixels.
class HudGeometry {
public:
    struct Vertex { glm::vec2 position; glm::vec4 color; };
    static constexpr size_t kMaxVertices = 65536;
    void begin();
    void addQuad(glm::vec2 center, glm::vec2 halfSize, glm::vec3 color, float opacity = 1);
    void addTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3 color, float opacity = 1);
    void addText(std::string_view text, glm::vec2 origin, glm::vec2 pixelSize, glm::vec3 color);
    const std::vector<Vertex>& vertices() const { return pending_; }
protected:
    std::vector<Vertex> pending_;
};
