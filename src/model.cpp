#include "model.h"
#include <GLFW/glfw3.h>  // [新增] 為了使用 glfwExtensionSupported
#include <algorithm>
#include <future>
#include <iostream>
#include <map>
#include <vector>
#include "gl_helper.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define TINYGLTF_IMPLEMENTATION
// #define STB_IMAGE_IMPLEMENTATION
// #define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
// #define TINYGLTF_NO_STB_IMAGE // We use built-in stbi
#include <glm/gtc/type_ptr.hpp>
#include "tiny_gltf.h"

Model* Model::fromGLBFile(const std::string& filename) {
  tinygltf::Model gltfModel;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);

  if (!warn.empty()) {
    std::cout << "TinyGLTF Warning: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << "TinyGLTF Error: " << err << std::endl;
  }

  if (!ret) {
    return nullptr;
  }

  Model* model = new Model();

  // 1. Process Textures
  for (const auto& img : gltfModel.images) {
    GLuint texID = 0;
    if (!img.image.empty()) {
      texID = createTextureFromData((unsigned char*)img.image.data(), img.width, img.height, img.component);

      // Apply Mipmaps and Filtering like in fromObjectFile
      if (texID != 0) {
        glBindTexture(GL_TEXTURE_2D, texID);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
      }
    }
    model->textures.push_back(texID);
  }

  // 2. Traverse Nodes (DFS)
  struct NodeState {
    int nodeIdx;
    glm::mat4 parentTransform;
  };
  std::vector<NodeState> nodeStack;

  const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
  for (int nodeIdx : scene.nodes) {
    nodeStack.push_back({nodeIdx, glm::mat4(1.0f)});
  }

  while (!nodeStack.empty()) {
    NodeState state = nodeStack.back();
    nodeStack.pop_back();

    if (state.nodeIdx < 0 || state.nodeIdx >= (int)gltfModel.nodes.size()) continue;
    const tinygltf::Node& node = gltfModel.nodes[state.nodeIdx];

    // Compute Global Transform
    glm::mat4 localTransform(1.0f);
    if (node.matrix.size() == 16) {
      localTransform = glm::make_mat4(node.matrix.data());
    } else {
      if (node.translation.size() == 3) {
        localTransform = glm::translate(
            localTransform,
            glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
      }
      if (node.rotation.size() == 4) {
        // GLTF rotation is quaternion (x, y, z, w) BUT glm::quat constructor is (w, x, y, z)
        // Accessors: [0]=x, [1]=y, [2]=z, [3]=w
        // glm::quat q(w, x, y, z)
        glm::quat q((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
        localTransform = localTransform * glm::mat4_cast(q);
      }
      if (node.scale.size() == 3) {
        localTransform =
            glm::scale(localTransform, glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
      }
    }
    glm::mat4 globalTransform = state.parentTransform * localTransform;

    // Process Mesh
    if (node.mesh >= 0 && node.mesh < (int)gltfModel.meshes.size()) {
      const tinygltf::Mesh& mesh = gltfModel.meshes[node.mesh];
      for (const auto& prim : mesh.primitives) {
        // Extract POSITION
        if (prim.attributes.find("POSITION") == prim.attributes.end()) continue;

        const tinygltf::Accessor& posAcc = gltfModel.accessors[prim.attributes.at("POSITION")];
        const tinygltf::BufferView& posView = gltfModel.bufferViews[posAcc.bufferView];
        const tinygltf::Buffer& posBuf = gltfModel.buffers[posView.buffer];
        const unsigned char* posData = posBuf.data.data() + posView.byteOffset + posAcc.byteOffset;
        int posStride = posAcc.ByteStride(posView);
        if (posStride == 0) {
          posStride =
              tinygltf::GetComponentSizeInBytes(posAcc.componentType) * tinygltf::GetNumComponentsInType(posAcc.type);
        }

        // Extract NORMAL
        const unsigned char* normData = nullptr;
        int normStride = 0;
        if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("NORMAL")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          normData = buf.data.data() + view.byteOffset + acc.byteOffset;
          normStride = acc.ByteStride(view);
          if (normStride == 0) {
            normStride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        // Extract TEXCOORD_0
        const unsigned char* uvData = nullptr;
        int uvStride = 0;
        if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          uvData = buf.data.data() + view.byteOffset + acc.byteOffset;
          uvStride = acc.ByteStride(view);
          if (uvStride == 0) {
            uvStride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        // Extract TEXCOORD_1 (for Roughness Map)
        const unsigned char* uv1Data = nullptr;
        int uv1Stride = 0;
        if (prim.attributes.find("TEXCOORD_1") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_1")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          uv1Data = buf.data.data() + view.byteOffset + acc.byteOffset;
          uv1Stride = acc.ByteStride(view);
          if (uv1Stride == 0) {
            uv1Stride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        // Attributes Extraction Helper
        auto getVec3 = [&](const unsigned char* ptr, int stride, int index, int componentType) {
          const unsigned char* src = ptr + index * stride;
          glm::vec3 ret(0.0f);
          for (int c = 0; c < 3; c++) {
            float val = 0.0f;
            if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
              val = ((const float*)src)[c];
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              val = ((const unsigned short*)src)[c] / 65535.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              val = ((const unsigned char*)src)[c] / 255.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
              val = std::max(((const short*)src)[c] / 32767.0f, -1.0f);
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
              val = std::max(((const char*)src)[c] / 127.0f, -1.0f);
            }
            ret[c] = val;
          }
          return ret;
        };
        auto getVec2 = [&](const unsigned char* ptr, int stride, int index, int componentType) {
          const unsigned char* src = ptr + index * stride;
          glm::vec2 ret(0.0f);
          for (int c = 0; c < 2; c++) {
            float val = 0.0f;
            if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
              val = ((const float*)src)[c];
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              val = ((const unsigned short*)src)[c] / 65535.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              val = ((const unsigned char*)src)[c] / 255.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
              val = std::max(((const short*)src)[c] / 32767.0f, -1.0f);
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
              val = std::max(((const char*)src)[c] / 127.0f, -1.0f);
            }
            ret[c] = val;
          }
          return ret;
        };

        // Indices
        std::vector<uint32_t> indices;
        if (prim.indices >= 0) {
          const tinygltf::Accessor& idxAcc = gltfModel.accessors[prim.indices];
          const tinygltf::BufferView& idxView = gltfModel.bufferViews[idxAcc.bufferView];
          const tinygltf::Buffer& idxBuf = gltfModel.buffers[idxView.buffer];
          const unsigned char* idxData = idxBuf.data.data() + idxView.byteOffset + idxAcc.byteOffset;
          int idxStride = idxAcc.ByteStride(idxView);

          for (size_t i = 0; i < idxAcc.count; i++) {
            if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              const unsigned short* p = (const unsigned short*)(idxData + i * (idxStride ? idxStride : 2));
              indices.push_back(*p);
            } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
              const unsigned int* p = (const unsigned int*)(idxData + i * (idxStride ? idxStride : 4));
              indices.push_back(*p);
            } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              const unsigned char* p = (const unsigned char*)(idxData + i * (idxStride ? idxStride : 1));
              indices.push_back(*p);
            }
          }
        } else {
          for (size_t i = 0; i < posAcc.count; i++) indices.push_back(i);
        }

        // Build Mesh
        SubDraw submesh;
        submesh.name = node.name.empty() ? mesh.name : node.name;  // Use Node name if available, else Mesh name
        submesh.first = model->positions.size() / 3;

        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(globalTransform)));

        for (uint32_t i : indices) {
          glm::vec3 pos = getVec3(posData, posStride, i, posAcc.componentType);
          glm::vec4 worldPos = globalTransform * glm::vec4(pos, 1.0f);
          model->positions.push_back(worldPos.x);
          model->positions.push_back(worldPos.y);
          model->positions.push_back(worldPos.z);

          if (normData) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("NORMAL")];
            glm::vec3 norm = getVec3(normData, normStride, i, acc.componentType);
            norm = glm::normalize(normalMatrix * norm);
            model->normals.push_back(norm.x);
            model->normals.push_back(norm.y);
            model->normals.push_back(norm.z);
          } else {
            model->normals.push_back(0);
            model->normals.push_back(1);
            model->normals.push_back(0);
          }

          if (uvData) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
            glm::vec2 uv = getVec2(uvData, uvStride, i, acc.componentType);
            model->texcoords.push_back(uv.x);
            model->texcoords.push_back(1.0f - uv.y);
          } else {
            model->texcoords.push_back(0);
            model->texcoords.push_back(0);
          }

          if (uv1Data) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_1")];
            glm::vec2 uv1 = getVec2(uv1Data, uv1Stride, i, acc.componentType);
            model->texcoords1.push_back(uv1.x);
            model->texcoords1.push_back(1.0f - uv1.y);
          } else {
            // Fallback: Use UV0 if UV1 missing, or 0
            if (uvData) {
              const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
              glm::vec2 uv = getVec2(uvData, uvStride, i, acc.componentType);
              model->texcoords1.push_back(uv.x);
              model->texcoords1.push_back(1.0f - uv.y);
            } else {
              model->texcoords1.push_back(0);
              model->texcoords1.push_back(0);
            }
          }
        }

        submesh.count = indices.size();

        // 預設值 (如果沒有材質)
        submesh.baseColorTexIndex = -1;
        submesh.ormTexIndex = -1;
        submesh.normalTexIndex = -1;
        submesh.emissiveTexIndex = -1;

        if (prim.material >= 0) {
          const tinygltf::Material& mat = gltfModel.materials[prim.material];
          const auto& pbr = mat.pbrMetallicRoughness;

          // 1. Base Color (貼圖 + 係數)
          if (pbr.baseColorTexture.index >= 0) {
            submesh.baseColorTexIndex = gltfModel.textures[pbr.baseColorTexture.index].source;
          }
          // [重要] 讀取顏色因子 (例如: 紅色車漆可能沒有貼圖，只有顏色)
          if (pbr.baseColorFactor.size() == 4) {
            submesh.baseColorFactor = glm::vec4((float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                                                (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3]);
          }

          // 2. Metallic / Roughness (貼圖 + 係數)
          if (pbr.metallicRoughnessTexture.index >= 0) {
            submesh.ormTexIndex = gltfModel.textures[pbr.metallicRoughnessTexture.index].source;
          }
          // [重要] 讀取粗糙度與金屬度因子
          // glTF 規定：最終值 = 貼圖採樣值 * 因子
          submesh.roughnessFactor = (float)pbr.roughnessFactor;
          submesh.metallicFactor = (float)pbr.metallicFactor;

          // 3. Normal Map (法線貼圖) - 讓畫質逼真的關鍵
          if (mat.normalTexture.index >= 0) {
            submesh.normalTexIndex = gltfModel.textures[mat.normalTexture.index].source;
          }

          // 4. Emissive (自發光) - 紅綠燈需要
          if (mat.emissiveTexture.index >= 0) {
            submesh.emissiveTexIndex = gltfModel.textures[mat.emissiveTexture.index].source;
          }
          if (mat.emissiveFactor.size() == 3) {
            submesh.emissiveFactor =
                glm::vec3((float)mat.emissiveFactor[0], (float)mat.emissiveFactor[1], (float)mat.emissiveFactor[2]);
          }
        }

        model->meshes.push_back(submesh);
      }
    }

    // Push Children
    for (int childIdx : node.children) {
      nodeStack.push_back({childIdx, globalTransform});
    }
  }

  model->numVertex = model->positions.size() / 3;
  std::cout << "Loaded GLB: " << filename << " (" << model->numVertex << " vertices)" << std::endl;
  return model;
}

