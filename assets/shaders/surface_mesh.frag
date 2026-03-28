#version 330 core

out vec4 o_Color;

in vec3 v_Normal;

uniform vec3 u_LightDirection = vec3(-0.6, -1.0, -0.45);
uniform vec3 u_LightColor = vec3(1.0, 0.98, 0.92);
uniform vec4 u_SurfaceColor = vec4(1.0, 0.92, 0.16, 0.42);
uniform vec4 u_LightFactors = vec4(0.35, 0.75, 0.12, 0.0);

void main()
{
    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    float diffuse = max(dot(normal, lightDir), 0.0);

    float ambient = max(u_LightFactors.x, 0.0);
    float diffuseStrength = max(u_LightFactors.y, 0.0);
    float fresnelStrength = max(u_LightFactors.z, 0.0);

    vec3 viewFacing = normalize(vec3(0.0, 0.0, 1.0));
    float fresnel = pow(1.0 - max(dot(normal, viewFacing), 0.0), 2.0);
    float light = ambient + diffuseStrength * diffuse + fresnelStrength * fresnel;
    vec3 lit = u_SurfaceColor.rgb * light * u_LightColor;
    o_Color = vec4(lit, u_SurfaceColor.a);
}
