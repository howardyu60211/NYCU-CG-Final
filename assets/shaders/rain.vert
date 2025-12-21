#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 Projection;
uniform mat4 ViewMatrix;
uniform mat4 ModelMatrix;

void main()
{
    gl_Position = Projection * ViewMatrix * vec4(aPos, 1.0);
}
