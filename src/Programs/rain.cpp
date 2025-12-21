#include <iostream>
#include <random>
#include <vector>
#include "context.h"
#include "program.h"

bool RainProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);

  // Allocate positions
  particlePositions = new float[maxParticles * 3];

  // Random initialization
  std::mt19937 rng(1337);
  std::uniform_real_distribution<float> distX(-20.0f, 20.0f);
  std::uniform_real_distribution<float> distY(0.0f, 20.0f);
  std::uniform_real_distribution<float> distZ(-20.0f, 20.0f);

  for (int i = 0; i < maxParticles; i++) {
    particlePositions[i * 3 + 0] = distX(rng);
    particlePositions[i * 3 + 1] = distY(rng);
    particlePositions[i * 3 + 2] = distZ(rng);
  }

  VAO = new GLuint[1];
  glGenVertexArrays(1, VAO);
  return programId != 0;
}

void RainProgram::doMainLoop() {
  glUseProgram(programId);

  // Update Particles
  // Simple gravity: y -= const
  // Reset if y < 0
  // To make it look like streaks, we draw GL_LINES from (x,y,z) to (x, y+len, z)
  // Actually, let's just update points and generate line vertices on CPU or GS.
  // Given the constraints, let's generate line vertices on CPU each frame.
  // 4000 particles * 2 vertices = 8000 verts. Very cheap.

  std::vector<float> lineVerts;
  lineVerts.reserve(maxParticles * 6);  // 2 points * 3 coords

  float rainSpeed = 0.5f;  // per frame/call approx
  float rainLen = 0.5f;

  // Get camera pos to center rain around
  const float* posPtr = ctx->camera->getPosition();
  glm::vec3 camPos(posPtr[0], posPtr[1], posPtr[2]);

  for (int i = 0; i < maxParticles; i++) {
    particlePositions[i * 3 + 1] -= rainSpeed;

    // Wrap around
    if (particlePositions[i * 3 + 1] < 0.0f) {
      if (enableEmission) {
        // Randomize Y to avoid "sheet" effect when respawning many at once
        float randomOffset = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 20.0f;
        particlePositions[i * 3 + 1] = 20.0f + randomOffset;
      }
      // Randomize X/Z slightly to avoid static patterns?
      // Keep them static for consistency or randomize? Randomize for better coverage.
      // But we need rng. For per-frame update, simpler logic is better.
      // Just wrap Y.
    }

    // Wrap X/Z relative to camera to simulate infinite rain
    // If particle is too far from camera, wrap it to the other side
    float dx = particlePositions[i * 3 + 0] - camPos.x;
    float dz = particlePositions[i * 3 + 2] - camPos.z;

    // 40x40 area
    if (dx > 20.0f) particlePositions[i * 3 + 0] -= 40.0f;
    if (dx < -20.0f) particlePositions[i * 3 + 0] += 40.0f;
    if (dz > 20.0f) particlePositions[i * 3 + 2] -= 40.0f;
    if (dz < -20.0f) particlePositions[i * 3 + 2] += 40.0f;

    float x = particlePositions[i * 3 + 0];
    float y = particlePositions[i * 3 + 1];
    float z = particlePositions[i * 3 + 2];

    // Top point
    lineVerts.push_back(x);
    lineVerts.push_back(y + rainLen);
    lineVerts.push_back(z);

    // Bottom point
    lineVerts.push_back(x);
    lineVerts.push_back(y);
    lineVerts.push_back(z);
  }

  // Upload to VBO
  GLuint VBO;
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO[0]);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_STREAM_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

  // Uniforms
  const float* p = ctx->camera->getProjectionMatrix();
  GLint pmatLoc = glGetUniformLocation(programId, "Projection");
  glUniformMatrix4fv(pmatLoc, 1, GL_FALSE, p);

  const float* v = ctx->camera->getViewMatrix();
  GLint vmatLoc = glGetUniformLocation(programId, "ViewMatrix");
  glUniformMatrix4fv(vmatLoc, 1, GL_FALSE, v);

  // Draw
  glDrawArrays(GL_LINES, 0, lineVerts.size() / 3);

  glDeleteBuffers(1, &VBO);  // Clean up VBO for this frame
  glUseProgram(0);
}