Model* Model::createCube() {
  Model* model = new Model();
  // Cube vertices (36 vertices)
  float vertices[] = {
      // Back face
      -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f,
      -0.5f,
      // Front face
      -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f,
      // Left face
      -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f,
      0.5f,
      // Right face
      0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
      // Bottom face
      -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f,
      -0.5f,
      // Top face
      -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};

  // Normals (simplified, same for all faces? No, need face normals)
  // Just use 0,1,0 for now or calculate? Or just use vertices as normals for sphere-like?
  // Actually geometry pass needs valid normals.
  // 36 vertices.
  float normals[] = {0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f,
                     0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f,

                     0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
                     0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,

                     -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,
                     -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,

                     1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
                     1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

                     0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,
                     0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  -1.0f, 0.0f,

                     0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
                     0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f};

  for (int i = 0; i < 36 * 3; ++i) {
    model->positions.push_back(vertices[i]);
    model->normals.push_back(normals[i]);
  }
  // Dummy UVs
  for (int i = 0; i < 36; ++i) {
    model->texcoords.push_back(0.0f);
    model->texcoords.push_back(0.0f);
  }

  model->numVertex = 36;

  // Create one submesh
  SubDraw mesh;
  mesh.first = 0;
  mesh.count = 36;
  mesh.baseColorFactor = glm::vec4(1.0f);  // White base
  model->meshes.push_back(mesh);

  return model;
}

