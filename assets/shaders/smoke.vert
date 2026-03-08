#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aAlpha; 

out vec2 TexCoord;
out float Alpha;

uniform mat4 Projection;
uniform mat4 ViewMatrix;

void main()
{
    gl_Position = Projection * ViewMatrix * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
    Alpha = aAlpha;
}