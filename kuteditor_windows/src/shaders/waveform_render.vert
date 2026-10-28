#version 450

/*
 * waveform_render.vert — Vertex shader for waveform rendering
 *
 * Standard vertex attributes from QRhi vertex buffer:
 *   location 0: vec2 position (pixel coords, origin top-left)
 *   location 1: vec4 color (pre-multiplied RGBA)
 *
 * Transforms pixel coordinates to NDC using viewport uniform.
 * QRhi NDC: X [-1,1] left→right, Y [-1,1] bottom→top
 * Pixel coords: (0,0) = top-left, (W,H) = bottom-right
 */

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform Transform {
    vec4 viewport; // .x = width, .y = height, .zw unused (padding)
};

void main() {
    // Convert pixel coords to NDC
    // X: 0 → -1 (left), width → 1 (right)
    // Y: 0 → 1 (top), height → -1 (bottom)  ← flip for QRhi NDC
    float ndcX = (inPosition.x / viewport.x)  * 2.0 - 1.0;
    float ndcY = 1.0 - (inPosition.y / viewport.y) * 2.0;

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    v_color = inColor;
}
