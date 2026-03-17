// atoms_instanced.vert
// Purpose: Instanced atom vertex transform for MVP renderer.
#version 330 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec4 a_ModelRow0;
layout (location = 2) in vec4 a_ModelRow1;
layout (location = 3) in vec4 a_ModelRow2;
layout (location = 4) in vec4 a_ModelRow3;
layout (location = 5) in vec3 a_InstanceColor;

uniform mat4 u_ViewProjection;

out vec3 v_Color;

void main() {
    mat4 model = mat4(a_ModelRow0, a_ModelRow1, a_ModelRow2, a_ModelRow3);
    gl_Position = u_ViewProjection * model * vec4(a_Position, 1.0);
    v_Color = a_InstanceColor;
}
