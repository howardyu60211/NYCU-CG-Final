#include <algorithm>
#include <iostream>
#include <vector>

#include <GLFW/glfw3.h>
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#undef GLAD_GL_IMPLEMENTATION
#include <glm/glm.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include "camera.h"
#include "car.h"
#include "context.h"
#include "gl_helper.h"
#include "model.h"
#include "opengl_context.h"
#include "program.h"

// Globals
Context ctx;
InputState inputState;
Car* car = nullptr;
bool useFPP = false;
std::vector<glm::mat4> g_carWheelOffsets;  // [NEW] Initial Wheel Offsets from GLB
bool useFreeCam = false;                   // [NEW] Free Camera Toggle
bool enableRain = true;                    // [NEW] Rain Toggle

void buildTrackGrid();
void initOpenGL();
void initGBuffer();  // [新增]
void resizeCallback(GLFWwindow* window, int width, int height);
void keyCallback(GLFWwindow* window, int key, int, int action, int);

// --- Game State & Traffic Lights ---
enum GameState { PRE_START, COUNTDOWN, READY, RACING };
GameState currentGameState = PRE_START;
float stateTimer = 0.0f;
int lightsOnCount = 0;
float readyDelay = 0.0f;  // Random delay for READY -> RACING

// [NEW] Jump Start Penalty State
bool jumpStartPenalty = false;
float penaltyTimer = 0.0f;

// --- Lap Counting ---
int currentLap = 0;
bool passedCheckpoint = true;
float currentLapTime = 0.0f;
float lastLapTime = 0.0f;
float bestLapTime = 9999.0f;  // High default

// [NEW] Sector Timing
// State: 0=None, 1=Yellow (Slower/NoRecord), 2=Green (PB)
float curS1 = 0.0f, curS2 = 0.0f, curS3 = 0.0f;
float bestS1 = 9999.0f, bestS2 = 9999.0f, bestS3 = 9999.0f;
int s1State = 0, s2State = 0, s3State = 0;
bool passedS1 = false;
bool passedS2 = false;
float lapTimeAtS1 = 0.0f;  // To calc diff
float lapTimeAtS2 = 0.0f;

float randomFloat(float min, float max) {
  return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}

// Material instances
// Material mAsphalt;
Material mCarPaint;

