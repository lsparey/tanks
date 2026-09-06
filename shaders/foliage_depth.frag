#version 450

layout(location = 9) flat in vec4 fragFoliageFade;

// Resolve opaque LOD coverage without running lighting or ray queries.
void main() {
    if (abs(fragFoliageFade.y) > .5) {
        // Paired levels use complementary masks at the same screen pixel.
        // The pattern is stable in time; only the blend amount changes.
        uvec2 pixel = uvec2(gl_FragCoord.xy);
        uint hash = pixel.x * 1664525u + pixel.y * 1013904223u
                    + uint(fragFoliageFade.z) * 747796405u;
        hash = (hash ^ (hash >> 16u)) * 2246822519u;
        hash ^= hash >> 13u;
        float threshold = (float(hash & 65535u) + .5) / 65536.0;
        bool finePixel = threshold < fragFoliageFade.x;
        if (finePixel != (fragFoliageFade.y > 0.0)) discard;
    }

}
