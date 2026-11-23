#version 450

#include "quad.glsl"

// The quad's texture and sampler, from the descriptor set bound with the draw (ADR 0017). Plain
// quads bind a 1x1 white texture, which leaves the color as it is.
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

void main() {
    // The sRGB texture is decoded to linear when sampled; the tint is linear (rb_color). The sRGB
    // image encodes the result on write (ADR 0013, P1); blending uses alpha (ADR 0015, H2).
    out_color = texture(tex, uv) * quad.color;
}
