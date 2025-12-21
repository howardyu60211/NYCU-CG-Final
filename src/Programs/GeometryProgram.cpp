#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include "context.h"
#include "program.h"

bool GeometryProgram::load() {
  // 使用 geometry pass 的 shader
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  // 這一部分與舊的 LightProgram 完全一樣：為所有模型生成 VAO
  int num_model = (int)ctx->models.size();

  // 這裡假設 Program 類別裡有定義 GLuint* VAO;
  // 如果 Compile 報錯，請確認 program.h 裡的 VAO 是否為 protected 或 public
  VAO = new GLuint[num_model];

  glGenVertexArrays(num_model, VAO);
  for (int i = 0; i < num_model; i++) {
    glBindVertexArray(VAO[i]);
    Model* model = ctx->models[i];

    GLuint VBO[4];
    glGenBuffers(4, VBO);

    // 1. Positions
    glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * model->positions.size(), model->positions.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // 2. Normals
    glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * model->normals.size(), model->normals.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // 3. TexCoords
    glBindBuffer(GL_ARRAY_BUFFER, VBO[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * model->texcoords.size(), model->texcoords.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // 4. TexCoords1
    if (!model->texcoords1.empty()) {
      glBindBuffer(GL_ARRAY_BUFFER, VBO[3]);
      glBufferData(GL_ARRAY_BUFFER, sizeof(float) * model->texcoords1.size(), model->texcoords1.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    } else {
      glDisableVertexAttribArray(3);  // Just in case
    }
  }
  return programId != 0;
}

void GeometryProgram::doMainLoop() {
  glUseProgram(programId);

  // 設定相機矩陣 (View & Projection)
  const float* p = ctx->camera->getProjectionMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "Projection"), 1, GL_FALSE, p);

  const float* v = ctx->camera->getViewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(programId, "ViewMatrix"), 1, GL_FALSE, v);

  int obj_num = (int)ctx->objects.size();
  for (int i = 0; i < obj_num; i++) {
    int modelIndex = ctx->objects[i]->modelIndex;
    glBindVertexArray(VAO[modelIndex]);

    Model* model = ctx->models[modelIndex];

    // Model Matrix & Normal Matrix
    glm::mat4 fullModel = ctx->objects[i]->transformMatrix * model->modelMatrix;
    glUniformMatrix4fv(glGetUniformLocation(programId, "ModelMatrix"), 1, GL_FALSE, glm::value_ptr(fullModel));

    glm::mat4 TIMatrix = glm::transpose(glm::inverse(fullModel));
    glUniformMatrix4fv(glGetUniformLocation(programId, "TIModelMatrix"), 1, GL_FALSE, glm::value_ptr(TIMatrix));

    // PBR 材質參數
    glUniform1f(glGetUniformLocation(programId, "materialMetallic"), ctx->objects[i]->material.metallic);
    glUniform1f(glGetUniformLocation(programId, "materialAO"), ctx->objects[i]->material.ao);

    // 傳送預設粗糙度 (給沒貼圖的部分用)
    glUniform1f(glGetUniformLocation(programId, "defaultRoughness"), ctx->objects[i]->material.roughness);

    // PBR 材質參數
    glUniform1f(glGetUniformLocation(programId, "materialMetallic"), ctx->objects[i]->material.metallic);
    glUniform1f(glGetUniformLocation(programId, "materialAO"), ctx->objects[i]->material.ao);

    // 傳送預設粗糙度 (給沒貼圖的部分用)
    glUniform1f(glGetUniformLocation(programId, "defaultRoughness"), ctx->objects[i]->material.roughness);

    // [NEW] Identify if object is Track (Model 0)
    // Track is Model 0. Car Body is 1. Wheels are 2..5.
    int isTrack = (modelIndex == 0) ? 1 : 0;
    glUniform1i(glGetUniformLocation(programId, "uIsTrack"), isTrack);

    // [NEW] Pass Wetness Level
    extern bool enableRain;
    float wetness = enableRain ? 1.0f : 0.0f;
    glUniform1f(glGetUniformLocation(programId, "wetnessLevel"), wetness);

    // --- 開始繪製 (Diffuse 貼圖處理) ---
    // --- 開始繪製 (PBR Rendering) ---
    if (!model->meshes.empty()) {
      for (const auto& mesh : model->meshes) {
        // [1] Base Color
        glActiveTexture(GL_TEXTURE0);
        if (mesh.baseColorTexIndex >= 0 && mesh.baseColorTexIndex < (int)model->textures.size()) {
          glBindTexture(GL_TEXTURE_2D, model->textures[mesh.baseColorTexIndex]);
        } else {
          // 如果沒有貼圖，綁定預設白圖，這樣 1.0 * Factor 才會顯示正確顏色
          glBindTexture(GL_TEXTURE_2D, ctx->defaultWhiteTexture);
        }
        glUniform1i(glGetUniformLocation(programId, "diffuseMap"), 0);
        glUniform4fv(glGetUniformLocation(programId, "uBaseColorFactor"), 1, glm::value_ptr(mesh.baseColorFactor));

        // [2] Normal Map
        bool useNormal = false;
        if (mesh.normalTexIndex >= 0 && mesh.normalTexIndex < (int)model->textures.size()) {
          glActiveTexture(GL_TEXTURE1);
          glBindTexture(GL_TEXTURE_2D, model->textures[mesh.normalTexIndex]);
          useNormal = true;
        }
        glUniform1i(glGetUniformLocation(programId, "normalMap"), 1);
        glUniform1i(glGetUniformLocation(programId, "useNormalMap"), useNormal ? 1 : 0);

        // [3] ORM / Roughness Map
        // glTF: R=Occlusion, G=Roughness, B=Metallic
        bool useORM = false;
        glActiveTexture(GL_TEXTURE2);
        if (mesh.ormTexIndex >= 0 && mesh.ormTexIndex < (int)model->textures.size()) {
          glBindTexture(GL_TEXTURE_2D, model->textures[mesh.ormTexIndex]);
          useORM = true;
        } else if (ctx->objects[i]->modelIndex == 2) {
          // Fallback for Road (legacy override or if needed)
          glBindTexture(GL_TEXTURE_2D, 0);  // Or specific road texture?
          // Note: If Road is GLB, it should have ormTexIndex. If existing system, stick to GLB first.
        } else {
          glBindTexture(GL_TEXTURE_2D, ctx->defaultWhiteTexture);
        }

        glUniform1i(glGetUniformLocation(programId, "roughnessTextureSampler"), 2);
        glUniform1i(glGetUniformLocation(programId, "useRoughnessMap"), useORM ? 1 : 0);

        // PBR Factors
        glUniform1f(glGetUniformLocation(programId, "uRoughnessFactor"), mesh.roughnessFactor);
        glUniform1f(glGetUniformLocation(programId, "uMetallicFactor"), mesh.metallicFactor);

        // Emissive (Optional, for traffic lights)
        if (mesh.emissiveTexIndex >= 0 && mesh.emissiveTexIndex < (int)model->textures.size()) {
          glActiveTexture(GL_TEXTURE3);
          glBindTexture(GL_TEXTURE_2D, model->textures[mesh.emissiveTexIndex]);
          glUniform1i(glGetUniformLocation(programId, "emissiveMap"), 3);
          glUniform1i(glGetUniformLocation(programId, "useEmissiveMap"), 1);
        } else {
          glUniform1i(glGetUniformLocation(programId, "useEmissiveMap"), 0);
        }
        glUniform3fv(glGetUniformLocation(programId, "uEmissiveFactor"), 1, glm::value_ptr(mesh.emissiveFactor));

        // Disable culling for Car (Index >= 1) to fix missing faces
        if (modelIndex >= 1) glDisable(GL_CULL_FACE);

        glDrawArrays(model->drawMode, mesh.first, mesh.count);

        if (modelIndex >= 1) glEnable(GL_CULL_FACE);
      }
    } else {
      // Fallback for non-mesh models (should be rare now)
      // Disable culling for Car (Index >= 1) to fix missing faces
      if (modelIndex >= 1) glDisable(GL_CULL_FACE);

      glDrawArrays(model->drawMode, 0, model->numVertex);

      if (modelIndex >= 1) glEnable(GL_CULL_FACE);
    }
  }
  glUseProgram(0);
}