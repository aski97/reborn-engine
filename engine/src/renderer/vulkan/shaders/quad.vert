#version 450

#include "quad.glsl"

// Two triangles sharing the diagonal from the top-left to the bottom-right corner, as fractions
// of the quad's size: no vertex buffer, gl_VertexIndex picks the corner (ADR 0015, F1).
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(1.0, 1.0), vec2(0.0, 1.0), vec2(0.0, 0.0));

void main() {
    vec2 point = quad.position + corners[gl_VertexIndex] * quad.size;
    // The view maps the space onto Vulkan's normalized coordinates, -1 at the viewport's top-left
    // edge and +1 at its bottom-right, y down (ADR 0016). The 1 in w lets it translate.
    gl_Position = quad.view * vec4(point, 0.0, 1.0);
}
