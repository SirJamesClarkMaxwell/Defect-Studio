// atoms_instanced.frag
// Purpose: Basic atom fragment shading for MVP renderer.
#version 330 core

out vec4 o_Color;

in vec3 v_Color;

void main() {
    o_Color = vec4(v_Color, 1.0);
}