// Helper to create a 1x1 white texture
GLuint createDefaultTexture() {
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  unsigned char white[] = {255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return texture;
}

void loadMaterial() {
  // PBR Materials
  // PBR Materials
  mCarPaint.diffuse = glm::vec3(0.7f, 0.7f, 0.7f);  // Base Diffuse
  // [Updated] Reduced shine to make texture visible
  mCarPaint.metallic = 0.0f;   // Non-metallic (Paint)
  mCarPaint.roughness = 0.5f;  // Semi-gloss, not mirror
  // 稍微加強 AO 讓縫隙暗一點
  mCarPaint.ao = 1.0f;
}

void loadPrograms() {
  // 1. Shadow (Depth Map)
  ctx.programs.push_back(new ShadowProgram(&ctx));

  // 2. Geometry Pass (G-Buffer)
  ctx.programs.push_back(new GeometryProgram(&ctx));

  // 3. Lighting Pass (Deferred)
  ctx.programs.push_back(new LightProgram(&ctx));

  // 4. Forward Pass (Transparent / Skybox)
  ctx.programs.push_back(new SkyboxProgram(&ctx));
  ctx.programs.push_back(new RainProgram(&ctx));
  ctx.programs.push_back(new SmokeProgram(&ctx));
  // 5. MiniMap (UI)
  ctx.programs.push_back(new MiniMapProgram(&ctx));
  ctx.programs.push_back(new BlurProgram(&ctx));
  ctx.programs.push_back(new BloomFinalProgram(&ctx));

  for (auto iter = ctx.programs.begin(); iter != ctx.programs.end(); iter++) {
    if (!(*iter)->load()) {
      std::cout << "Load program fail, force terminate" << std::endl;
      exit(1);
    }
  }
  glUseProgram(0);
}

// --- F1 HUD Helper Functions (保持原本的 HUD 代碼) ---
// (為了簡潔，這裡省略 drawRect, drawDigit, renderF1HUD 的實作細節，
//  請直接貼上您原本 main.cpp 裡的那此函數，它們不需要修改)
// 1. 基礎矩形繪製 (保持不變)
void drawRect(int x, int y, int w, int h, glm::vec3 color) {
  glScissor(x, y, w, h);
  glClearColor(color.r, color.g, color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

// 2. [核心修復] 七段顯示器數字繪製 (細緻風格)
// 這裡修復了原本代碼中錯亂的座標計算，讓 '2' 能正確顯示。
/*
      A (0)
     ---
  F |   | B (1)
  (5)--- G (6)
  E |   | C (2)
  (4)---
      D (3)
*/
void drawDigit(int x, int y, int size, int number, glm::vec3 c = glm::vec3(1.0f)) {
  // [風格調整] 回復到較細的線條
  int thickness = size / 6;
  if (thickness < 1) thickness = 1;  // 確保至少有 1 像素寬

  int h = size * 2;  // 高度
  // glm::vec3 c is now a parameter

  if (number < 0 || number > 9) return;

  // 標準七段顯示器定義 (A, B, C, D, E, F, G)
  bool seg[10][7] = {
      {1, 1, 1, 1, 1, 1, 0},  // 0
      {0, 1, 1, 0, 0, 0, 0},  // 1
      {1, 1, 0, 1, 1, 0, 1},  // 2 (修復重點)
      {1, 1, 1, 1, 0, 0, 1},  // 3
      {0, 1, 1, 0, 0, 1, 1},  // 4
      {1, 0, 1, 1, 0, 1, 1},  // 5
      {1, 0, 1, 1, 1, 1, 1},  // 6
      {1, 1, 1, 0, 0, 0, 0},  // 7
      {1, 1, 1, 1, 1, 1, 1},  // 8
      {1, 1, 1, 1, 0, 1, 1}   // 9
  };

  // --- 繪製邏輯 (修復了座標計算，並允許角落輕微重疊以保持平滑) ---

  // A: 上方橫條
  if (seg[number][0]) drawRect(x, y + h - thickness, size, thickness, c);

  // B: 右上直條
  // 注意：y 座標從中間開始往上畫到頂
  if (seg[number][1]) drawRect(x + size - thickness, y + h / 2, thickness, h / 2, c);

  // C: 右下直條
  // 注意：y 座標從底部開始往上畫到中間
  if (seg[number][2]) drawRect(x + size - thickness, y, thickness, h / 2, c);

  // D: 下方橫條
  if (seg[number][3]) drawRect(x, y, size, thickness, c);

  // E: 左下直條
  if (seg[number][4]) drawRect(x, y, thickness, h / 2, c);

  // F: 左上直條
  if (seg[number][5]) drawRect(x, y + h / 2, thickness, h / 2, c);

  // G: 中間橫條
  // 稍微調整 Y 讓它居中
  if (seg[number][6]) drawRect(x, y + h / 2 - thickness / 2, size, thickness, c);
}

// 3. 儀表板渲染 (保持佈局優化，應用新的細緻數字)
void renderF1HUD(GLFWwindow* window, float speed, const InputState& input, bool isFPP, float penaltyTimer = 0.0f) {
  int winW, winH;
  glfwGetFramebufferSize(window, &winW, &winH);
  glEnable(GL_SCISSOR_TEST);

  // Dynamic Scale: Based on height, reference 1000px.
  // Clamp: Don't scale UP (keep current size max), only scale DOWN for small screens.
  float baseScale = (float)winH / 1000.0f;
  if (baseScale > 1.0f) baseScale = 1.0f;
  if (baseScale < 0.5f) baseScale = 0.5f;

  auto S = [&](int val) -> int { return (int)(val * baseScale); };

  int panelW = S(420);
  int panelH = S(130);
  int panelX, panelY;

  if (isFPP) {
    panelX = (winW - panelW) / 2;
    panelY = S(20);
  } else {
    panelX = winW - panelW - S(40);
    panelY = S(40);
  }

  // 背景與邊框
  int border = S(4);
  glm::vec3 bgColor(0.02f, 0.02f, 0.05f);  // Original Dark Blue

  // [NEW] Penalty Flash Logic
  if (penaltyTimer > 0.0f) {
    // Flash Red (2 times per second)
    float flash = std::sin(glfwGetTime() * 15.0f);
    if (flash > 0.0f) {
      bgColor = glm::vec3(0.6f, 0.0f, 0.0f);  // Bright Red
    }
  }

  drawRect(panelX - border, panelY - border, panelW + border * 2, panelH + border * 2, glm::vec3(0.2f, 0.2f, 0.25f));
  drawRect(panelX, panelY, panelW, panelH, bgColor);

  // 轉速條 (RPM Bar) - 保持彩色樣式
  float physicsMaxSpeed = 50.0f;  // [Modified] Matched to new car max speed
  float rpmRatio = std::abs(speed) / physicsMaxSpeed;
  if (rpmRatio > 1.0f) rpmRatio = 1.0f;

  int numLeds = 15;
  int ledPadding = S(4);
  int ledW = (panelW - S(20)) / numLeds - ledPadding;
  int ledH = S(15);
  int barStartX = panelX + S(10);
  int barStartY = panelY + panelH - S(25);

  for (int i = 0; i < numLeds; i++) {
    float threshold = (float)(i + 1) / numLeds;
    glm::vec3 color(0.1f, 0.1f, 0.1f);
    if (rpmRatio >= threshold - (1.0f / numLeds)) {
      if (i < numLeds * 0.4)
        color = glm::vec3(0.0f, 0.8f, 0.0f);
      else if (i < numLeds * 0.7)
        color = glm::vec3(0.8f, 0.0f, 0.0f);
      else
        color = glm::vec3(0.0f, 0.4f, 1.0f);
    }
    drawRect(barStartX + i * (ledW + ledPadding), barStartY, ledW, ledH, color);
  }

  // 速度顯示 (左側)
  int displaySpeed = (int)std::abs(speed * 5.f);
  if (displaySpeed > 999) displaySpeed = 999;

  int digitSize = S(18);
  int digitSpacing = S(28);
  int speedStartX = panelX + S(30);
  int dataY = panelY + S(22);

  if (displaySpeed >= 100) drawDigit(speedStartX, dataY, digitSize, displaySpeed / 100);
  if (displaySpeed >= 10)
    drawDigit(speedStartX + digitSpacing, dataY, digitSize, (displaySpeed / 10) % 10);
  else if (displaySpeed >= 100)
    drawDigit(speedStartX + digitSpacing, dataY, digitSize, 0);
  drawDigit(speedStartX + digitSpacing * 2, dataY, digitSize, displaySpeed % 10);

  // "KMH" 標籤 (保持細線風格)
  int charX = speedStartX + digitSpacing * 3 + S(5);
  int charY = dataY;
  glm::vec3 txtCol(0.7f, 0.7f, 0.7f);
  int cw = S(2);       // 線條寬度
  if (cw < 2) cw = 2;  // Keep legible

  // K
  drawRect(charX, charY, cw, S(10), txtCol);
  drawRect(charX + cw, charY + S(4), cw, S(2), txtCol);  // Tweaked offsets
  drawRect(charX + cw * 2, charY + S(6), cw, S(2), txtCol);
  drawRect(charX + cw * 3, charY + S(8), cw, S(2), txtCol);
  drawRect(charX + cw * 2, charY + S(2), cw, S(2), txtCol);
  drawRect(charX + cw * 3, charY, cw, S(2), txtCol);
  // M
  charX += S(10) + cw;  // Dynamic spacing
  drawRect(charX, charY, cw, S(10), txtCol);
  drawRect(charX + S(8), charY, cw, S(10), txtCol);
  drawRect(charX + cw, charY + S(8), cw, S(2), txtCol);
  drawRect(charX + cw * 2, charY + S(6), cw, S(2), txtCol);
  drawRect(charX + cw * 3, charY + S(8), cw, S(2), txtCol);
  // H
  charX += S(12) + cw;
  drawRect(charX, charY, cw, S(10), txtCol);
  drawRect(charX + S(8), charY, cw, S(10), txtCol);
  drawRect(charX, charY + S(4), S(10), cw, txtCol);

  // 檔位顯示 (中央大數字)
  int gear = (int)(std::abs(speed) / 7.5f) + 1;
  if (gear > 8) gear = 8;
  if (std::abs(speed) < 0.5f) gear = 0;
  drawDigit(panelX + S(190), dataY - S(5), S(35), gear);

  // 圈數 (右側)
  int lapX = panelX + S(260);
  // "LAP" 標籤
  drawRect(lapX, charY, cw, S(10), txtCol);  // L
  drawRect(lapX, charY, S(8), cw, txtCol);
  lapX += S(12);
  drawRect(lapX, charY, cw, S(10), txtCol);  // A
  drawRect(lapX + S(8), charY, cw, S(10), txtCol);
  drawRect(lapX + cw, charY + S(8), S(6), cw, txtCol);
  drawRect(lapX + cw, charY + S(4), S(6), cw, txtCol);
  lapX += S(12);
  drawRect(lapX, charY, cw, S(10), txtCol);  // P
  drawRect(lapX + cw, charY + S(8), S(6), cw, txtCol);
  drawRect(lapX + cw, charY + S(4), S(6), cw, txtCol);
  drawRect(lapX + S(8), charY + S(4), cw, S(6), txtCol);

  // 圈數數字
  extern int currentLap;
  drawDigit(lapX + S(20), dataY, digitSize, currentLap);

  // 油門與煞車量表 (最右側)
  int pedalW = S(12);
  int pedalH = S(60);
  int pedalX = panelX + S(360);
  int pedalY = panelY + S(20);

  // Throttle Bar with Analog Support
  float throttleVal = input.throttleValue;
  if (throttleVal < 0.01f && input.up) throttleVal = 1.0f;  // Keyboard Fallback

  drawRect(pedalX, pedalY, pedalW, pedalH, glm::vec3(0.2f, 0.2f, 0.2f));
  if (throttleVal > 0) drawRect(pedalX, pedalY, pedalW, (int)(pedalH * throttleVal), glm::vec3(0.0f, 0.8f, 0.0f));

  // Brake Bar with Analog Support
  float brakeVal = input.brakeValue;
  if (brakeVal < 0.01f && (input.down || input.brake)) brakeVal = 1.0f;  // Keyboard Fallback

  drawRect(pedalX + S(18), pedalY, pedalW, pedalH, glm::vec3(0.2f, 0.2f, 0.2f));
  if (brakeVal > 0) drawRect(pedalX + S(18), pedalY, pedalW, (int)(pedalH * brakeVal), glm::vec3(0.8f, 0.0f, 0.0f));

  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

void renderTrafficLights(int lightsOn) {
  int winW, winH;
  glfwGetFramebufferSize(ctx.window, &winW, &winH);
  glEnable(GL_SCISSOR_TEST);

  int panelW = 350;
  int panelH = 100;
  int panelX = (winW - panelW) / 2;
  int panelY = winH - panelH - 20;  // Top center

  // Background Panel
  int border = 4;
  drawRect(panelX - border, panelY - border, panelW + border * 2, panelH + border * 2, glm::vec3(0.1f, 0.1f, 0.1f));
  drawRect(panelX, panelY, panelW, panelH, glm::vec3(0.0f, 0.0f, 0.0f));

  // Lights
  int lightSize = 50;
  int spacing = 15;
  int startX = panelX + (panelW - (5 * lightSize + 4 * spacing)) / 2;
  int startY = panelY + (panelH - lightSize) / 2;

  for (int i = 0; i < 5; i++) {
    glm::vec3 color(0.1f, 0.0f, 0.0f);  // Off (Dark Red)
    if (lightsOn > i) {
      color = glm::vec3(1.0f, 0.0f, 0.0f);  // On (Bright Red)
    }

    // Draw "Circle" (Approximated by low-res rect for now, or just rect)
    // Since we only have drawRect, we use rect.
    drawRect(startX + i * (lightSize + spacing), startY, lightSize, lightSize, color);
  }

  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

void loadModels() {
  Model* trackModel = Model::fromGLBFile("assets/models/track_mlb/source/track.glb");
  if (!trackModel) exit(1);
  ctx.models.push_back(trackModel);

  // [NEW] Load External PBR Textures for Track
  GLuint trackRoughness = createTexture("assets/models/track_mlb/Roughness_Map.png");
  GLuint trackNormal = createTexture("assets/models/track_mlb/Normal_Map.png");

  // Add to model's texture list
  int roughIndex = -1;
  int normIndex = -1;

  if (trackRoughness != 0) {
    trackModel->textures.push_back(trackRoughness);
    roughIndex = trackModel->textures.size() - 1;
  }
  if (trackNormal != 0) {
    trackModel->textures.push_back(trackNormal);
    normIndex = trackModel->textures.size() - 1;
  }

  // Assign to all meshes
  for (auto& mesh : trackModel->meshes) {
    // [FIX] Only apply to meshes that already have an ORM map (Likely Road)
    if (mesh.ormTexIndex != -1) {
      // Set Textures
      if (roughIndex != -1) mesh.ormTexIndex = roughIndex;
      if (normIndex != -1) mesh.normalTexIndex = normIndex;

      // Set Factors (Ensure they allow the texture to show through)
      mesh.roughnessFactor = 1.0f;
      mesh.metallicFactor = 0.0f;  // Road is non-metallic
    }
  }

  // [Updated] Load Car from GLB with Split Wheels
  std::vector<std::string> wheelNames = {"Wheel_FL", "Wheel_FR", "Wheel_BL", "Wheel_BR"};
  std::vector<Model*> wheelModels;

  Model* carModel = Model::loadSplitGLB("assets/models/car/car.glb", wheelNames, wheelModels, g_carWheelOffsets);

  if (!carModel) exit(1);
  ctx.models.push_back(carModel);  // Index 1: Body

  for (Model* wm : wheelModels) {
    if (wm)
      ctx.models.push_back(wm);  // Indices 2, 3, 4, 5
    else {
      std::cerr << "Failed to load wheel model!" << std::endl;
      exit(1);
    }
  }

  // [NEW] Reuse track model for road physics (since track_mlb is a single file)
  Model* roadModel = trackModel;
  if (!roadModel) {
    std::cout << "Warning: Road model not found!" << std::endl;
  } else {
    ctx.models.push_back(roadModel);  // Index 6
  }

  // [NEW] Brake Light Model (Cube)
  Model* brakeLightModel = Model::createCube();
  if (brakeLightModel) {
    ctx.models.push_back(brakeLightModel);
  }

  buildTrackGrid();
}

// [新增] 初始化 G-Buffer
void initGBuffer() {
  int SCR_WIDTH, SCR_HEIGHT;
  glfwGetFramebufferSize(ctx.window, &SCR_WIDTH, &SCR_HEIGHT);

  glGenFramebuffers(1, &ctx.gBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, ctx.gBuffer);

  // 1. Position (RGB32F for high precision)
  glGenTextures(1, &ctx.gPosition);
  glBindTexture(GL_TEXTURE_2D, ctx.gPosition);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx.gPosition, 0);

  // 2. Normal (RGB16F)
  glGenTextures(1, &ctx.gNormal);
  glBindTexture(GL_TEXTURE_2D, ctx.gNormal);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, ctx.gNormal, 0);

  // 3. Albedo + Spec/Metallic (RGBA8)
  glGenTextures(1, &ctx.gAlbedoSpec);
  glBindTexture(GL_TEXTURE_2D, ctx.gAlbedoSpec);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, ctx.gAlbedoSpec, 0);

  // 4. PBR Params (Roughness/AO)
  glGenTextures(1, &ctx.gPBRParams);
  glBindTexture(GL_TEXTURE_2D, ctx.gPBRParams);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, ctx.gPBRParams, 0);

  unsigned int attachments[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                 GL_COLOR_ATTACHMENT3};
  glDrawBuffers(4, attachments);

  // Depth Renderbuffer
  glGenRenderbuffers(1, &ctx.rboDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, ctx.rboDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx.rboDepth);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "Framebuffer not complete!" << std::endl;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // --- [NEW] HDR Framebuffer (Float) & PingPong Buffers ---
  glGenFramebuffers(1, &ctx.hdrFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);

  glGenTextures(2, ctx.colorBuffers);
  for (unsigned int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, ctx.colorBuffers[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // attach texture to framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, ctx.colorBuffers[i], 0);
  }

  // Use shared depth buffer from G-Buffer? No, we might write to it.
  // Actually, we copy depth from G-Buffer to default usually.
  // But now we render to HDR FBO. So we need a depth buffer there too.
  // We can share the same RBO/Texture if sizes match.
  // Let's create a dedicated one to be safe.
  glGenRenderbuffers(1, &ctx.rboDepthHDR);
  glBindRenderbuffer(GL_RENDERBUFFER, ctx.rboDepthHDR);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx.rboDepthHDR);

  unsigned int attachmentsHDR[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, attachmentsHDR);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "HDR Framebuffer not complete!" << std::endl;

  // PingPong
  glGenFramebuffers(2, ctx.pingpongFBO);
  glGenTextures(2, ctx.pingpongColorbuffers);
  for (unsigned int i = 0; i < 2; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.pingpongFBO[i]);
    glBindTexture(GL_TEXTURE_2D, ctx.pingpongColorbuffers[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);  // Clamp to prevent edge bleeding
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx.pingpongColorbuffers[i], 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      std::cout << "PingPong Framebuffer not complete!" << std::endl;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void setupObjects() {
  ctx.objects.clear();

  // 1. Road Model (Index 0) -> renaming to Track Base
  glm::mat4 roadTransform = glm::translate(glm::identity<glm::mat4>(), glm::vec3(0, 0.0f, 0));
  ctx.objects.push_back(new Object(0, roadTransform));
  // ctx.objects.back()->material = mAsphalt;
  ctx.objects.back()->material.roughness = 1.0f;  // Default matte
  ctx.objects.back()->textureIndex = -1;

  // 2. Car Body (Index 1)
  // [Updated] Start Position from User Terminal Log
  glm::vec3 startPos(87.7f, 7.4f, -173.5f);
  glm::mat4 carTransform = glm::translate(glm::identity<glm::mat4>(), startPos);

  // Create Main Body Object
  Object* carObj = new Object(1, carTransform);
  carObj->material = mCarPaint;
  carObj->textureIndex = ctx.models[1]->textures.size() > 0 ? (int)ctx.models[1]->textures[0] : -1;
  // Note: textures vector stores GLuints, but Object::textureIndex is int (index in Model::textures? No, GLuint)
  // Wait, Object::textureIndex is just an integer.
  // In LightProgram.cpp (presumably), it uses ctx.models[obj->modelIndex]->textures[obj->textureIndex]?
  // Or is textureIndex the raw GLuint?
  // Let's check Model::fromObjectFile. It sets submesh.baseColorTexIndex.
  // The 'Object' struct has 'textureIndex' which is often used as an override or if mesh has no texture.
  // In car.glb, textures are bound to meshes.
  // So we can set textureIndex = -1 to let the mesh decide.
  carObj->textureIndex = -1;
  ctx.objects.push_back(carObj);

  // 3. Wheel Objects (Indices 2..5)
  std::vector<Object*> wheelObjs;
  for (int i = 0; i < 4; ++i) {
    Object* wo = new Object(2 + i, carTransform);  // Initial transform same as car
    wo->material = mCarPaint;                      // Wheels probably have own materials in GLB, so let mesh decide
    wo->textureIndex = -1;
    ctx.objects.push_back(wo);
    wheelObjs.push_back(wo);
  }

  car = new Car(carObj);
  car->setPosition(startPos);
  car->setHeading(glm::radians(90.0f));
  car->setHeading(glm::radians(90.0f));
  car->setWheels(wheelObjs, g_carWheelOffsets);

  // [NEW] Setup Brake Light
  if (ctx.models.size() > 0) {           // Safety
    Model* blModel = ctx.models.back();  // Assuming we just added it last in loadModels
    // Verify it is the cube model? We rely on order.
    // Index should be size-1.
    int blIndex = ctx.models.size() - 1;
    Object* blObj = new Object(blIndex, carTransform);
    blObj->textureIndex = -1;
    ctx.objects.push_back(blObj);
    car->setBrakeLight(blObj, blModel);
  }

  // 4. New Road Surface (Index 6)
  if (ctx.models.size() > 6) {
    ctx.objects.push_back(new Object(6, roadTransform));
    // ctx.objects.back()->material = mAsphalt;
    ctx.objects.back()->material.roughness = 1.0f;  // Matte
    ctx.objects.back()->textureIndex = -1;
  }
}

void resizeGBuffer(int width, int height) {
  glBindFramebuffer(GL_FRAMEBUFFER, ctx.gBuffer);

  // 1. Position Texture
  glBindTexture(GL_TEXTURE_2D, ctx.gPosition);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, NULL);

  // 2. Normal Texture
  glBindTexture(GL_TEXTURE_2D, ctx.gNormal);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);

  // 3. Albedo + Spec/Metallic Texture
  glBindTexture(GL_TEXTURE_2D, ctx.gAlbedoSpec);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  // 4. PBR Params Texture
  glBindTexture(GL_TEXTURE_2D, ctx.gPBRParams);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

  // Depth Renderbuffer
  glBindRenderbuffer(GL_RENDERBUFFER, ctx.rboDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);

  // HDR FBO
  glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
  for (unsigned int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, ctx.colorBuffers[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
  }
  glBindRenderbuffer(GL_RENDERBUFFER, ctx.rboDepthHDR);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);

  // PingPong FBOs
  for (unsigned int i = 0; i < 2; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.pingpongFBO[i]);
    glBindTexture(GL_TEXTURE_2D, ctx.pingpongColorbuffers[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void resizeCallback(GLFWwindow* window, int width, int height) {
  glViewport(0, 0, width, height);
  if (ctx.camera) {
    ctx.camera->updateProjectionMatrix((float)width / (float)height);
  }
  resizeGBuffer(width, height);
}

float barycentric(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c, float* u, float* v, float* w) {
  glm::vec2 v0 = b - a, v1 = c - a, v2 = p - a;
  float d00 = dot(v0, v0);
  float d01 = dot(v0, v1);
  float d11 = dot(v1, v1);
  float d20 = dot(v2, v0);
  float d21 = dot(v2, v1);
  float denom = d00 * d11 - d01 * d01;
  if (abs(denom) < 1e-6f) return -1.0f;
  *v = (d11 * d20 - d01 * d21) / denom;
  *w = (d00 * d21 - d01 * d20) / denom;
  *u = 1.0f - *v - *w;
  return 1.0f;
}
int lastFoundTriangleIndex = -1;
struct UniformGrid {
  float minX, minZ, maxX, maxZ, cellSize;
  int cols, rows;
  std::vector<std::vector<int>> cells;
  void init(float min_x, float min_z, float max_x, float max_z, float size) {
    minX = min_x;
    minZ = min_z;
    maxX = max_x;
    maxZ = max_z;
    cellSize = size;
    cols = (int)std::ceil((maxX - minX) / cellSize);
    rows = (int)std::ceil((maxZ - minZ) / cellSize);
    if (cols > 2000) cols = 2000;
    if (rows > 2000) rows = 2000;
    cells.assign(cols * rows, {});
  }
  void insert(int triangleIndex, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
    float triMinX = std::min({p1.x, p2.x, p3.x});
    float triMaxX = std::max({p1.x, p2.x, p3.x});
    float triMinZ = std::min({p1.z, p2.z, p3.z});
    float triMaxZ = std::max({p1.z, p2.z, p3.z});
    int startCol = (int)((triMinX - minX) / cellSize);
    int endCol = (int)((triMaxX - minX) / cellSize);
    int startRow = (int)((triMinZ - minZ) / cellSize);
    int endRow = (int)((triMaxZ - minZ) / cellSize);
    startCol = std::max(0, std::min(startCol, cols - 1));
    endCol = std::max(0, std::min(endCol, cols - 1));
    startRow = std::max(0, std::min(startRow, rows - 1));
    endRow = std::max(0, std::min(endRow, rows - 1));
    for (int r = startRow; r <= endRow; r++) {
      for (int c = startCol; c <= endCol; c++) {
        cells[r * cols + c].push_back(triangleIndex);
      }
    }
  }
  const std::vector<int>& getCandidates(float x, float z) {
    int c = (int)((x - minX) / cellSize);
    int r = (int)((z - minZ) / cellSize);
    if (c >= 0 && c < cols && r >= 0 && r < rows) return cells[r * cols + c];
    static std::vector<int> empty;
    return empty;
  }
};
UniformGrid trackGrid;
void buildTrackGrid() {
  if (ctx.models.empty()) return;  // Ensure track model (index 0) is loaded
  Model* track = ctx.models[0];    // Use Track Model for physics
  int numTriangles = (int)track->positions.size() / 9;
  float minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9;
  for (size_t i = 0; i < track->positions.size(); i += 3) {
    float x = track->positions[i];
    float z = track->positions[i + 2];
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (z < minZ) minZ = z;
    if (z > maxZ) maxZ = z;
  }
  minX -= 10.0f;
  maxX += 10.0f;
  minZ -= 10.0f;
  maxZ += 10.0f;
  trackGrid.init(minX, minZ, maxX, maxZ, 5.0f);
  for (int i = 0; i < numTriangles; i++) {
    int base = i * 9;
    glm::vec3 p1(track->positions[base], track->positions[base + 1], track->positions[base + 2]);
    glm::vec3 p2(track->positions[base + 3], track->positions[base + 4], track->positions[base + 5]);
    glm::vec3 p3(track->positions[base + 6], track->positions[base + 7], track->positions[base + 8]);
    trackGrid.insert(i, p1, p2, p3);
  }
}
float getTrackHeight(float x, float z) {
  if (ctx.models.empty()) return -10000.0f;
  Model* track = ctx.models[0];  // Use Track Model for height check
  int numTriangles = (int)track->positions.size() / 9;
  glm::vec2 p(x, z);
  const float EPSILON = 1e-4f;
  float bestHeight = -10000.0f;
  float minDiff = 10000.0f;
  bool foundAny = false;
  float searchY = car ? car->getPosition().y : 0.0f;

  if (lastFoundTriangleIndex != -1 && lastFoundTriangleIndex < numTriangles) {
    int i = lastFoundTriangleIndex;
    int base = i * 9;
    glm::vec3 p1(track->positions[base], track->positions[base + 1], track->positions[base + 2]);
    glm::vec3 p2(track->positions[base + 3], track->positions[base + 4], track->positions[base + 5]);
    glm::vec3 p3(track->positions[base + 6], track->positions[base + 7], track->positions[base + 8]);
    float u, v, w;
    if (barycentric(p, glm::vec2(p1.x, p1.z), glm::vec2(p2.x, p2.z), glm::vec2(p3.x, p3.z), &u, &v, &w) > 0) {
      if (u >= -EPSILON && v >= -EPSILON && w >= -EPSILON) {
        float h = u * p1.y + v * p2.y + w * p3.y;
        if (std::abs(h - searchY) < 2.0f) return h;
        bestHeight = h;
        minDiff = std::abs(h - searchY);
        foundAny = true;
      }
    }
  }
  const std::vector<int>& candidates = trackGrid.getCandidates(x, z);
  for (int i : candidates) {
    if (i == lastFoundTriangleIndex) continue;
    int base = i * 9;
    glm::vec3 p1(track->positions[base], track->positions[base + 1], track->positions[base + 2]);
    glm::vec3 p2(track->positions[base + 3], track->positions[base + 4], track->positions[base + 5]);
    glm::vec3 p3(track->positions[base + 6], track->positions[base + 7], track->positions[base + 8]);
    float minX = std::min({p1.x, p2.x, p3.x});
    float maxX = std::max({p1.x, p2.x, p3.x});
    float minZ = std::min({p1.z, p2.z, p3.z});
    float maxZ = std::max({p1.z, p2.z, p3.z});
    if (x < minX - EPSILON || x > maxX + EPSILON || z < minZ - EPSILON || z > maxZ + EPSILON) continue;
    float u, v, w;
    if (barycentric(p, glm::vec2(p1.x, p1.z), glm::vec2(p2.x, p2.z), glm::vec2(p3.x, p3.z), &u, &v, &w) > 0) {
      if (u >= -EPSILON && v >= -EPSILON && w >= -EPSILON) {
        float h = u * p1.y + v * p2.y + w * p3.y;
        float diff = std::abs(h - searchY);
        if (diff < minDiff) {
          minDiff = diff;
          bestHeight = h;
          lastFoundTriangleIndex = i;
          foundAny = true;
        }
      }
    }
  }
  if (foundAny) return bestHeight;
  return -10000.0f;
}

// [NEW] Lap Timer UI (Top Right) - Refined V4 (No Sector Values, Bold Best)
void renderLapTimer(int lap, float curTime, float bestTime, float s1, int s1St, float s2, int s2St, float s3,
                    int s3St) {
  int winW, winH;
  glfwGetFramebufferSize(ctx.window, &winW, &winH);
  glEnable(GL_SCISSOR_TEST);

  float scale = (float)winH / 1000.0f;
  if (scale < 0.5f) scale = 0.5f;
  auto S = [&](int v) { return (int)(v * scale); };

  // Design: Taller to fit Footer
  int w = S(360);
  int h = S(110);
  int x = winW - w - S(30);
  int y = winH - h - S(30);

  // Colors
  glm::vec3 colHeaderDef(0.18f, 0.20f, 0.25f);
  glm::vec3 colBody(0.08f, 0.08f, 0.08f);
  glm::vec3 colBorder(0.3f, 0.3f, 0.35f);
  glm::vec3 colText(0.95f, 0.95f, 0.95f);      // White
  glm::vec3 colTextDark(0.05f, 0.05f, 0.05f);  // Black
  glm::vec3 colTextDim(0.6f, 0.6f, 0.6f);
  glm::vec3 colGreen(0.0f, 0.8f, 0.0f);   // PB Green
  glm::vec3 colYellow(0.9f, 0.8f, 0.1f);  // Slower Yellow

  // Layout Heights
  int headerH = S(24);
  int footerH = S(25);
  int bodyH = h - headerH - footerH;

  int border = S(2);

  // 1. Draw Structure (Outer Border)
  drawRect(x, y, w, h, colBorder);

  // Sections Y
  int syHeader = y + h - headerH - border;
  int syBody = y + footerH + border;  // Body starts above footer
  int syFooter = y + border;

  // Sections X
  int sectionW = (w - border * 2) / 3;
  int sx = x + border;

  // Helpers
  int charW = S(8);
  int charH = S(10);

  // Helper to draw char with custom stroke width
  auto drawCharEx = [&](int cx, int cy, char c, glm::vec3 col, int strokeW) {
    if (strokeW < 1) strokeW = 1;

    if (c == 'S') {
      drawRect(cx, cy + charH - strokeW, charW, strokeW, col);
      drawRect(cx, cy + charH / 2, charW, strokeW, col);
      drawRect(cx, cy, charW, strokeW, col);
      drawRect(cx, cy + charH / 2, strokeW, charH / 2, col);
      drawRect(cx + charW - strokeW, cy, strokeW, charH / 2, col);
    } else if (c == 'B') {
      drawRect(cx, cy, strokeW, charH, col);
      drawRect(cx, cy + charH - strokeW, charW, strokeW, col);
      drawRect(cx, cy + charH / 2, charW, strokeW, col);
      drawRect(cx, cy, charW, strokeW, col);
      drawRect(cx + charW - strokeW, cy, strokeW, charH, col);
    } else if (c == 'E') {
      drawRect(cx, cy, strokeW, charH, col);
      drawRect(cx, cy + charH - strokeW, charW, strokeW, col);
      drawRect(cx, cy + charH / 2, charW, strokeW, col);
      drawRect(cx, cy, charW, strokeW, col);
    } else if (c == 'T') {
      drawRect(cx + charW / 2 - strokeW / 2, cy, strokeW, charH, col);
      drawRect(cx, cy + charH - strokeW, charW, strokeW, col);
    } else if (c == '/') {
      int steps = 4;
      int stepH = charH / steps;
      for (int k = 0; k < steps; k++) {
        drawRect(cx + k * S(2), cy + k * stepH, strokeW * 2, stepH + 1, col);
      }
    } else if (c == '-') {
      drawRect(cx, cy + charH / 2, charW, strokeW, col);
    }
  };

  int defaultCW = S(1);
  if (defaultCW < 1) defaultCW = 1;

  auto drawChar = [&](int cx, int cy, char c, glm::vec3 col) { drawCharEx(cx, cy, c, col, defaultCW); };

  auto drawWordEx = [&](int cx, int cy, const char* str, glm::vec3 col, int strokeW) {
    while (*str) {
      drawCharEx(cx, cy, *str, col, strokeW);
      cx += charW + S(4);
      str++;
    }
  };

  // 2. Draw Header Sections (Background + Text)
  for (int i = 0; i < 3; i++) {
    int state = (i == 0 ? s1St : (i == 1 ? s2St : s3St));
    // float val = (i==0 ? s1 : (i==1 ? s2 : s3)); // [User] Don't show numeric

    glm::vec3 bg = colHeaderDef;
    glm::vec3 txt = colText;

    // Background Color Logic
    if (state == 1) {
      bg = colYellow;
      txt = colTextDark;
    } else if (state == 2) {
      bg = colGreen;
      txt = colTextDark;
    }

    // Draw BG Box
    drawRect(sx + i * sectionW, syHeader, sectionW - S(2), headerH, bg);

    // Draw Label: Always "S1", "S2", "S3"
    int width = sectionW - S(2);
    int availW = S(20);
    int cx = sx + i * sectionW + (width - availW) / 2;
    int cy = syHeader + (headerH - charH) / 2;

    drawChar(cx, cy, 'S', txt);
    drawDigit(cx + S(10), cy, S(8), i + 1, txt);
  }

  // 3. Body (Black)
  drawRect(x + border, syBody, w - border * 2, bodyH - border, colBody);

  int bodyCenterY = syBody + bodyH / 2;

  // Left: Lap / Total
  int lx = x + S(25);
  int curLapFnSize = S(14);

  if (lap > 99) lap = 99;
  if (lap >= 10) {
    drawDigit(lx, bodyCenterY - curLapFnSize, curLapFnSize, lap / 10, colText);
    lx += curLapFnSize * 1.5;
  }
  drawDigit(lx, bodyCenterY - curLapFnSize, curLapFnSize, lap % 10, colText);
  lx += curLapFnSize * 1.5;

  lx += S(8);
  drawChar(lx, bodyCenterY - charH / 2, '/', colTextDim);
  lx += S(12);

  int total = 20;
  int totSize = S(12);
  drawDigit(lx, bodyCenterY - totSize, totSize, total / 10, colTextDim);
  lx += totSize * 1.5;
  drawDigit(lx, bodyCenterY - totSize, totSize, total % 10, colTextDim);

  // Right: Current Time
  auto drawTimeFull = [&](int rx, int cy, int tSize, float tVal, glm::vec3 col) {
    int step = tSize * 1.5f;
    int dotW = tSize * 0.5f;
    int ty = cy - tSize;

    int mm = (int)(tVal / 60);
    int ss = (int)tVal % 60;
    int ms = (int)((tVal - std::floor(tVal)) * 1000);

    int totalW = 3 * step + dotW + 2 * step;
    if (mm > 0) totalW += step * (mm > 9 ? 2 : 1) + dotW;

    int tx = rx - totalW;

    if (mm > 0) {
      if (mm > 9) {
        drawDigit(tx, ty, tSize, mm / 10, col);
        tx += step;
      }
      drawDigit(tx, ty, tSize, mm % 10, col);
      tx += step;
      drawRect(tx, ty + tSize * 0.5, tSize / 4, tSize / 4, col);
      drawRect(tx, ty + tSize * 1.2, tSize / 4, tSize / 4, col);
      tx += dotW;
    }
    drawDigit(tx, ty, tSize, ss / 10, col);
    tx += step;
    drawDigit(tx, ty, tSize, ss % 10, col);
    tx += step;
    drawRect(tx, ty, tSize / 4, tSize / 4, col);
    tx += dotW;
    drawDigit(tx, ty, tSize, ms / 100, col);
    tx += step;
    drawDigit(tx, ty, tSize, (ms / 10) % 10, col);
    tx += step;
    drawDigit(tx, ty, tSize, ms % 10, col);
  };

  int rx = x + w - S(25);
  drawTimeFull(rx, bodyCenterY, S(14), curTime, colText);

  // 4. Footer (Best Time)
  drawRect(x + border, syFooter, w - border * 2, footerH - border, colHeaderDef);

  int footerCenterY = syFooter + footerH / 2;

  // "BEST" Label (Left) - [User] Bold + Padding
  int boldCW = defaultCW + S(1);  // Thicker
  drawWordEx(x + S(25), footerCenterY - charH / 2, "BEST", colTextDim, boldCW);

  // Best Time Value (Right)
  if (bestTime < 999.0f) {
    drawTimeFull(rx, footerCenterY, S(12), bestTime, colGreen);
  } else {
    drawWordEx(rx - S(40), footerCenterY - charH / 2, "--:--.---", colTextDim, defaultCW);
  }

  glDisable(GL_SCISSOR_TEST);
}

// Input Helpers (ProcessInput, KeyCallback) 保持不變
void processInput(GLFWwindow* window) {
  inputState.up = false;
  inputState.down = false;
  inputState.left = false;
  inputState.right = false;
  inputState.brake = false;

  // [NEW] If using Free Cam, disable car control inputs
  extern bool useFreeCam;
  if (useFreeCam) return;

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) inputState.up = true;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    inputState.down = true;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) inputState.left = true;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) inputState.right = true;
  if (glfwJoystickPresent(GLFW_JOYSTICK_1)) {
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1)) {
      GLFWgamepadstate state;
      if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) {
        float deadzone = 0.2f;
        float steerX = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
        if (std::abs(steerX) < deadzone) steerX = 0.0f;

        // Digital Fallback (Keep for compatibility)
        if (steerX < -deadzone) inputState.left = true;
        if (steerX > deadzone) inputState.right = true;

        // [NEW] Analog Non-Linear Steering
        // Curve: sign(x) * pow(abs(x), 2.2)
        inputState.steerValue = (steerX > 0 ? 1.0f : -1.0f) * std::pow(std::abs(steerX), 2.2f);

        // Throttle (Right Trigger usually -1 to 1, or 0 to 1 depending on OS mapping)
        // GLFW standard: -1 (released) to 1 (pressed)
        float throttleAxis = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
        // Map -1..1 to 0..1
        float throttleRaw = (throttleAxis + 1.0f) / 2.0f;
        // Non-Linear Throttle (More control at low speed)
        inputState.throttleValue = std::pow(throttleRaw, 1.5f);
        if (inputState.throttleValue > 0.1f) inputState.up = true;

        // Brake (Left Trigger)
        float brakeAxis = state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
        float brakeRaw = (brakeAxis + 1.0f) / 2.0f;
        inputState.brakeValue = std::pow(brakeRaw, 1.5f);
        // if (inputState.brakeValue > 0.1f) inputState.down = true;  // Removed Reverse mapping specifically for
        // Trigger
        if (inputState.brakeValue > 0.1f) inputState.brake = true;
      }
    }
  }
}
void keyCallback(GLFWwindow* window, int key, int, int action, int) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    return;
  }
  if (action == GLFW_PRESS) {
    switch (key) {
      case GLFW_KEY_F1: {
        if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        else
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        break;
      }
      case GLFW_KEY_V: {
        extern bool useFPP;
        useFPP = !useFPP;
        break;
      }
      case GLFW_KEY_R: {
        extern bool enableRain;
        enableRain = !enableRain;
        break;
      }
      case GLFW_KEY_L: {
        extern bool useFreeCam;
        useFreeCam = !useFreeCam;
        // Ensure cursor is disabled for camera control
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (useFreeCam) {
          // When switching to free cam, we might want to tell the camera to reset its mouse tracking
          // so it doesn't jump.
          Camera* cam = (Camera*)glfwGetWindowUserPointer(window);
          if (cam) cam->setLastMousePos(window);
        }
        break;
      }
      case GLFW_KEY_P: {
        Camera* cam = (Camera*)glfwGetWindowUserPointer(window);
        if (cam) {
          const float* p = cam->getPosition();
          std::cout << "Camera Position: (" << p[0] << ", " << p[1] << ", " << p[2] << ")" << std::endl;
        }
        break;
      }
      default:
        break;
    }
  }
}

