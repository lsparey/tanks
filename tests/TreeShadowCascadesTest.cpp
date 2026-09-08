#include "render/TreeShadowCascades.h"
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-6f) {
    for (int c=0;c<4;++c) for (int r=0;r<4;++r)
        if (std::abs(a[c][r]-b[c][r]) > tolerance) return false;
    return true;
}
}

int main() {
    using namespace TreeShadowCascades;
    glm::vec3 light = glm::normalize(glm::vec3(-.45f,-.55f,-.8f));
    auto origin = build(glm::vec3(0),light);
    glm::mat4 lightView = glm::lookAt(-light*(kDepthRange*.5f),glm::vec3(0),glm::vec3(0,1,0));
    glm::vec3 right = glm::vec3(glm::inverse(lightView)[0]);
    for (uint32_t i=0;i<kCount;++i) {
        float texel = 2.f*kHalfWidths[i]/kResolution;
        auto subtexel = build(right*(texel*.2f),light);
        require(close(origin[i],subtexel[i]), "Subtexel camera motion changed shadow grid");
        auto shifted = build(right*texel,light);
        glm::vec4 p0 = origin[i]*glm::vec4(0,0,0,1);
        glm::vec4 p1 = shifted[i]*glm::vec4(0,0,0,1);
        require(std::abs((p1.x-p0.x)*kResolution*.5f+1.f)<.002f,
                "Shadow grid failed to advance exactly one texel");
        require(intersects(origin[i],glm::vec3(0),1.f), "Origin receiver omitted");
        // A caster towards the sun has identical projected XY even when it
        // would be behind the camera: shadow culling must retain it.
        require(intersects(origin[i],-light*80.f,1.f), "Off-camera caster omitted");
        require(!intersects(origin[i],right*(kHalfWidths[i]+5.f),1.f), "Distant caster not culled");
        require(intersects(origin[i],right*(kHalfWidths[i]+.5f),1.f), "Edge caster sphere clipped");
        require(!intersects(origin[i],-light*400.f,1.f), "Caster outside depth volume retained");
        auto inverse = glm::inverse(origin[i]);
        for (float x : {-.79f,.79f}) for (float y : {-.79f,.79f}) {
            auto world = inverse*glm::vec4(x,y,.5f,1);
            if (i+1<kCount) {
                auto coarse = origin[i+1]*world;
                require(std::abs(coarse.x)<.8f && std::abs(coarse.y)<.8f,
                        "Coarse cascade does not cover fine transition region");
            }
        }
    }
    for (glm::vec3 direction : {glm::vec3(0,-1,0),glm::vec3(0,1,0),light}) {
        for (const auto& matrix : build(glm::vec3(17,8,-21),direction))
            for (int c=0;c<4;++c) for (int r=0;r<4;++r)
                require(std::isfinite(matrix[c][r]), "Singular light basis");
    }
}
