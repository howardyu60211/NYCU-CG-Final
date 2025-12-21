#version 430

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

uniform mat4 Projection;
uniform mat4 ViewMatrix;
uniform mat4 ModelMatrix;
uniform mat4 TIModelMatrix;
uniform mat4 lightSpaceMatrix;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;
out vec4 FragPosLightSpace;

// TODO#3-3: vertex shader
void main() {
  // Transform position to world space
  FragPos = vec3(ModelMatrix * vec4(position, 1.0));

  FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
  
  // Transform normal to world space using Normal Matrix (Transpose Inverse)
  Normal = mat3(TIModelMatrix) * normal;
  
  TexCoord = texCoord;
  
  gl_Position = Projection * ViewMatrix * ModelMatrix * vec4(position, 1.0);
}