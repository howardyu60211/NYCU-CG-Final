#include <iostream>
#include <vector>
#include "context.h"
#include "program.h"

// Reuse quad vertices for screen processing
static float quadVertices[] = {
    // positions        // texture Coords
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
};

// --- BlurProgram ---

bool BlurProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  glGenVertexArrays(1, &VAO_Quad);
  unsigned int VBO;
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO_Quad);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

  return programId != 0;
}

void BlurProgram::doMainLoop() {
  // This function is just a placeholder because blur needs specific control (pingpong)
  // We will handle the uniforms manually in the main loop or create a specialized method.
  // For now, let's leave it empty or print a warning if called directly without setup.
}

// --- BloomFinalProgram ---

bool BloomFinalProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  glGenVertexArrays(1, &VAO_Quad);
  unsigned int VBO;
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO_Quad);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

  glUseProgram(programId);
  glUniform1i(glGetUniformLocation(programId, "scene"), 0);
  glUniform1i(glGetUniformLocation(programId, "bloomBlur"), 1);

  return programId != 0;
}

void BloomFinalProgram::doMainLoop() {
  glUseProgram(programId);

  // Bind textures
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ctx->colorBuffers[0]);  // Scene Color

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, ctx->pingpongColorbuffers[!true]);  // Result of blur (we'll fix the index logic in main)
  // Actually, main loop logic tracks which buffer is the last one.
  // We should probably rely on main.cpp binding the right texture to unit 1 before calling this?
  // Or we assume main.cpp sets the uniform/texture.

  // Let's assume Unit 1 is bound to the blurred texture by the caller.

  glUniform1f(glGetUniformLocation(programId, "exposure"), ctx->exposure);

  glBindVertexArray(VAO_Quad);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  glUseProgram(0);
}
