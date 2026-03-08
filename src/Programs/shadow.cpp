#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "context.h"
#include "program.h"

bool ShadowProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);
  if (programId == 0) return false;

  // 產生 Texture
  glGenTextures(1, &ctx->depthMapTexture);
  glBindTexture(GL_TEXTURE_2D, ctx->depthMapTexture);

  // 分配記憶體 (使用 Context 定義的寬高)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, ctx->SHADOW_WIDTH, ctx->SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT,
               GL_FLOAT, NULL);

  // 2. [關鍵修改] 設定硬體 PCF 比較模式
  // 這行告訴 OpenGL：當我們在 Shader 裡採樣這張圖時，自動幫我們比較深度並回傳 0.0~1.0 的混合結果
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

  // 設定濾波器 (配合 Soft Shadow 使用 GL_LINEAR)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // 設定邊界模式 (超出範圍不產生陰影)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

  // 產生 FBO 並綁定
  glGenFramebuffers(1, &ctx->depthMapFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, ctx->depthMapFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->depthMapTexture, 0);

  // 告訴 OpenGL 我們不畫顏色，只畫深度
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  // 檢查 FBO 是否完整
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "Shadow Framebuffer not complete!" << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);  // 解除綁定

  // --------------------------------------------------------
  // 2. 初始化 VAO (原本的程式碼)
  // --------------------------------------------------------
  int num_model = (int)ctx->models.size();
  VAO = new GLuint[num_model];
  glGenVertexArrays(num_model, VAO);
  for (int i = 0; i < num_model; i++) {
    glBindVertexArray(VAO[i]);
    Model* model = ctx->models[i];
    GLuint VBO;
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * model->positions.size(), model->positions.data(), GL_STATIC_DRAW);

    // Shadow Pass 通常只需要 Position (Location 0)
    // 除非你要做 Alpha Test (例如樹葉陰影)，否則不需要 UV 和 Normal
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  }

  return true;
}

void ShadowProgram::doMainLoop() {
  glUseProgram(programId);

  // 1. 設定光源矩陣 (保持原本的程式碼)
  glUniformMatrix4fv(glGetUniformLocation(programId, "lightSpaceMatrix"), 1, GL_FALSE,
                     glm::value_ptr(ctx->lightSpaceMatrix));

  // =========================================================
  // [關鍵修改] 畫陰影時，剔除正面 (只畫背面)
  // 這能極大程度消除地面的條紋
  // =========================================================
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);

  // 2. 繪製所有物件 (保持原本的迴圈)
  int obj_num = (int)ctx->objects.size();
  for (int i = 0; i < obj_num; i++) {
    int modelIndex = ctx->objects[i]->modelIndex;
    Model* model = ctx->models[modelIndex];
    glBindVertexArray(VAO[modelIndex]);

    glUniformMatrix4fv(glGetUniformLocation(programId, "ModelMatrix"), 1, GL_FALSE,
                       glm::value_ptr(ctx->objects[i]->transformMatrix));

    glDrawArrays(GL_TRIANGLES, 0, (int)model->positions.size() / 3);
  }

  // =========================================================
  // [關鍵修改] 畫完陰影後，記得把剔除模式改回背面 (恢復正常)
  // 否則接下來畫場景時，東西會不見或破掉
  // =========================================================
  glCullFace(GL_BACK);
  glUseProgram(0);
}