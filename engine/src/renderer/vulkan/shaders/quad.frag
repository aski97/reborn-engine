#version 450

#include "quad.glsl"

layout(location = 0) out vec4 out_color;

void main() {
    // The sRGB image encodes the linear color on write (ADR 0013, P1); blending uses alpha (H2).
    out_color = quad.color;
}
