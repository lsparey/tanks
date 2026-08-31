#pragma once

#include <cstdint>
#include <vector>

// Procedurally generates the two textures for the play-area boundary (see
// BoundaryGenerator): a red ground-line decal that reads as roughly painted
// onto the grass, and a vertical "wall of light" gradient that fades out
// with height. Kept as one class since both are one feature's two textures,
// the same relationship Mesh::treeBark/treeLeaves have to a single tree.
class BoundaryTextureGenerator {
public:
    // u (texture x) spans the line's width, tiling along v (length) --
    // mostly-opaque red center with a soft, noise-roughened edge so the
    // strip reads as painted rather than a perfectly clean-edged decal.
    static std::vector<uint8_t> generateGroundLine(uint32_t size);

    // u (texture x) tiles along the wall's length; v (texture y) runs 0 at
    // the base to 1 at the top, where alpha has already fully faded out --
    // callers size the wall mesh's own height so that top edge sits above
    // the tallest trees (see BoundaryGenerator::buildWallMesh).
    static std::vector<uint8_t> generateWall(uint32_t size);
};
