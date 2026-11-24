// The push constants (ADR 0015, F1; ADR 0016). Must match rb_vk_quad_push in vulkan_pipeline.h
// byte for byte: push constant blocks use the std430 layout, where a mat4 and a vec4 start on a
// 16-byte boundary (offsets 0, 64, 72, 80, 88, 96: 112 bytes). `view` is pushed when the space changes,
// the rest with each draw.
layout(push_constant) uniform Quad {
    mat4 view;     // the current space (world units or pixels) to normalized coordinates
    vec2 position; // top-left corner, in the current space
    vec2 size;      // in the current space
    vec2 uv_offset; // the texture region's top-left corner, as fractions of the texture (ADR 0018)
    vec2 uv_size;   // the region's size, as fractions of the texture
    vec4 color;     // linear, straight alpha (rb_color); multiplies the texture (ADR 0017)
} quad;