Model* Model::fromObjectFile(const std::string& filename) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  std::cout << "Loading model: " << filename << std::endl;

  std::string base_dir = filename.substr(0, filename.find_last_of("/\\") + 1);
  std::string mtl_base = base_dir;
  // Special handling for town4new structure where MTL refers to textures in parent dir
  // and OBJ refers to MTL in sub-dir relative to execution or incorrectly
  if (filename.find("town4new") != std::string::npos) {
    // The OBJ is in .../source/town4new.obj
    // The MTL is in .../source/town4new.mtl
    // But textures are in .../ (parent of source)
    // And MTL is loaded relative to base_dir?
    // Actually, if we pass base_dir as .../source/, it finds MTL.
    // But textures in MTL are like "TrashDecal1.png".
    // So if mtl_base is .../source/, it looks for .../source/TrashDecal1.png -> Fail.
    // We might need to adjust texture loading path below, OR adjust mtl_base if that affects textures.
    // tinyobj loads MTL. It stores texture names. It doesn't check texture existence.
    // WE load textures. So we need the correct path for textures.
    // Let's assume textures are in parent dir of OBJ for town4new.
    mtl_base = base_dir + "../";
  }

  bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), mtl_base.c_str());

  if (!warn.empty()) {
    std::cout << "TinyObjLoader Warning: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << "TinyObjLoader Error: " << err << std::endl;
  }

  if (!ret) {
    return nullptr;
  }

  Model* model = new Model();

  // 1. Reserve Memory
  size_t total_indices = 0;
  for (const auto& shape : shapes) {
    total_indices += shape.mesh.indices.size();
  }
  model->positions.reserve(total_indices);  // Each index corresponds to one vertex, which has 3 components
  model->normals.reserve(total_indices);    // Same for normals
  model->texcoords.reserve(total_indices * 2 /
                           3);  // Each index corresponds to one vertex, which has 2 components for texcoords.
                                // total_indices is number of vertex indices, not number of vertices.
                                // A face has 3 indices, so total_indices / 3 is number of faces.
                                // Each face has 3 vertices, so total_indices is number of vertices.
                                // So, total_indices * 3 for positions/normals, total_indices * 2 for texcoords.
  // Corrected reservation:
  model->positions.reserve(total_indices);  // Each index corresponds to one vertex, which has 3 components
  model->normals.reserve(total_indices);    // Same for normals
  model->texcoords.reserve(
      total_indices);  // Each index corresponds to one vertex, which has 2 components for texcoords.
  model->texcoords1.reserve(total_indices);  // Reserve for UV1 as well

  // Group faces by Material ID to estimate mesh count
  std::map<int, std::vector<tinyobj::index_t>> materialGroups;
  for (const auto& shape : shapes) {
    for (size_t f = 0; f < shape.mesh.material_ids.size(); f++) {
      int matId = shape.mesh.material_ids[f];
      // 3 vertices per face (triangulated)
      materialGroups[matId].push_back(shape.mesh.indices[3 * f + 0]);
      materialGroups[matId].push_back(shape.mesh.indices[3 * f + 1]);
      materialGroups[matId].push_back(shape.mesh.indices[3 * f + 2]);
    }
  }
  model->meshes.reserve(materialGroups.size());  // Approximation

  // 2. Parallel Texture Loading
  struct TextureResult {
    unsigned char* data;
    int w, h, c;
  };

  std::vector<std::future<TextureResult>> heavyTasks;
  std::map<std::string, int> uniqueTexturePaths;  // path -> index in heavyTasks/model->textures
  std::vector<int> matToTexIndex(materials.size(), -1);

  for (size_t i = 0; i < materials.size(); i++) {
    if (!materials[i].diffuse_texname.empty()) {
      std::string texPath = base_dir + materials[i].diffuse_texname;
      if (filename.find("town4new") != std::string::npos) {
        texPath = base_dir + "../" + materials[i].diffuse_texname;
      }

      // Check if already scheduled
      if (uniqueTexturePaths.find(texPath) == uniqueTexturePaths.end()) {
        int idx = heavyTasks.size();
        uniqueTexturePaths[texPath] = idx;

        heavyTasks.push_back(std::async(std::launch::async, [texPath]() {
          TextureResult res;
          res.data = loadTextureCPU(texPath.c_str(), &res.w, &res.h, &res.c);
          return res;
        }));
      }
      matToTexIndex[i] = uniqueTexturePaths[texPath];
    }
  }

  // 3. Process Textures (Upload)
  // 3. Process Textures (Upload)
  for (auto& fut : heavyTasks) {
    TextureResult res = fut.get();
    if (res.data) {
      // 呼叫底層函式建立紋理 ID
      GLuint texID = createTextureFromData(res.data, res.w, res.h, res.c);

      // =========================================================
      // [關鍵修復] 手動啟用高品質過濾 (Mipmap + Anisotropic)
      // =========================================================
      if (texID != 0) {
        glBindTexture(GL_TEXTURE_2D, texID);

        // 1. 生成 Mipmap (解決遠處噪點的第一步)
        // 必備！沒有這行，設 GL_LINEAR_MIPMAP_LINEAR 會變黑
        glGenerateMipmap(GL_TEXTURE_2D);

        // 2. 設定三線性過濾 (Trilinear Filtering)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);
      }
      // =========================================================

      model->textures.push_back(texID);
      freeTextureData(res.data);
    } else {
      model->textures.push_back(0);
    }
  }

  // Default Material Group (if matId -1 exists or empty) and Process Groups
  for (auto& group : materialGroups) {
    int matId = group.first;
    const auto& indices = group.second;

    SubDraw submesh;
    submesh.name = "OBJ_Mesh";  // Fallback for OBJ
    submesh.first = model->positions.size() / 3;
    submesh.baseColorTexIndex = (matId >= 0 && matId < (int)matToTexIndex.size()) ? matToTexIndex[matId] : -1;

    // If textureIndex is -1 (no texture or invalid mat), use a fallback?
    // Let's assume index 0 of loaded textures is "default" if we force one?
    // Or if logic in main.cpp added a fallback.
    // For now, leave it -1 and let validation handle or crash? NO.
    // We really should provide a safe default.

    for (const auto& idx : indices) {
      int v_idx = idx.vertex_index;
      // Bounds check
      if (v_idx < 0 || (size_t)(3 * v_idx + 2) >= attrib.vertices.size()) {
        continue;
      }
      model->positions.push_back(attrib.vertices[3 * v_idx + 0]);
      model->positions.push_back(attrib.vertices[3 * v_idx + 1]);
      model->positions.push_back(attrib.vertices[3 * v_idx + 2]);

      int n_idx = idx.normal_index;
      if (n_idx >= 0 && (size_t)(3 * n_idx + 2) < attrib.normals.size()) {
        model->normals.push_back(attrib.normals[3 * n_idx + 0]);
        model->normals.push_back(attrib.normals[3 * n_idx + 1]);
        model->normals.push_back(attrib.normals[3 * n_idx + 2]);
      } else {
        model->normals.push_back(0);
        model->normals.push_back(1);
        model->normals.push_back(0);
      }

      int t_idx = idx.texcoord_index;
      if (t_idx >= 0 && (size_t)(2 * t_idx + 1) < attrib.texcoords.size()) {
        model->texcoords.push_back(attrib.texcoords[2 * t_idx + 0]);
        model->texcoords.push_back(attrib.texcoords[2 * t_idx + 1]);

        // Copy to UV1
        model->texcoords1.push_back(attrib.texcoords[2 * t_idx + 0]);
        model->texcoords1.push_back(attrib.texcoords[2 * t_idx + 1]);
      } else {
        model->texcoords.push_back(0);
        model->texcoords.push_back(0);

        model->texcoords1.push_back(0);
        model->texcoords1.push_back(0);
      }
    }
    submesh.count = (model->positions.size() / 3) - submesh.first;
    model->meshes.push_back(submesh);
  }

  model->numVertex = model->positions.size() / 3;
  // If no meshes created (empty), create one dummy?
  if (model->meshes.empty() && model->numVertex > 0) {
    model->meshes.push_back({0, model->numVertex, 0});
  }

  std::cout << "  Loaded " << model->numVertex << " vertices in " << model->meshes.size() << " sub-meshes."
            << std::endl;
  return model;
}

