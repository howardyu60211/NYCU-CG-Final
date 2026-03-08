project(HW2 C CXX)

set(HW2_SOURCE
  ${HW2_SOURCE_DIR}/camera.cpp
  ${HW2_SOURCE_DIR}/gl_helper.cpp
  ${HW2_SOURCE_DIR}/main.cpp
  ${HW2_SOURCE_DIR}/model.cpp
  ${HW2_SOURCE_DIR}/opengl_context.cpp
  ${HW2_SOURCE_DIR}/Programs/example.cpp

  ${HW2_SOURCE_DIR}/Programs/light.cpp
  ${HW2_SOURCE_DIR}/Programs/shadow.cpp
  ${HW2_SOURCE_DIR}/Programs/skybox.cpp
  ${HW2_SOURCE_DIR}/Programs/rain.cpp
  ${HW2_SOURCE_DIR}/Programs/smoke.cpp
  ${HW2_SOURCE_DIR}/Programs/MiniMapProgram.cpp
  ${HW2_SOURCE_DIR}/Programs/GeometryProgram.cpp
  ${HW2_SOURCE_DIR}/Programs/bloom.cpp
  ${HW2_SOURCE_DIR}/car.cpp
)

add_executable(RacingGame ${HW2_SOURCE} ${HW2_HEADER})

target_include_directories(RacingGame PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/tinyobjloader
    ${CMAKE_SOURCE_DIR}/extern/tinygltf
    ${CMAKE_SOURCE_DIR}/extern/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HW2_SOURCE_DIR}/../include
)

add_dependencies(RacingGame glad glfw glm stb)
# Can include glfw and glad in arbitrary order
target_compile_definitions(RacingGame PRIVATE GLFW_INCLUDE_NONE)
# More warnings
if (NOT MSVC)
  target_compile_options(RacingGame
    PRIVATE "-Wall"
    PRIVATE "-Wextra"
    PRIVATE "-Wpedantic"
  )
endif()
# Prefer std c++20, at least need c++17 to compile
set_target_properties(RacingGame PROPERTIES
  CXX_STANDARD 20
  CXX_EXTENSIONS OFF
)

target_link_libraries(RacingGame
  PRIVATE glad
  PRIVATE glfw
  PRIVATE stb
)

if (TARGET glm::glm_shared)
  target_link_libraries(RacingGame PRIVATE glm::glm_shared)
elseif(TARGET glm::glm_static)
  target_link_libraries(RacingGame PRIVATE glm::glm_static)
else()
  target_link_libraries(RacingGame PRIVATE glm::glm)
endif()
