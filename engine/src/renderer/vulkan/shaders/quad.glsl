// One quad, sent with each draw as push constants (ADR 0015, F1). Must match rb_vk_quad_push in
// vulkan_pipeline.h byte for byte: push constant blocks use the std430 layout, where a vec4 starts
// on a 16-byte boundary (offsets 0, 8, 16, 32: 40 bytes).
layout(push_constant) uniform Quad {
    vec2 position; // top-left corner, in pixels from the image's top left
    vec2 size;     // in pixels
    vec4 color;    // linear, straight alpha (rb_color)
    vec2 target;   // the image's size in pixels
} quad;
