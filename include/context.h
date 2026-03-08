#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include "camera.h"
#include "model.h"
#include "program.h"

// Global varaibles share between main.cpp and shader programs
class Context {
 public:
  // Index of program id in Context::programs vector
  // which is currently use to render all objects
  int currentProgram = 0;

 public:
  std::vector<Program*> programs;
  std::vector<Model*> models;
  std::vector<Object*> objects;

 public:
  // Parameter of lights use in light shader
  int directionLightEnable = 1;
  glm::vec3 directionLightDirection = glm::vec3(-0.3f, -0.5f, -0.2f);
  glm::vec3 directionLightColor = glm::vec3(0.6f, 0.6f, 0.6f);

  // Multi-Point Lights
  int pointLightEnable[6] = {0, 0, 0, 0, 0, 0};
  glm::vec3 pointLightPosition[6];
  glm::vec3 pointLightColor[6];
  float pointLightConstant[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  float pointLightLinear[6] = {0.09f, 0.09f, 0.09f, 0.09f, 0.09f, 0.09f};
  float pointLightQuadratic[6] = {0.032f, 0.032f, 0.032f, 0.032f, 0.032f, 0.032f};

  static const int MAX_SPOT_LIGHTS = 32;
  int spotLightEnable[MAX_SPOT_LIGHTS] = {0};
  glm::vec3 spotLightPosition[MAX_SPOT_LIGHTS];
  glm::vec3 spotLightDirection[MAX_SPOT_LIGHTS];
  glm::vec3 spotLightColor[MAX_SPOT_LIGHTS];
  float spotLightCutOff[MAX_SPOT_LIGHTS];
  float spotLightConstant[MAX_SPOT_LIGHTS];
  float spotLightLinear[MAX_SPOT_LIGHTS];
  float spotLightQuardratic[MAX_SPOT_LIGHTS];

 public:
  Camera* camera = 0;
  GLFWwindow* window = 0;
  unsigned int cubemapTexture = 0;

 public:
  GLuint depthMapFBO = 0;
  GLuint depthMapTexture = 0;
  const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
  glm::mat4 lightSpaceMatrix;

  // --- [新增] Deferred Rendering G-Buffer Resources ---
 public:
  GLuint gBuffer = 0;      // G-Buffer Framebuffer Object
  GLuint gPosition = 0;    // Texture: World Position (RGB)
  GLuint gNormal = 0;      // Texture: World Normal (RGB)
  GLuint gAlbedoSpec = 0;  // Texture: Albedo (RGB) + Metallic (A)
  GLuint gPBRParams = 0;   // Texture: Roughness (R) + AO (G)
  GLuint rboDepth = 0;     // Renderbuffer: Depth & Stencil (for depth testing)

  // [新增] 用來存賽道的粗糙度貼圖 ID
  GLuint trackRoughnessTexture = 0;

  // --- [新增] Optimization: Bloom Buffers ---
  unsigned int hdrFBO = 0;
  unsigned int colorBuffers[2] = {0, 0};
  unsigned int rboDepthHDR =
      0;  // Shared depth or separate? Usually shared if blitting, or new if drawing fresh. Forward pass needs depth.

  unsigned int pingpongFBO[2] = {0, 0};
  unsigned int pingpongColorbuffers[2] = {0, 0};

  // Helper flags
  bool bloom = true;
  float exposure = 1.0f;

  GLuint defaultWhiteTexture = 0;  // [NEW] Default 1x1 white texture
};