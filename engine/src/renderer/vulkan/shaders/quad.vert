#version 450

#include "quad.glsl"

// Two triangles sharing the diagonal from the top-left to the bottom-right corner, as fractions
// of the quad's size: no vertex buffer, gl_VertexIndex picks the corner (ADR 0015, F1).
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(1.0, 1.0), vec2(0.0, 1.0), vec2(0.0, 0.0));

// Where in the texture this corner reads (ADR 0017): the same fractions, so the whole texture
// covers the quad, its first row at the top. The rasterizer interpolates it for every pixel.
layout(location = 0) out vec2 out_uv;

void main() {
    vec2 corner = corners[gl_VertexIndex];
    vec2 point = quad.position + corner * quad.size;
    // The view maps the space onto Vulkan's normalized coordinates, -1 at the viewport's top-left
    // edge and +1 at its bottom-right, y down (ADR 0016). The 1 in w lets it translate.
    gl_Position = quad.view * vec4(point, 0.0, 1.0);
    out_uv = corner;
}
