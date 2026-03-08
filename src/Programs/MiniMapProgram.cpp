#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include "context.h"
#include "program.h"

bool MiniMapProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  // 1. Setup Track VAO (From Model 0 - Track)
  if (!ctx->models.empty()) {
    Model* track = ctx->models[0];
    trackVertexCount = track->positions.size() / 3;

    glGenVertexArrays(1, &trackVAO);
    glBindVertexArray(trackVAO);

    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * track->positions.size(), track->positions.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Calculate Bounds
    minX = 1e9;
    maxX = -1e9;
    minY = 1e9;
    maxY = -1e9;
    minZ = 1e9;
    maxZ = -1e9;
    for (size_t i = 0; i < track->positions.size(); i += 3) {
      float x = track->positions[i];
      float y = track->positions[i + 1];
      float z = track->positions[i + 2];
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
      if (z < minZ) minZ = z;
      if (z > maxZ) maxZ = z;
    }
    // Padding
    float padding = 20.0f;
    minX -= padding;
    maxX += padding;
    minZ -= padding;
    maxZ += padding;
  }

  // 2. Setup Point VAO (Triangle for Dot) -> Now Circle
  {
    std::vector<float> vertices;
    // Center
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);

    // Circle Rim
    int segments = 32;
    for (int i = 0; i <= segments; ++i) {
      float theta = 2.0f * 3.1415926f * float(i) / float(segments);
      vertices.push_back(0.5f * cos(theta));  // X
      vertices.push_back(0.0f);               // Y
      vertices.push_back(0.5f * sin(theta));  // Z
    }

    glGenVertexArrays(1, &pointVAO);
    glBindVertexArray(pointVAO);

    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  }

  return programId != 0;
}

void MiniMapProgram::doMainLoop() {
  glUseProgram(programId);

  int width, height;
  glfwGetFramebufferSize(ctx->window, &width, &height);

  // MiniMap Settings
  int mapSize = 400;  // Enlarge
  int paddingX = 20;
  int paddingY = 10;                         // Smaller value to move UP (closer to top edge)
  int startX = paddingX;                     // Top-Left
  int startY = height - mapSize - paddingY;  // Top-Right logic (OpenGL origin bottom-left)

  // 1. Setup Viewport & Scissor
  glEnable(GL_SCISSOR_TEST);
  glScissor(startX, startY, mapSize, mapSize);
  glViewport(startX, startY, mapSize, mapSize);

  // Clear MiniMap Area
  glClear(GL_DEPTH_BUFFER_BIT);  // Always clear depth

  // 2. Setup Camera (Ortho Top-Down, Following Car)
  glm::mat4 VP = glm::mat4(1.0f);  // Default Initialize

  if (ctx->objects.size() > 1) {
    glm::vec3 carPos = glm::vec3(ctx->objects[1]->transformMatrix[3]);
    
    // 假設 Column 2 是 Forward (請確認你的模型矩陣定義，通常 OpenGL 預設 Forward 是 -Z)
    // 如果發現地圖上下顛倒，試著改成 -ctx->objects[1]->transformMatrix[2]
    glm::vec3 carForward = glm::vec3(ctx->objects[1]->transformMatrix[2]); 
    carForward = glm::normalize(carForward); // 確保正規化

    float zoom = 200.0f; 

    // Projection: 使用 Ortho 保持 2D 感
    // zNear 設為 -500 可以確保就算車子爬坡，相機下方的樹木或高樓也能被畫進去(不會被切掉)
    glm::mat4 projection = glm::ortho(-zoom, zoom, -zoom, zoom, -500.0f, 500.0f);

    // View: 
    // 1. 相機放在車子"正上方"
    glm::vec3 eye = carPos + glm::vec3(0.0f, 100.0f, 0.0f);
    glm::vec3 center = carPos;

    // 2. 關鍵：為了讓小地圖旋轉（車頭朝上），我們要把 LookAt 的 "Up" 設為車子的 "Forward"
    // 當相機垂直向下看時，Up 向量決定了螢幕的"上方"指向哪裡
    glm::vec3 up = carForward; 

    // 防呆：如果車子垂直爬牆 (Forward 變成 Y 軸)，與視線平行會壞掉
    // 但在賽車遊戲中通常不會發生完全垂直，除非翻車
    if (glm::length(glm::cross(up, glm::vec3(0,-1,0))) < 0.01f) {
        up = glm::vec3(0, 0, -1); // Fallback
    }

    glm::mat4 view = glm::lookAt(eye, center, up);

    VP = projection * view;
}

  // 3. Draw Track
  glm::mat4 bgModel = glm::mat4(1.0f);
  glm::mat4 mvp = VP * bgModel;
  glUniformMatrix4fv(glGetUniformLocation(programId, "MVP"), 1, GL_FALSE, glm::value_ptr(mvp));

  // [Fix] Ensure State for Track Drawing
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glBindVertexArray(trackVAO);

  if (!ctx->models.empty()) {
    Model* track = ctx->models[0];
    if (!track->meshes.empty()) {
      for (const auto& mesh : track->meshes) {
        if (mesh.ormTexIndex != -1) {
          glUniform3f(glGetUniformLocation(programId, "color"), 1.0f, 0.98f, 0.94f);  // Milky White
          glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
        } else {
          // [User Request] Remove Black Background Frame
          // Do not draw terrain/background meshes
        }
      }
    } else {
      glUniform3f(glGetUniformLocation(programId, "color"), 0.5f, 0.5f, 0.5f);
      glDrawArrays(GL_TRIANGLES, 0, trackVertexCount);
    }
  }

  // 4. Draw Red Dot (Player)
  if (ctx->objects.size() > 1) {
    // std::cout << "Drawing Dot. Car Pos: " << ctx->objects[1]->transformMatrix[3][0] << ", " <<
    // ctx->objects[1]->transformMatrix[3][2] << std::endl;
    // 4. Draw Red Dot (Player) - Screen Space (NDC)
    // Pinned to center of minimap
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Just scale it to be a small dot in NDC (-1 to 1)
    // Viewport is 400x400. Dot size ~10-20px?
    // 20px / 400px = 0.05.
    // NDC width is 2. So 0.05 * 2 = 0.1.
    glm::mat4 dotModel = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.1f));
    // Rotate 90 deg X to face camera? No, we are drawing 2D circle on XY plane (Screen).
    // The VAO (lines 64-66) defines circle on XZ plane: (cos, 0, sin).
    // We need to rotate it to XY plane for NDC.
    // Rotate 90 deg around X axis.
    dotModel = glm::rotate(dotModel, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    mvp = dotModel;  // Identity View/Proj
    glUniformMatrix4fv(glGetUniformLocation(programId, "MVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(glGetUniformLocation(programId, "color"), 1.0f, 0.0f, 0.0f);  // Red

    glBindVertexArray(pointVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 34);
    glUniform3f(glGetUniformLocation(programId, "color"), 1.0f, 0.0f, 0.0f);  // Red

    glBindVertexArray(pointVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 34);  // Center + 32 segments + 1 close

    glEnable(GL_DEPTH_TEST);  // Restore
    glEnable(GL_CULL_FACE);
  }

  // 5. Restore Viewport
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, width, height);

  glUseProgram(0);
}
