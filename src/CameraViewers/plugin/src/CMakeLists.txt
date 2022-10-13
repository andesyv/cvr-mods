target_sources(unity-plugin PRIVATE main.cpp)

if(${BUILD_TEST})
  target_sources(test PRIVATE test.cpp)

  # GLAD OpenGL loader
  target_include_directories(test PRIVATE ${LIB_DIR}/glad/include)
  target_sources(test PRIVATE ${LIB_DIR}/glad/src/glad.c)

  # GLFW
  find_package(glfw3 CONFIG REQUIRED)
  target_link_libraries(test PRIVATE glfw)
endif()