Model* Model::loadSplitGLB(const std::string& filename, const std::vector<std::string>& splitNames,
                           std::vector<Model*>& outModels, std::vector<glm::mat4>& outTransforms) {
  tinygltf::Model gltfModel;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);

  if (!warn.empty()) {
    std::cout << "TinyGLTF Warning: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << "TinyGLTF Error: " << err << std::endl;
  }

  if (!ret) {
    return nullptr;
  }

  Model* mainModel = new Model();
  std::vector<GLuint> sharedTextures;

  // 1. Process Textures (Shared)
  for (const auto& img : gltfModel.images) {
    GLuint texID = 0;
    if (!img.image.empty()) {
      texID = createTextureFromData((unsigned char*)img.image.data(), img.width, img.height, img.component);
      if (texID != 0) {
        glBindTexture(GL_TEXTURE_2D, texID);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
      }
    }
    sharedTextures.push_back(texID);
  }
  mainModel->textures = sharedTextures;

  // Resize Outputs
  outModels.resize(splitNames.size(), nullptr);
  outTransforms.resize(splitNames.size(), glm::mat4(1.0f));

  // 2. Traverse Nodes (DFS)
  struct NodeState {
    int nodeIdx;
    glm::mat4 parentGlobalTransform;
    Model* targetModel;
    glm::mat4 localAccumulatedTransform;  // Relative to the split root (or global if main)
  };
  std::vector<NodeState> nodeStack;

  const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
  for (int nodeIdx : scene.nodes) {
    nodeStack.push_back({nodeIdx, glm::mat4(1.0f), mainModel, glm::mat4(1.0f)});
  }

  while (!nodeStack.empty()) {
    NodeState state = nodeStack.back();
    nodeStack.pop_back();

    if (state.nodeIdx < 0 || state.nodeIdx >= (int)gltfModel.nodes.size()) continue;
    const tinygltf::Node& node = gltfModel.nodes[state.nodeIdx];

    // Compute Transforms
    glm::mat4 localTransform(1.0f);
    if (node.matrix.size() == 16) {
      localTransform = glm::make_mat4(node.matrix.data());
    } else {
      if (node.translation.size() == 3) {
        localTransform = glm::translate(
            localTransform,
            glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
      }
      if (node.rotation.size() == 4) {
        glm::quat q((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
        localTransform = localTransform * glm::mat4_cast(q);
      }
      if (node.scale.size() == 3) {
        localTransform =
            glm::scale(localTransform, glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
      }
    }

    glm::mat4 currentGlobalTransform = state.parentGlobalTransform * localTransform;

    // Determine Target Model and Transforms
    Model* currentTarget = state.targetModel;
    glm::mat4 transformForMesh = glm::mat4(1.0f);  // Transform to bake into vertices
    glm::mat4 nextLocalAcc = state.localAccumulatedTransform * localTransform;

    // Check Split
    bool isSplitRoot = false;
    for (size_t i = 0; i < splitNames.size(); ++i) {
      if (node.name == splitNames[i]) {
        // Found Split Node!
        Model* newModel = new Model();
        newModel->textures = sharedTextures;  // Copy textures
        outModels[i] = newModel;
        outTransforms[i] = currentGlobalTransform;  // Capture Global Position/Rot as Initial Transform

        currentTarget = newModel;
        nextLocalAcc = glm::mat4(1.0f);  // Reset local accumulation for new model
        isSplitRoot = true;
        break;
      }
    }

    if (currentTarget == mainModel) {
      transformForMesh = currentGlobalTransform;
    } else {
      // For split models, we bake the accumulating local transform
      // If this is the split root, nextLocalAcc was reset to Identity (or LocalT? No, Identity logic in next
      // iteration) Wait, 'nextLocalAcc' is for Children. For THIS mesh:
      if (isSplitRoot) {
        transformForMesh = glm::mat4(1.0f);  // Baked vertices should be local to Axle (Identity)
      } else {
        transformForMesh = state.localAccumulatedTransform * localTransform;
      }
    }

    // Process Mesh
    if (node.mesh >= 0 && node.mesh < (int)gltfModel.meshes.size()) {
      const tinygltf::Mesh& mesh = gltfModel.meshes[node.mesh];
      for (const auto& prim : mesh.primitives) {
        // --- Extract Attributes (Same as fromGLBFile) ---
        // Extract POSITION
        if (prim.attributes.find("POSITION") == prim.attributes.end()) continue;

        const tinygltf::Accessor& posAcc = gltfModel.accessors[prim.attributes.at("POSITION")];
        const tinygltf::BufferView& posView = gltfModel.bufferViews[posAcc.bufferView];
        const tinygltf::Buffer& posBuf = gltfModel.buffers[posView.buffer];
        const unsigned char* posData = posBuf.data.data() + posView.byteOffset + posAcc.byteOffset;
        int posStride = posAcc.ByteStride(posView);
        if (posStride == 0) {
          posStride =
              tinygltf::GetComponentSizeInBytes(posAcc.componentType) * tinygltf::GetNumComponentsInType(posAcc.type);
        }

        const unsigned char* normData = nullptr;
        int normStride = 0;
        if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("NORMAL")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          normData = buf.data.data() + view.byteOffset + acc.byteOffset;
          normStride = acc.ByteStride(view);
          if (normStride == 0) {
            normStride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        const unsigned char* uvData = nullptr;
        int uvStride = 0;
        if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          uvData = buf.data.data() + view.byteOffset + acc.byteOffset;
          uvStride = acc.ByteStride(view);
          if (uvStride == 0) {
            uvStride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        const unsigned char* uv1Data = nullptr;
        int uv1Stride = 0;
        if (prim.attributes.find("TEXCOORD_1") != prim.attributes.end()) {
          const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_1")];
          const tinygltf::BufferView& view = gltfModel.bufferViews[acc.bufferView];
          const tinygltf::Buffer& buf = gltfModel.buffers[view.buffer];
          uv1Data = buf.data.data() + view.byteOffset + acc.byteOffset;
          uv1Stride = acc.ByteStride(view);
          if (uv1Stride == 0) {
            uv1Stride =
                tinygltf::GetComponentSizeInBytes(acc.componentType) * tinygltf::GetNumComponentsInType(acc.type);
          }
        }

        auto getVec3 = [&](const unsigned char* ptr, int stride, int index, int componentType) {
          const unsigned char* src = ptr + index * stride;
          glm::vec3 ret(0.0f);
          for (int c = 0; c < 3; c++) {
            float val = 0.0f;
            if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
              val = ((const float*)src)[c];
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              val = ((const unsigned short*)src)[c] / 65535.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              val = ((const unsigned char*)src)[c] / 255.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
              val = std::max(((const short*)src)[c] / 32767.0f, -1.0f);
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
              val = std::max(((const char*)src)[c] / 127.0f, -1.0f);
            }
            ret[c] = val;
          }
          return ret;
        };
        auto getVec2 = [&](const unsigned char* ptr, int stride, int index, int componentType) {
          const unsigned char* src = ptr + index * stride;
          glm::vec2 ret(0.0f);
          for (int c = 0; c < 2; c++) {
            float val = 0.0f;
            if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
              val = ((const float*)src)[c];
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              val = ((const unsigned short*)src)[c] / 65535.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              val = ((const unsigned char*)src)[c] / 255.0f;
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
              val = std::max(((const short*)src)[c] / 32767.0f, -1.0f);
            } else if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
              val = std::max(((const char*)src)[c] / 127.0f, -1.0f);
            }
            ret[c] = val;
          }
          return ret;
        };

        std::vector<uint32_t> indices;
        if (prim.indices >= 0) {
          const tinygltf::Accessor& idxAcc = gltfModel.accessors[prim.indices];
          const tinygltf::BufferView& idxView = gltfModel.bufferViews[idxAcc.bufferView];
          const tinygltf::Buffer& idxBuf = gltfModel.buffers[idxView.buffer];
          const unsigned char* idxData = idxBuf.data.data() + idxView.byteOffset + idxAcc.byteOffset;
          int idxStride = idxAcc.ByteStride(idxView);
          for (size_t i = 0; i < idxAcc.count; i++) {
            if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              const unsigned short* p = (const unsigned short*)(idxData + i * (idxStride ? idxStride : 2));
              indices.push_back(*p);
            } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
              const unsigned int* p = (const unsigned int*)(idxData + i * (idxStride ? idxStride : 4));
              indices.push_back(*p);
            } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              const unsigned char* p = (const unsigned char*)(idxData + i * (idxStride ? idxStride : 1));
              indices.push_back(*p);
            }
          }
        } else {
          for (size_t i = 0; i < posAcc.count; i++) indices.push_back(i);
        }

        SubDraw submesh;
        submesh.first = currentTarget->positions.size() / 3;

        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(transformForMesh)));

        for (uint32_t i : indices) {
          glm::vec3 pos = getVec3(posData, posStride, i, posAcc.componentType);
          glm::vec4 worldPos = transformForMesh * glm::vec4(pos, 1.0f);
          currentTarget->positions.push_back(worldPos.x);
          currentTarget->positions.push_back(worldPos.y);
          currentTarget->positions.push_back(worldPos.z);

          if (normData) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("NORMAL")];
            glm::vec3 norm = getVec3(normData, normStride, i, acc.componentType);
            norm = glm::normalize(normalMatrix * norm);
            currentTarget->normals.push_back(norm.x);
            currentTarget->normals.push_back(norm.y);
            currentTarget->normals.push_back(norm.z);
          } else {
            currentTarget->normals.push_back(0);
            currentTarget->normals.push_back(1);
            currentTarget->normals.push_back(0);
          }

          if (uvData) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
            glm::vec2 uv = getVec2(uvData, uvStride, i, acc.componentType);
            currentTarget->texcoords.push_back(uv.x);
            currentTarget->texcoords.push_back(1.0f - uv.y);
          } else {
            currentTarget->texcoords.push_back(0);
            currentTarget->texcoords.push_back(0);
          }
          if (uv1Data) {
            const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_1")];
            glm::vec2 uv1 = getVec2(uv1Data, uv1Stride, i, acc.componentType);
            currentTarget->texcoords1.push_back(uv1.x);
            currentTarget->texcoords1.push_back(1.0f - uv1.y);
          } else {
            if (uvData) {
              const tinygltf::Accessor& acc = gltfModel.accessors[prim.attributes.at("TEXCOORD_0")];
              glm::vec2 uv = getVec2(uvData, uvStride, i, acc.componentType);
              currentTarget->texcoords1.push_back(uv.x);
              currentTarget->texcoords1.push_back(1.0f - uv.y);
            } else {
              currentTarget->texcoords1.push_back(0);
              currentTarget->texcoords1.push_back(0);
            }
          }
        }
        submesh.count = indices.size();

        // Material
        submesh.baseColorTexIndex = -1;
        submesh.ormTexIndex = -1;
        submesh.normalTexIndex = -1;
        submesh.emissiveTexIndex = -1;
        if (prim.material >= 0) {
          const tinygltf::Material& mat = gltfModel.materials[prim.material];
          const auto& pbr = mat.pbrMetallicRoughness;
          if (pbr.baseColorTexture.index >= 0)
            submesh.baseColorTexIndex = gltfModel.textures[pbr.baseColorTexture.index].source;
          if (pbr.baseColorFactor.size() == 4)
            submesh.baseColorFactor = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                                                pbr.baseColorFactor[3]);
          if (pbr.metallicRoughnessTexture.index >= 0)
            submesh.ormTexIndex = gltfModel.textures[pbr.metallicRoughnessTexture.index].source;
          submesh.roughnessFactor = (float)pbr.roughnessFactor;
          submesh.metallicFactor = (float)pbr.metallicFactor;
          if (mat.normalTexture.index >= 0) submesh.normalTexIndex = gltfModel.textures[mat.normalTexture.index].source;
          if (mat.emissiveTexture.index >= 0)
            submesh.emissiveTexIndex = gltfModel.textures[mat.emissiveTexture.index].source;
          if (mat.emissiveFactor.size() == 3)
            submesh.emissiveFactor = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);
        }
        currentTarget->meshes.push_back(submesh);
      }
    }

    // Push Children
    for (int childIdx : node.children) {
      nodeStack.push_back({childIdx, currentGlobalTransform, currentTarget, nextLocalAcc});
    }
  }

  mainModel->numVertex = mainModel->positions.size() / 3;

  // Finalize Split Models (Compute numVertex and Center Mesh)
  for (size_t i = 0; i < outModels.size(); i++) {
    Model* m = outModels[i];
    if (m) {
      m->numVertex = m->positions.size() / 3;

      // [NEW] Auto-Center the mesh to fix wheel wobbling
      // 1. Calculate Bounding Box
      if (m->positions.empty()) continue;

      glm::vec3 minBounds(1e9f), maxBounds(-1e9f);
      for (size_t v = 0; v < m->positions.size(); v += 3) {
        glm::vec3 p(m->positions[v], m->positions[v + 1], m->positions[v + 2]);
        minBounds = glm::min(minBounds, p);
        maxBounds = glm::max(maxBounds, p);
      }

      glm::vec3 center = (minBounds + maxBounds) * 0.5f;

      // 2. Shift vertices to center (Visual Center becomes 0,0,0)
      for (size_t v = 0; v < m->positions.size(); v += 3) {
        m->positions[v] -= center.x;
        m->positions[v + 1] -= center.y;
        m->positions[v + 2] -= center.z;
      }

      // 3. Compensate Matrix (Move Model Origin to where the visual center was)
      // NewMatrix = OldMatrix * Translate(Center)
      outTransforms[i] = outTransforms[i] * glm::translate(glm::mat4(1.0f), center);

      std::cout << "  Auto-Centered Split Model " << i << ": Offset " << center.x << ", " << center.y << ", "
                << center.z << std::endl;
    }
  }

  std::cout << "Loaded Split GLB: " << filename << std::endl;
  return mainModel;
}
