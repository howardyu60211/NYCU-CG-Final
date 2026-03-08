#include <iostream>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <string>
#include "context.h"
#include "gl_helper.h"
#include "opengl_context.h"
#include "program.h"
#include "stb_image.h"

class SkyBox {
 public:
  static std::vector<GLfloat> vertices;
  static std::vector<GLuint> indices;
  GLuint VAO = 0;
  GLuint VBO = 0;
  GLuint EBO = 0;

  void setup() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);

    glBindVertexArray(0);
  }
};

std::vector<GLfloat> SkyBox::vertices = {-1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
                                         -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,  1.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f};
std::vector<GLuint> SkyBox::indices = {0, 1, 3, 3, 1, 2, 1, 5, 2, 2, 5, 6, 5, 4, 6, 6, 4, 7,
                                       4, 0, 7, 7, 0, 3, 3, 2, 7, 7, 2, 6, 4, 5, 0, 0, 5, 1};

// Static functions removed as logic is moved to SkyboxProgram::load()

bool SkyboxProgram::load() {
  programId = quickCreateProgram(vertProgramFile, fragProgramFIle);
  if (programId == 0) {
    return false;
  }

  SkyBox* sb = new SkyBox();
  sb->setup();

  VAO = new GLuint[1];
  VAO[0] = sb->VAO;

  // Conversion Logic
  unsigned int captureFBO, captureRBO;
  glGenFramebuffers(1, &captureFBO);
  glGenRenderbuffers(1, &captureRBO);

  glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
  glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

  unsigned int envCubemap;
  glGenTextures(1, &envCubemap);
  glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
  for (unsigned int i = 0; i < 6; ++i) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
  glm::mat4 captureViews[] = {
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};

  // Load 2D texture
  stbi_set_flip_vertically_on_load(true);
  int width, height, nrComponents;
  // Force load 3 channels? Or handle 4. Let's handle whatever is there.
  unsigned char* data = stbi_load("assets/skybox/night.png", &width, &height, &nrComponents, 0);
  unsigned int hdrTexture;
  if (data) {
    glGenTextures(1, &hdrTexture);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    GLenum format = GL_RGB;
    if (nrComponents == 4)
      format = GL_RGBA;
    else if (nrComponents == 1)
      format = GL_RED;

    // Set alignment to 1 because STBI data is tightly packed
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    // Restore alignment default
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
  } else {
    std::cout << "Failed to load HDR image." << std::endl;
    return false;
  }

  // Convert
  GLuint captureProgram =
      quickCreateProgram("assets/shaders/cubemap.vert", "assets/shaders/equirectangular_to_cubemap.frag");
  if (captureProgram == 0) {
    return false;
  }
  glUseProgram(captureProgram);
  glUniform1i(glGetUniformLocation(captureProgram, "equirectangularMap"), 0);
  glUniformMatrix4fv(glGetUniformLocation(captureProgram, "projection"), 1, GL_FALSE,
                     glm::value_ptr(captureProjection));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, hdrTexture);

  glViewport(0, 0, 512, 512);
  glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

  for (unsigned int i = 0; i < 6; ++i) {
    glUniformMatrix4fv(glGetUniformLocation(captureProgram, "view"), 1, GL_FALSE, glm::value_ptr(captureViews[i]));
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexArray(VAO[0]);  // Cube mesh
    glDrawElements(GL_TRIANGLES, (GLsizei)SkyBox::indices.size(), GL_UNSIGNED_INT, (void*)0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Mipmaps
  glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  ctx->cubemapTexture = envCubemap;

  // Restore Default Framebuffer viewport (assuming window size)
  int w, h;
  glfwGetFramebufferSize(OpenGLContext::getWindow(), &w, &h);
  glViewport(0, 0, w, h);

  glUseProgram(programId);
  GLint loc = glGetUniformLocation(programId, "skybox");
  if (loc >= 0) glUniform1i(loc, 0);
  glUseProgram(0);

  return true;
}

void SkyboxProgram::doMainLoop() {
  glDepthFunc(GL_LEQUAL);  // ensure skybox rendered behind
  glUseProgram(programId);

  const float* p = ctx->camera->getProjectionMatrix();
  GLint pmatLoc = glGetUniformLocation(programId, "Projection");
  glUniformMatrix4fv(pmatLoc, 1, GL_FALSE, p);

  const float* v = ctx->camera->getViewMatrix();
  GLint vmatLoc = glGetUniformLocation(programId, "ViewMatrix");

  glm::mat4 view = glm::make_mat4(v);
  view[3][0] = 0.0f;
  view[3][1] = 0.0f;
  view[3][2] = 0.0f;
  const float* viewPtr = glm::value_ptr(view);
  glUniformMatrix4fv(vmatLoc, 1, GL_FALSE, viewPtr);

  glBindVertexArray(VAO[0]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ctx->cubemapTexture);
  glDepthMask(GL_FALSE);
  glDrawElements(GL_TRIANGLES, (GLsizei)SkyBox::indices.size(), GL_UNSIGNED_INT, (void*)0);
  glDepthMask(GL_TRUE);
  glBindVertexArray(0);

  glUseProgram(0);
  glDepthFunc(GL_LESS);
}
