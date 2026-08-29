#pragma once

#include <string>
#include <vector>

#include "../render/Vertex.h"

// Loads a 3D model file via Assimp and flattens its scene graph into the
// single shared Vertex format used by every mesh in the prototype. Color
// comes from each sub-mesh's diffuse material (gray fallback if absent).
class ModelLoader {
public:
    struct Result {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    static Result load(const std::string& path);
};
