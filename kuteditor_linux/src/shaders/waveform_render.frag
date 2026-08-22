#version 450

/*
 * waveform_render.frag — Simple passthrough fragment shader
 *
 * Outputs the interpolated vertex color with pre-multiplied alpha
 * as required by Qt Quick's scene graph compositor.
 */

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 fragColor;

void main() {
    // Colors are already pre-multiplied by the vertex generator
    fragColor = v_color;
}
