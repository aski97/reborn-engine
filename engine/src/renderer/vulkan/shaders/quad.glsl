// The push constants (ADR 0015, F1; ADR 0016; ADR 0027). Must match rb_vk_quad_push in
// vulkan_pipeline.h byte for byte: push constant blocks use the std430 layout, where a mat4 and a
// vec4 start on a 16-byte boundary and a vec2 on an 8-byte one (offsets 0, 64, 80, 88, 96, 104,
// 112: 120 bytes). `view` is pushed when the space changes, the rest with each draw.
layout(push_constant) uniform Quad {
    mat4 view;      // the current space (world units or pixels) to normalized coordinates
    vec4 color;     // linear, straight alpha (rb_color); multiplies the texture (ADR 0017)
    vec2 origin;    // the quad's top-left corner, already turned by the CPU, in the current space
    vec2 axis_x;    // its top edge, left to right, turned
    vec2 axis_y;    // its left edge, top to bottom, turned
    vec2 uv_offset; // the texture region's top-left corner, as fractions of the texture (ADR 0018)
    vec2 uv_size;   // the region's size, as fractions of the texture
} quad;
