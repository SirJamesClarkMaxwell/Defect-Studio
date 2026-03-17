// atoms_instanced.frag
// Purpose: Basic atom fragment shading for MVP renderer.
#version 330 core

out vec4 o_Color;

in vec3 v_Color;
in vec3 v_Normal;

uniform vec3 u_LightDirection = vec3(-0.6, -1.0, -0.45);
uniform vec3 u_LightFactors = vec3(0.45, 0.85, 1.0);
uniform vec3 u_LightColor = vec3(1.0, 0.98, 0.92);
uniform float u_GlowStrength = 0.08;

void main() {
    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    float diffuse = max(dot(normal, lightDir), 0.0);

    float ambient = max(u_LightFactors.x, 0.0);
    float diffuseStrength = max(u_LightFactors.y, 0.0);
    float light = ambient + diffuseStrength * diffuse;
    vec3 lit = v_Color * light * u_LightColor;
    vec3 glow = v_Color * max(u_GlowStrength, 0.0);
    o_Color = vec4(lit + glow, 1.0);
}
