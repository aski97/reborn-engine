#version 450

#include "quad.glsl"

// Two triangles sharing the diagonal from the top-left to the bottom-right corner, as fractions
// of the quad's size: no vertex buffer, gl_VertexIndex picks the corner (ADR 0015, F1).
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(1.0, 1.0), vec2(0.0, 1.0), vec2(0.0, 0.0));

void main() {
    vec2 pixel = quad.position + corners[gl_VertexIndex] * quad.size;
    // Pixels to Vulkan's normalized coordinates: -1 at the top-left edge, +1 at the bottom-right,
    // y pointing down like the pixels (B1), so no flip.
    vec2 ndc = pixel / quad.target * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
