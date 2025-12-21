#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec2 aTexCoords1; // [NEW] Second UV

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec2 TexCoords1; // [NEW]

uniform mat4 ModelMatrix;
uniform mat4 ViewMatrix;
uniform mat4 Projection;
uniform mat4 TIModelMatrix; // Transpose Inverse Model Matrix (用於法線變換)

void main()
{
    // 計算世界座標
    vec4 worldPos = ModelMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz; 
    
    TexCoords = aTexCoords;
    TexCoords1 = aTexCoords1; // [NEW]
    
    // 計算正確的法線變換 (移除縮放影響)
    // 如果 C++ 沒傳 TIModelMatrix，也可以在這裡算: mat3(transpose(inverse(ModelMatrix))) * aNormal;
    Normal = mat3(TIModelMatrix) * aNormal;
    
    gl_Position = Projection * ViewMatrix * worldPos;
}