#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include "gl_helper.h"

class Context;

class Program {
 public:
  const char* vertProgramFile;
  const char* fragProgramFIle;

 public:
  Program(Context* ctx) : ctx(ctx) {
    vertProgramFile = "assets/shaders/example.vert";
    fragProgramFIle = "assets/shaders/example.frag";
  }

  virtual bool load() = 0;
  virtual void doMainLoop() = 0;
  GLuint getProgramId() const { return programId; }

 protected:
  GLuint programId = -1;
  Context* ctx;
  // 基底類別的 VAO 指標，有些子類別當陣列用(Geometry)，有些當單一變數用
  GLuint* VAO = 0;
};

class ExampleProgram : public Program {
 public:
  ExampleProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/example.vert";
    fragProgramFIle = "assets/shaders/example.frag";
  }
  bool load() override;
  void doMainLoop() override;
};

// [新加入] Geometry Pass
// 這個類別接手了原本 LightProgram "畫模型" 的工作
class GeometryProgram : public Program {
 public:
  GeometryProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/geometry.vert";  // 寫入 G-Buffer 的 Shader
    fragProgramFIle = "assets/shaders/geometry.frag";
  }
  bool load() override;
  void doMainLoop() override;

  // 這裡需要儲存所有模型的 VAO (陣列)
  // 繼承自基底類別的 GLuint* VAO 即可，或者您也可以在這裡明確宣告
};

// [修改] Lighting Pass
// 現在它只負責畫一個全螢幕四邊形，並計算 PBR 光照
class LightProgram : public Program {
 public:
  LightProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/quad.vert";               // 簡單的 Pass-through vertex shader
    fragProgramFIle = "assets/shaders/lighting_deferred.frag";  // 讀取 G-Buffer 的 PBR shader
  }
  bool load() override;
  void doMainLoop() override;

  // 用於繪製全螢幕四邊形的 VAO/VBO
  GLuint VAO_Quad = 0;
  GLuint VBO_Quad = 0;
};

class SkyboxProgram : public Program {
 public:
  SkyboxProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/skybox.vert";
    fragProgramFIle = "assets/shaders/skybox.frag";
  }
  bool load() override;
  void doMainLoop() override;
};

class ShadowProgram : public Program {
 public:
  ShadowProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/shadow.vert";
    fragProgramFIle = "assets/shaders/shadow.frag";
  }
  bool load() override;
  void doMainLoop() override;
  GLuint* VAO = nullptr;
};

class RainProgram : public Program {
 public:
  RainProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/rain.vert";
    fragProgramFIle = "assets/shaders/rain.frag";
  }
  bool load() override;
  void doMainLoop() override;

  int maxParticles = 4000;
  float* particlePositions = nullptr;  // x, y, z per particle
  bool enableEmission = true;
};

struct Particle {
  glm::vec3 position;
  glm::vec3 velocity;
  float life;  // 1.0 to 0.0
  float size;
  float rotation;
  float maxLife;
};

class SmokeProgram : public Program {
 public:
  SmokeProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/smoke.vert";
    fragProgramFIle = "assets/shaders/smoke.frag";
  }
  bool load() override;
  void doMainLoop() override;

  std::vector<Particle> particles;
  GLuint textureId = 0;
};

class MiniMapProgram : public Program {
 public:
  MiniMapProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/solid.vert";
    fragProgramFIle = "assets/shaders/solid.frag";
  }
  bool load() override;
  void doMainLoop() override;

  GLuint trackVAO = 0;
  int trackVertexCount = 0;
  GLuint pointVAO = 0;  // [NEW] For Red Dot

  // Track Bounds for Ortho
  float minX = -100, maxX = 100, minY = -100, maxY = 100, minZ = -100, maxZ = 100;
};

// [NEW] Gaussian Blur Program
class BlurProgram : public Program {
 public:
  BlurProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/blur.vert";
    fragProgramFIle = "assets/shaders/blur.frag";
  }
  bool load() override;
  void doMainLoop() override;
  // We need a custom doMainLoop that takes params, or just use context variables
  // Standard doMainLoop() doesn't take args. We'll set uniforms via Context or methods.
  // Actually, we can just call glUseProgram and set uniforms manually in main loop too.
  // But let's stick to the pattern.

  GLuint VAO_Quad = 0;
};

// [NEW] Final Bloom Combine Program
class BloomFinalProgram : public Program {
 public:
  BloomFinalProgram(Context* ctx) : Program(ctx) {
    vertProgramFile = "assets/shaders/bloom_final.vert";
    fragProgramFIle = "assets/shaders/bloom_final.frag";
  }
  bool load() override;
  void doMainLoop() override;

  GLuint VAO_Quad = 0;
};