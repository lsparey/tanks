// Gather raw depths and reconstruct bilinear comparisons explicitly. D32
// still needs only nearest sampling support. The sampler supplies a lit
// border, matching the former out-of-bounds texelFetch checks.
// Gather component order: offsets (0,1), (1,1), (1,0), (0,0).
float treeCompare(vec2 uv, int cascade, float receiver) {
    vec2 weight = fract(uv * frame.treeShadowParams.y - 0.5);
    vec4 visibility = step(vec4(receiver), textureGather(treeShadowMap, vec3(uv,cascade), 0));
    return mix(mix(visibility.w,visibility.z,weight.x),
               mix(visibility.x,visibility.y,weight.x),weight.y);
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
        // Nine bilinear comparisons share a 4x4 texel footprint. Gather
        // each texel once and apply the equivalent separable box weights.
        vec2 texel = vec2(1.0 / frame.treeShadowParams.y);
        vec2 f = fract(uv * frame.treeShadowParams.y - 0.5);
        vec4 a = step(vec4(receiver),textureGather(treeShadowMap,vec3(uv-texel,cascade),0));
        vec4 b = step(vec4(receiver),textureGather(treeShadowMap,vec3(uv+vec2(texel.x,-texel.y),cascade),0));
        vec4 c = step(vec4(receiver),textureGather(treeShadowMap,vec3(uv+vec2(-texel.x,texel.y),cascade),0));
        vec4 d = step(vec4(receiver),textureGather(treeShadowMap,vec3(uv+texel,cascade),0));
        float row0 = a.w*(1.0-f.x)+a.z+b.w+b.z*f.x;
        float row1 = a.x*(1.0-f.x)+a.y+b.x+b.y*f.x;
        float row2 = c.w*(1.0-f.x)+c.z+d.w+d.z*f.x;
        float row3 = c.x*(1.0-f.x)+c.y+d.x+d.y*f.x;
        return (row0*(1.0-f.y)+row1+row2+row3*f.y)/9.0;
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
