#version 450

#include "quad.glsl"

// Two triangles sharing the diagonal from the top-left to the bottom-right corner, as fractions
// of the quad's size: no vertex buffer, gl_VertexIndex picks the corner (ADR 0015, F1).
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(1.0, 1.0), vec2(0.0, 1.0), vec2(0.0, 0.0));

// Where in the texture this corner reads (ADR 0017): the same fractions, scaled into the region
// the draw asked for (ADR 0018; the whole texture by default), its first row at the top. The
// rasterizer interpolates it for every pixel.
layout(location = 0) out vec2 out_uv;

void main() {
    vec2 corner = corners[gl_VertexIndex];
    // The CPU has turned the quad (ADR 0027): a corner of the unit square goes along its two
    // edges, so any parallelogram comes out of the same two lines. Upright, the edges are
    // (width, 0) and (0, height), and this is the rectangle's corner as before.
    vec2 point = quad.origin + corner.x * quad.axis_x + corner.y * quad.axis_y;
    // The view maps the space onto Vulkan's normalized coordinates, -1 at the viewport's top-left
    // edge and +1 at its bottom-right, y down (ADR 0016). The 1 in w lets it translate.
    gl_Position = quad.view * vec4(point, 0.0, 1.0);
    out_uv = quad.uv_offset + corner * quad.uv_size;
}
