#pragma once

#include <string>
#include <vector>

#include "../render/Vertex.h"

// Loads a 3D model file via Assimp into the single shared Vertex format used
// by every mesh in the prototype. Color comes from each sub-mesh's diffuse
// material (gray fallback if absent).
//
// Geometry is kept as one Part per Assimp sub-mesh (Assimp always splits a
// mesh into one aiMesh per material, so this naturally preserves any
// material-based grouping baked into the source file -- e.g. tank.x has
// separate "Tracks"/"Base"/"Detail"/"Turret"/"Barrel" materials, which is
// how Tank tells the turret/barrel apart from the hull without needing any
// separate frame/node hierarchy in the file).
class ModelLoader {
public:
    struct Part {
        std::string materialName;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct Result {
        std::vector<Part> parts;
    };

    static Result load(const std::string& path);
};
