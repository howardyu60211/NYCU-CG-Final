#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 ModelMatrix;

void main()
{
    gl_Position = lightSpaceMatrix * ModelMatrix * vec4(aPos, 1.0);
}