cmake_minimum_required(VERSION 3.16.0)

set(LIB_DIR ${CMAKE_SOURCE_DIR}/../../../lib)

option(BUILD_TEST "Build test application" ON)

project(unity-plugin)

# Project settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# $<$<CONFIG:>:> hacky generator that removes any multi-config folders generated (e.g. msvc)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<$<CONFIG:>:>)

# Output executable
add_library(unity-plugin SHARED)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "com.andough.cameraviewers.renderingplugin")

target_include_directories(unity-plugin PRIVATE ${LIB_DIR}/UnityPluginAPI)

if(${BUILD_TEST})
  add_executable(test)
  
  if (WIN32)
    target_compile_definitions(test PRIVATE _WIN32)
    target_compile_options(test PRIVATE /diagnostics:caret)
  endif()
  target_compile_definitions(test PRIVATE ROOT_DIR="${CMAKE_SOURCE_DIR}")

  # GLAD OpenGL loader
  target_include_directories(test PRIVATE ${LIB_DIR}/glad/include)
  target_sources(test PRIVATE ${LIB_DIR}/glad/src/glad.c)

  # GLFW
  find_package(glfw3 CONFIG REQUIRED)
  target_link_libraries(test PRIVATE glfw)

  # GLM
  find_package(glm CONFIG REQUIRED)
  target_link_libraries(test PRIVATE glm::glm)
endif()

# Source files
add_subdirectory(src)
