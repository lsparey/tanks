// Depth comparisons are bilinearly reconstructed explicitly so D32 needs
// only nearest sampling support. No per-frame noise or lighting history.
float treeDepth(ivec2 pixel, int cascade) {
    int size = int(frame.treeShadowParams.y);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, ivec2(size)))) return 1.0;
    return texelFetch(treeShadowMap, ivec3(pixel, cascade), 0).r;
}

float treeCompare(vec2 uv, int cascade, float receiver) {
    vec2 pixel = uv * frame.treeShadowParams.y - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 weight = fract(pixel);
    float a = step(receiver, treeDepth(base, cascade));
    float b = step(receiver, treeDepth(base + ivec2(1,0), cascade));
    float c = step(receiver, treeDepth(base + ivec2(0,1), cascade));
    float d = step(receiver, treeDepth(base + ivec2(1,1), cascade));
    return mix(mix(a,b,weight.x), mix(c,d,weight.x), weight.y);
}

float filterTreeCascade(int cascade, vec3 world, vec3 normal, vec3 toLight) {
    float width = frame.treeShadowWidths[cascade];
    float texelWorld = width / frame.treeShadowParams.y;
    vec3 biasNormal = dot(normal,toLight) < 0.0 ? -normal : normal;
    vec3 position = world + biasNormal * min(texelWorld * 0.4, 0.04);
    vec3 clip = (frame.treeShadowMatrices[cascade] * vec4(position,1)).xyz;
    if (clip.z <= 0.0 || clip.z >= 1.0) return 1.0;
    vec2 uv = clip.xy * 0.5 + 0.5;
    float receiver = clip.z - 0.002 / frame.treeShadowWidths.w;
    float radius = 1.0;
    if (frame.treeShadowParams.x > 1.5) {
        // Directional-light PCSS: world penumbra = angular radius *
        // blocker/receiver separation. Depth is linear in these maps.
        float searchRadius = clamp(35.0 * tan(frame.atmosphere.w) / texelWorld, 1.0, 8.0);
        float blockerSum = 0.0;
        float blockers = 0.0;
        for (int i=0; i<12; ++i) {
            float angle = float(i) * 2.39996323;
            vec2 offset = vec2(cos(angle),sin(angle)) * sqrt((float(i)+0.5)/12.0);
            float depth = textureLod(treeShadowMap,
                vec3(uv + offset * searchRadius / frame.treeShadowParams.y, cascade),0).r;
            if (depth < receiver) { blockerSum += depth; blockers += 1.0; }
        }
        if (blockers > 0.0) {
            float separation = max(0.0, receiver - blockerSum / blockers) * frame.treeShadowWidths.w;
            radius = clamp(separation * tan(frame.atmosphere.w) / texelWorld, 1.0, 6.0);
        }
    }
    float sum = 0.0;
    if (radius <= 1.01) {
        for (int y=-1; y<=1; ++y)
            for (int x=-1; x<=1; ++x)
                sum += treeCompare(uv + vec2(x,y) / frame.treeShadowParams.y, cascade, receiver);
        return sum / 9.0;
    }
    // Fixed disk pattern avoids temporal sparkle; bilinear comparisons
    // preserve subtexel motion. Large radii remain an approximation.
    for (int i=0; i<16; ++i) {
        float angle = float(i) * 2.39996323;
        vec2 offset = vec2(cos(angle),sin(angle)) * sqrt((float(i)+0.5)/16.0);
        sum += treeCompare(uv + offset * radius / frame.treeShadowParams.y, cascade, receiver);
    }
    return sum / 16.0;
}

float mappedTreeShadow(vec3 world, vec3 normal, vec3 toLight) {
    for (int cascade=0; cascade<3; ++cascade) {
        vec3 clip = (frame.treeShadowMatrices[cascade] * vec4(world,1)).xyz;
        float edge = max(abs(clip.x),abs(clip.y));
        if (edge >= 0.94 || clip.z <= 0.0 || clip.z >= 1.0) continue;
        float visibility = filterTreeCascade(cascade,world,normal,toLight);
        float blend = smoothstep(0.80,0.94,edge);
        if (blend > 0.0) {
            float next = cascade < 2 ? filterTreeCascade(cascade+1,world,normal,toLight) : 1.0;
            visibility = mix(visibility,next,blend);
        }
        return visibility;
    }
    return 1.0;
}
