// atoms_instanced.frag
// Purpose: Basic atom fragment shading for MVP renderer.
#version 330 core

out vec4 o_Color;

in vec3 v_Color;
in vec3 v_Normal;

uniform vec3 u_LightDirection = vec3(-0.6, -1.0, -0.45);

void main() {
    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    float diffuse = max(dot(normal, lightDir), 0.0);

    float light = 0.30 + 0.70 * diffuse;
    o_Color = vec4(v_Color * light, 1.0);
}
