// grid_lines.frag
// Purpose: Viewport ground grid line color shader.
#version 330 core

out vec4 o_Color;

uniform vec3 u_GridColor = vec3(0.35, 0.38, 0.44);

void main() {
    o_Color = vec4(u_GridColor, 1.0);
}
