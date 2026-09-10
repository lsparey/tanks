#version 460

// Standard fullscreen-triangle trick: 3 vertices, no vertex buffer. Produces
// a triangle covering the whole [-1,1] NDC square, with fragUV spanning
// [0,1] over the visible screen area. No projection matrix is involved, so
// none of the other passes' negative-viewport-height flip (needed there to
// correct that matrix's NDC convention) applies here -- this pass samples
// the HDR target pixel-for-pixel in the same screen-space layout it was
// written in.
layout(location = 0) out vec2 fragUV;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
