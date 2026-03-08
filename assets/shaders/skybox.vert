#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 TexCoords;
uniform mat4 Projection;
uniform mat4 ViewMatrix;

// TODO#3-1: vertex shader
void main() {
    TexCoords = aPos;
    
    vec4 pos = Projection * ViewMatrix * vec4(aPos, 1.0);
    
    // Optimization: Set z to w so that z/w = 1.0 (max depth)
    gl_Position = pos.xyww;
}