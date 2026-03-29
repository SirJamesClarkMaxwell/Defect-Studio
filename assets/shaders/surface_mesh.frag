#version 330 core

out vec4 o_Color;

in vec3 v_Normal;
in vec3 v_WorldPosition;

uniform vec3 u_LightDirection = vec3(-0.6, -1.0, -0.45);
uniform vec3 u_LightColor = vec3(1.0, 0.98, 0.92);
uniform vec3 u_CameraPosition = vec3(0.0, 0.0, 6.0);
uniform vec4 u_SurfaceColor = vec4(1.0, 0.92, 0.16, 0.42);
uniform vec3 u_SpecularColor = vec3(0.0);
uniform float u_Shininess = 100.0;
uniform vec4 u_LightFactors = vec4(0.35, 0.75, 0.12, 0.0);

void main()
{
    vec3 normal = normalize(v_Normal);
    if (!gl_FrontFacing)
    {
        normal = -normal;
    }

    vec3 lightDir = normalize(-u_LightDirection);
    vec3 viewDir = normalize(u_CameraPosition - v_WorldPosition);
    float diffuse = max(dot(normal, lightDir), 0.0);

    float ambient = max(u_LightFactors.x, 0.0);
    float diffuseStrength = max(u_LightFactors.y, 0.0);
    float fresnelStrength = max(u_LightFactors.z, 0.0);
    float specularStrength = max(u_LightFactors.w, 0.0);

    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    vec3 halfVector = normalize(lightDir + viewDir);
    float specular = pow(max(dot(normal, halfVector), 0.0), max(u_Shininess, 1.0));
    float light = ambient + diffuseStrength * diffuse + fresnelStrength * fresnel;
    vec3 lit = u_SurfaceColor.rgb * light * u_LightColor + u_SpecularColor * (specularStrength * specular);
    o_Color = vec4(lit, u_SurfaceColor.a);
}
