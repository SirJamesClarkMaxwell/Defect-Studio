// grid_lines.frag
// Purpose: Viewport ground grid line color shader.
#version 330 core

out vec4 o_Color;
in vec3 v_Color;

uniform vec3 u_GridColor = vec3(0.35, 0.38, 0.44);
uniform float u_UseVertexColor = 0.0;

void main() {
    vec3 finalColor = mix(u_GridColor, v_Color, clamp(u_UseVertexColor, 0.0, 1.0));
    o_Color = vec4(finalColor, 1.0);
}
