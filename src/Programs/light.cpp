#include <iostream>
#include <string>
#include <vector>
#include "context.h"
#include "program.h"

// 定義全螢幕四邊形的頂點數據
float quadVertices[] = {
    // positions        // texture Coords
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
};

bool LightProgram::load() {
  // 注意：這裡的 vertProgramFile 應該是一個簡單的 Pass-through vertex shader (例如 quad.vert)
  // fragProgramFile 則是您的 lighting_deferred.frag
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  // --- [修改 1] 建立全螢幕四邊形 VAO，而不是模型的 VAO ---
  glGenVertexArrays(1, &VAO_Quad);  // 假設您在 header 定義了 GLuint VAO_Quad;
  glGenBuffers(1, &VBO_Quad);       // 假設您在 header 定義了 GLuint VBO_Quad;

  glBindVertexArray(VAO_Quad);
  glBindBuffer(GL_ARRAY_BUFFER, VBO_Quad);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

  // 設定 Position (location = 0)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

  // 設定 TexCoords (location = 1) - 雖然 deferred frag 裡可能直接用 gl_FragCoord 計算，但傳入 UV 比較保險
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

  return programId != 0;
}

void LightProgram::doMainLoop() {
  glUseProgram(programId);

  // --- [修改 2] 綁定 G-Buffer 紋理 ---
  // 假設 Context 中已經儲存了 G-Buffer 的 texture handle

  // 綁定 Position (Unit 0)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ctx->gPosition);
  glUniform1i(glGetUniformLocation(programId, "gPosition"), 0);

  // 綁定 Normal (Unit 1)
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, ctx->gNormal);
  glUniform1i(glGetUniformLocation(programId, "gNormal"), 1);

  // 綁定 Albedo + Metallic (Unit 2)
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, ctx->gAlbedoSpec);
  glUniform1i(glGetUniformLocation(programId, "gAlbedoSpec"), 2);

  // 綁定 Roughness + AO (Unit 3)
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, ctx->gPBRParams);  // 或是 gNormal (如果存一起的話)
  glUniform1i(glGetUniformLocation(programId, "gPBRParams"), 3);

  // --- [修改 3] 設定全域 Uniforms (不需要 Model Matrix 了) ---

  // 設定 Camera Position (計算 Specular/Reflection 用)
  const float* vp = ctx->camera->getPosition();
  glUniform3f(glGetUniformLocation(programId, "viewPos"), vp[0], vp[1], vp[2]);

  // 設定 Light Space Matrix (如果要在 Deferred 做陰影，需要這個矩陣來反推 ShadowCoord)
  // 注意：Deferred Rendering 沒有 vertex shader 傳來的 FragPosLightSpace，必須自己在 Fragment Shader 用 inverse view
  // 計算
  glUniformMatrix4fv(glGetUniformLocation(programId, "lightSpaceMatrix"), 1, GL_FALSE,
                     glm::value_ptr(ctx->lightSpaceMatrix));

  // [NEW] SSR Matrices
  const float* v = ctx->camera->getViewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "view"), 1, GL_FALSE, v);

  const float* p = ctx->camera->getProjectionMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "projection"), 1, GL_FALSE, p);

  // --- [保留] 光源設定 (這部分跟原本一樣，只是移除了對物件的迴圈) ---

  // Directional Light
  glUniform1i(glGetUniformLocation(programId, "dl.enable"), ctx->directionLightEnable);
  glUniform3fv(glGetUniformLocation(programId, "dl.direction"), 1, glm::value_ptr(ctx->directionLightDirection));
  glUniform3fv(glGetUniformLocation(programId, "dl.lightColor"), 1, glm::value_ptr(ctx->directionLightColor));

  // Point Lights
  for (int j = 0; j < 6; j++) {
    std::string base = "pointLights[" + std::to_string(j) + "]";
    glUniform1i(glGetUniformLocation(programId, (base + ".enable").c_str()), ctx->pointLightEnable[j]);
    glUniform3fv(glGetUniformLocation(programId, (base + ".position").c_str()), 1,
                 glm::value_ptr(ctx->pointLightPosition[j]));
    glUniform3fv(glGetUniformLocation(programId, (base + ".lightColor").c_str()), 1,
                 glm::value_ptr(ctx->pointLightColor[j]));
    glUniform1f(glGetUniformLocation(programId, (base + ".constant").c_str()), ctx->pointLightConstant[j]);
    glUniform1f(glGetUniformLocation(programId, (base + ".linear").c_str()), ctx->pointLightLinear[j]);
    glUniform1f(glGetUniformLocation(programId, (base + ".quadratic").c_str()), ctx->pointLightQuadratic[j]);
  }

  // Spot Lights
  for (int j = 0; j < Context::MAX_SPOT_LIGHTS; j++) {
    std::string base = "spotLights[" + std::to_string(j) + "]";
    glUniform1i(glGetUniformLocation(programId, (base + ".enable").c_str()), ctx->spotLightEnable[j]);
    glUniform3fv(glGetUniformLocation(programId, (base + ".position").c_str()), 1,
                 glm::value_ptr(ctx->spotLightPosition[j]));
    glUniform3fv(glGetUniformLocation(programId, (base + ".direction").c_str()), 1,
                 glm::value_ptr(ctx->spotLightDirection[j]));
    glUniform3fv(glGetUniformLocation(programId, (base + ".lightColor").c_str()), 1,
                 glm::value_ptr(ctx->spotLightColor[j]));
    glUniform1f(glGetUniformLocation(programId, (base + ".cutOff").c_str()), ctx->spotLightCutOff[j]);
    glUniform1f(glGetUniformLocation(programId, (base + ".constant").c_str()), ctx->spotLightConstant[j]);
    glUniform1f(glGetUniformLocation(programId, (base + ".linear").c_str()), ctx->spotLightLinear[j]);
    glUniform1f(glGetUniformLocation(programId, (base + ".quadratic").c_str()), ctx->spotLightQuardratic[j]);
  }

  // --- [修改 4] 綁定環境與陰影貼圖 ---

  // Skybox (Irradiance / Reflection)
  if (ctx->cubemapTexture != 0) {
    glActiveTexture(GL_TEXTURE4);  // 改到 Unit 4，因為 0-3 被 G-Buffer 用了
    glBindTexture(GL_TEXTURE_CUBE_MAP, ctx->cubemapTexture);
    glUniform1i(glGetUniformLocation(programId, "skybox"), 4);
  }

  // Shadow Map
  glActiveTexture(GL_TEXTURE5);  // 改到 Unit 5
  glBindTexture(GL_TEXTURE_2D, ctx->depthMapTexture);
  glUniform1i(glGetUniformLocation(programId, "shadowMap"), 5);

  // [NEW] Fog Uniforms
  glUniform1f(glGetUniformLocation(programId, "fogDensity"), 0.003f);
  glUniform3f(glGetUniformLocation(programId, "fogColor"), 0.5f, 0.6f, 0.7f);

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);  // 禁止寫入深度

  glBindVertexArray(VAO_Quad);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  // 畫完後立刻恢復，以免影響後面的 Forward Pass (Skybox, Rain 等)
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);

  glUseProgram(0);
}