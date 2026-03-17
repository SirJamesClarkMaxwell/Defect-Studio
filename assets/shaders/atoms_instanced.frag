// atoms_instanced.frag
// Purpose: Basic atom fragment shading for MVP renderer.
#version 330 core

out vec4 o_Color;

uniform vec3 u_AtomColor = vec3(0.35, 0.75, 0.95);

void main() {
    o_Color = vec4(u_AtomColor, 1.0);
}
