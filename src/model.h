#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct SubDraw {
  int first;
  int count;

  // --- 貼圖索引 (Texture Indices) ---
  int baseColorTexIndex = -1;
  int normalTexIndex = -1;    // [新增] 法線貼圖
  int ormTexIndex = -1;       // [新增] ORM (Occlusion-Roughness-Metallic)
  int emissiveTexIndex = -1;  // [新增] 自發光貼圖

  // --- PBR 係數 (Factors) - 預設值很重要 ---
  glm::vec4 baseColorFactor = glm::vec4(1.0f);  // 預設白色
  float metallicFactor = 1.0f;                  // 預設 1.0
  float roughnessFactor = 1.0f;                 // 預設 1.0
  glm::vec3 emissiveFactor = glm::vec3(0.0f);   // 預設不發光
};

class Model {
 public:
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> texcoords;
  std::vector<float> texcoords1;  // [NEW] Second UV set
  std::vector<GLuint> textures;
  std::vector<SubDraw> meshes;  // Added for multi-material support

  int numVertex = 0;
  GLenum drawMode = GL_TRIANGLES;
  glm::mat4 modelMatrix = glm::mat4(1.0f);

  Model() = default;

  // Static factory method as used in main.cpp
  // Static factory method as used in main.cpp
  static Model* createCube();
  static Model* fromObjectFile(const std::string& filename);
  static Model* fromGLBFile(const std::string& filename);
  static Model* loadSplitGLB(const std::string& filename, const std::vector<std::string>& splitNames,
                             std::vector<Model*>& outModels, std::vector<glm::mat4>& outTransforms);
};

struct Material {
  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;
  float shininess;
  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;
  float reflectivity = 0.0f;
};

struct Object {
  int modelIndex;
  int textureIndex = -1;           // Added missing member
  int roughnessTextureIndex = -1;  // [NEW] Roughness map index
  glm::mat4 transformMatrix;
  Material material;

  Object(int index, glm::mat4 transform) : modelIndex(index), transformMatrix(transform) {}
};