// MAIN
int main() {
  initOpenGL();
  GLFWwindow* window = OpenGLContext::getWindow();
  glfwSetWindowTitle(window, "Final Project - 113550012");

  ctx.window = window;

  // [重要] 初始化 G-Buffer
  initGBuffer();

  // [NEW] Initialize Default Texture for PBR fallback
  ctx.defaultWhiteTexture = createDefaultTexture();

  Camera camera(glm::vec3(0, 2, 5));
  camera.initialize(OpenGLContext::getAspectRatio());
  glfwSetWindowUserPointer(window, &camera);
  ctx.camera = &camera;

  loadMaterial();
  loadModels();
  loadPrograms();

  setupObjects();

  float lastFrame = 0.0f;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    processInput(window);

    if (enableRain) {
      // 下雨：路面變滑 (This logic is now handled by Roughness Map in shader + GLB)
      // ctx.objects[0]->material.roughness = 0.15f; // REMOVED: Don't make grass shiny!
    } else {
      // 晴天：路面變粗糙
      // ctx.objects[0]->material.roughness = 0.85f;
    }

    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    // --- Traffic Light State Logic ---
    stateTimer += deltaTime;

    // [NEW] Update Penalty Timer
    if (penaltyTimer > 0.0f) {
      penaltyTimer -= deltaTime;
      if (penaltyTimer < 0.0f) penaltyTimer = 0.0f;
    }

    switch (currentGameState) {
      case PRE_START:
        if (stateTimer > 1.0f) {
          currentGameState = COUNTDOWN;
          stateTimer = 0.0f;
          lightsOnCount = 0;
        }
        break;
      case COUNTDOWN:
        // [NEW] Jump Start Check
        if (car && (std::abs(car->getSpeed()) > 0.1f || inputState.throttleValue > 0.1f)) {
          jumpStartPenalty = true;
          penaltyTimer = 1.0f;  // Flash for 3 seconds
        }

        if (stateTimer >= 1.0f) {
          stateTimer = 0.0f;
          lightsOnCount++;
          if (lightsOnCount >= 5) {
            currentGameState = READY;
            // Random delay between 2.0 and 4.0 seconds for lights out
            readyDelay = randomFloat(2.0f, 4.0f);
          }
        }
        break;
      case READY:
        // [NEW] Jump Start Check
        if (car && (std::abs(car->getSpeed()) > 0.1f || inputState.throttleValue > 0.1f)) {
          jumpStartPenalty = true;
          penaltyTimer = 3.0f;  // Flash for 3 seconds
        }

        if (stateTimer >= readyDelay) {
          currentGameState = RACING;
          lightsOnCount = 0;  // Lights Out!
        }
        break;
      case RACING:
        // Race is on!
        break;
    }

    // Input Interception
    InputState effectiveInput = inputState;
    if (currentGameState != RACING) {
      // Allow reving engine but punish with flash?
      // Actually user requested "early start" penalty.
      // If we disable throttle here, car won't move, so "jump start" technically impossible unless we allow movement.
      // But if we allow movement, they can cheat.
      // Usually games allow movement but teleport back or give penalty time.
      // OR, visual penalty AND simply block movement.
      // "提早起步" implies they TRIED to move.
      // So detecting input is enough even if we block movement.
      effectiveInput.up = false;
    }

    // --- Traffic Light 3D Illumination ---
    // User provided coordinates:
    // 1. (105.387, 12.0478, -180.967)
    // 2. (105.457, 12.0538, -179.874)
    // 3. (105.416, 12.0844, -178.885)
    // 4. (105.305, 12.0376, -177.746)
    // 5. (105.468, 12.1329, -176.653)

    glm::vec3 lightPositions[] = {glm::vec3(105.1f, 12.0478f, -180.967f), glm::vec3(105.2f, 12.0538f, -179.874f),
                                  glm::vec3(105.3f, 12.0844f, -178.885f), glm::vec3(105.3f, 12.0376f, -177.746f),
                                  glm::vec3(105.3f, 12.1329f, -176.653f)};

    if (currentGameState != RACING) {
      for (int i = 0; i < 5; i++) {
        if (lightsOnCount > i) {
          ctx.pointLightEnable[i] = 1;
          ctx.pointLightPosition[i] = lightPositions[i];
          ctx.pointLightColor[i] = glm::vec3(5.0f, 0.0f, 0.0f);  // Bright Red
          ctx.pointLightConstant[i] = 1.0f;
          // Tune falloff for individual lights (smaller range)
          ctx.pointLightLinear[i] = 0.35f;
          ctx.pointLightQuadratic[i] = 0.44f;
        } else {
          ctx.pointLightEnable[i] = 0;
        }
      }
    } else {
      // Turn off all lights when racing
      for (int i = 0; i < 5; i++) {
        ctx.pointLightEnable[i] = 0;
      }
    }

    // Logic Update
    if (car) {
      // Update Car Physics
      car->update(deltaTime, effectiveInput, [&](float x, float z) { return getTrackHeight(x, z); });

      // Update Camera (Free vs Follow)
      if (useFreeCam) {
        camera.move(window);
      } else {
        if (useFPP) {
          camera.updateFPP(car->getPosition() - car->getFront() * 0.35f, car->getFront(), car->getUp());
        } else {
          camera.updateTPS(car->getPosition(), car->getFront(), deltaTime);
        }
      }

      // Light Logic Update
      ctx.spotLightPosition[0] = car->getPosition() + glm::vec3(0.0f, 0.5f, 0.0f);
      ctx.spotLightDirection[0] = car->getFront();

      // --- Lap Counting Logic ---
      glm::vec3 pos = car->getPosition();
      // Checkpoint: Roughly halfway (Z > 0)
      if (pos.z > 0.0f && !passedCheckpoint) {
        passedCheckpoint = true;
      }
      // [NEW] Checkpoints Logic
      // S1 Trigger: X [126, 129], Z [146, 164]
      if (!passedS1 && pos.x > 126.0f && pos.x < 129.0f && pos.z > 146.0f && pos.z < 164.0f) {
        passedS1 = true;
        lapTimeAtS1 = currentLapTime;
        curS1 = currentLapTime;  // S1 Duration = Time from Start

        if (curS1 < bestS1) {
          bestS1 = curS1;
          s1State = 2;  // Green
        } else {
          s1State = 1;  // Yellow
        }
      }

      // S2 Trigger: X [-118, -102], Z [57, 69]
      if (!passedS2 && pos.x > -118.0f && pos.x < -102.0f && pos.z > 57.0f && pos.z < 69.0f) {
        passedS2 = true;
        lapTimeAtS2 = currentLapTime;
        curS2 = currentLapTime - lapTimeAtS1;  // S2 Duration

        if (curS2 < bestS2) {
          bestS2 = curS2;
          s2State = 2;  // Green
        } else {
          s2State = 1;  // Yellow
        }
      }

      // Finish Line: Between (106.7, -188.3) and (107.0, -169.3)
      // Car starts at X=88.6 and moves +X towards the line at X~106.8
      // Trigger when X > 106.5 and Z is within range [-189, -169]
      if (pos.x > 106.5f && pos.z > -189.0f && pos.z < -169.0f && passedCheckpoint) {
        // [New] Lap Time Logic
        if (currentLap > 0) {  // If completing a valid lap (Lap 0 is start grid)
          lastLapTime = currentLapTime;
          if (lastLapTime < bestLapTime) bestLapTime = lastLapTime;

          // S3 Calculation: Total - TimeAtS2
          // If missed S2 (e.g. cheat), use Total - LastSplit
          float tMinus = (passedS2 ? lapTimeAtS2 : (passedS1 ? lapTimeAtS1 : 0.0f));
          curS3 = currentLapTime - tMinus;

          if (curS3 < bestS3) {
            bestS3 = curS3;
            s3State = 2;
          } else {
            s3State = 1;
          }
        }
        currentLapTime = 0.0f;  // Reset Timer
        // Reset flags for new lap
        passedS1 = false;
        passedS2 = false;
        s1State = 0;
        s2State = 0;
        s3State = 0;  // Clear display colors? Or keep them?
        // Usually racing games keep previous lap sectors until you cross them again.
        // But for "State", we might want to reset to "None" so they turn grey until crossed.
        s1State = 0;
        s2State = 0;
        s3State = 0;
        curS1 = 0;
        curS2 = 0;
        curS3 = 0;

        currentLap++;
        passedCheckpoint = false;
        // Optional: Reset/Flash lights or something?
      }

      // Update Timer
      if (currentGameState == RACING) {
        currentLapTime += deltaTime;
      }
    }

    // Shadow Matrices
    glm::mat4 lightProjection, lightView;
    float near_plane = 1.0f, far_plane = 50.0f;
    lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, near_plane, far_plane);

    glm::vec3 targetPos = glm::vec3(0.0f);
    if (car) {
      targetPos = car->getPosition();
    }

    // [調整] 投影範圍 (Ortho Size)
    // 範圍越小，陰影越清晰；範圍越大，能看到的陰影越遠但越模糊
    // [Updated] User requested larger distance. Increased from 20.0f to 60.0f
    float orthoSize = 60.0f;
    lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 1.0f, 200.0f);

    // [調整] 光源位置
    // 保持光源方向，但位置要移動到車子上方/後方
    // 我們把光源放在車子位置的 "反方向" 延伸出去
    glm::vec3 lightDir = glm::normalize(ctx.directionLightDirection);
    // Move light back further to cover the larger ortho box
    glm::vec3 lightPos = targetPos - lightDir * 100.0f;  // Increased offset from 50 to 100

    // [技巧] Texel Snapping (選用) - 防止陰影邊緣隨著車子移動而閃爍
    // 如果你發現陰影邊緣在抖動，可以解除下面的註解
    float shadowMapResolution = (float)ctx.SHADOW_WIDTH;  // Use context width
    float unitsPerTexel = (2.0f * orthoSize) / shadowMapResolution;
    lightPos.x = floor(lightPos.x / unitsPerTexel) * unitsPerTexel;
    lightPos.y = floor(lightPos.y / unitsPerTexel) * unitsPerTexel;
    lightPos.z = floor(lightPos.z / unitsPerTexel) * unitsPerTexel;

    // Fix: Look at lightPos + lightDir (not targetPos) to keep rotation constant!
    // If we look at targetPos, the rotation changes slightly as we snap lightPos, causing shimmer.
    lightView = glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0.0f, 1.0f, 0.0f));
    ctx.lightSpaceMatrix = lightProjection * lightView;

    // --- RENDER PASS 1: Shadow Map ---
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.depthMapFBO);
    glViewport(0, 0, ctx.SHADOW_WIDTH, ctx.SHADOW_HEIGHT);
    glClear(GL_DEPTH_BUFFER_BIT);
    // ctx.programs[0] is ShadowProgram (based on loadPrograms order)
    ctx.programs[0]->doMainLoop();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- RENDER PASS 2: Geometry Pass (G-Buffer) ---
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.gBuffer);
    int scrWidth, scrHeight;
    glfwGetFramebufferSize(window, &scrWidth, &scrHeight);
    glViewport(0, 0, scrWidth, scrHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // ctx.programs[1] is GeometryProgram
    ctx.programs[1]->doMainLoop();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- RENDER PASS 3: Lighting Pass (Deferred) ---
    // Render to HDR FBO
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
    glViewport(0, 0, scrWidth, scrHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Technique: Blit depth buffer from gBuffer to HDR FBO
    glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.hdrFBO);  // Write to HDR FBO
    glBlitFramebuffer(0, 0, scrWidth, scrHeight, 0, 0, scrWidth, scrHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);  // Bind back HDR FBO

    // Now draw lighting quad (Output to Color + Brightness)
    ctx.programs[2]->doMainLoop();

    // --- RENDER PASS 4: Forward Pass (Transparents / Skybox) ---
    // Still render to HDR FBO
    // ctx.programs[3,4,5] are Skybox, Rain, Smoke
    for (size_t i = 3; i < ctx.programs.size(); i++) {
      // [Safety check] Stop if we hit MiniMap or Blur programs (indices might change if we push back more)
      // MiniMap is loaded after Smoke. Indices:
      // 0: Shadow, 1: Geometry, 2: Light, 3: Skybox, 4: Rain, 5: Smoke, 6: MiniMap, 7: Blur, 8: BloomFinal
      // We only want 3, 4, 5 here. 6 is UI (should be on screen? or HDR?).
      // UI (MiniMap) is usually LDR and on top.
      // Skybox/Rain/Smoke are HDR.
      if (i > 5) break;

      if (i == 4) {  // Rain is index 4
        extern bool enableRain;
        RainProgram* rainProg = (RainProgram*)ctx.programs[i];
        rainProg->enableEmission = enableRain;
      }
      ctx.programs[i]->doMainLoop();
    }

    // --- RENDER PASS 5: Blur Pass ---
    bool horizontal = true, first_iteration = true;
    unsigned int amount = 10;                       // Blur iterations
    glUseProgram(ctx.programs[7]->getProgramId());  // Blur Program
    // Assuming programs[7] is Blur

    for (unsigned int i = 0; i < amount; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER, ctx.pingpongFBO[horizontal]);
      glUniform1i(glGetUniformLocation(ctx.programs[7]->getProgramId(), "horizontal"), horizontal);

      // Bind texture to blur
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, first_iteration ? ctx.colorBuffers[1] : ctx.pingpongColorbuffers[!horizontal]);
      // ctx.colorBuffers[1] is the Brightness buffer

      // Draw Quad
      glBindVertexArray(((LightProgram*)ctx.programs[2])->VAO_Quad);  // Reusing Quad VAO from LightProgram
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

      horizontal = !horizontal;
      if (first_iteration) first_iteration = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- RENDER PASS 6: Final Bloom Combine ---
    // Render to Default Framebuffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(ctx.programs[8]->getProgramId());  // BloomFinal

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx.colorBuffers[0]);  // Scene
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx.pingpongColorbuffers[!horizontal]);  // Blurred Brightness
    glUniform1i(glGetUniformLocation(ctx.programs[8]->getProgramId(), "scene"), 0);
    glUniform1i(glGetUniformLocation(ctx.programs[8]->getProgramId(), "bloomBlur"), 1);
    glUniform1f(glGetUniformLocation(ctx.programs[8]->getProgramId(), "exposure"), 1.0f);

    // Draw Quad
    glBindVertexArray(((LightProgram*)ctx.programs[2])->VAO_Quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // --- RENDER PASS 7: UI (MiniMap / HUD) ---
    // UI should be drawn LAST on top of everything.
    // MiniMap is program[6]
    ctx.programs[6]->doMainLoop();

    // F1 HUD
    renderF1HUD(window, car->getSpeed(), inputState, useFPP, penaltyTimer);

    // [NEW] Timer HUD (Top Right)
    renderLapTimer(currentLap, currentLapTime, bestLapTime, curS1, s1State, curS2, s2State, curS3, s3State);

#ifdef __APPLE__
    glFlush();
#endif
    glfwSwapBuffers(window);
  }
  return 0;
}

void initOpenGL() {
#ifdef __APPLE__
  OpenGLContext::createContext(21, GLFW_OPENGL_ANY_PROFILE);
#else
  OpenGLContext::createContext(21, GLFW_OPENGL_ANY_PROFILE);
#endif
  GLFWwindow* window = OpenGLContext::getWindow();
  glfwSetKeyCallback(window, keyCallback);
  glfwSetFramebufferSizeCallback(window, resizeCallback);
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}