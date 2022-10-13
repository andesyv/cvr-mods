#include <iostream>
#include <chrono>
#include <map>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

int main()
{
  const auto program_start = std::chrono::steady_clock::now();

  // glfw: initialize and configure
  // ------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

  // glfw window creation
  // --------------------
  GLFWwindow *window = glfwCreateWindow(800, 600, "Viewer test", NULL, NULL);

  if (window == NULL)
  {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);

  glfwSetFramebufferSizeCallback(window, [](GLFWwindow *window, int width, int height)
                                 { glViewport(0, 0, width, height); });

  // glad: load all OpenGL function pointers
  // ---------------------------------------
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  int versionMajor, versionMinor;
  glGetIntegerv(GL_MAJOR_VERSION, &versionMajor);
  glGetIntegerv(GL_MINOR_VERSION, &versionMinor);
  std::cout << std::format("Running OpenGL version: {}.{}", versionMajor, versionMinor) << std::endl;

  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

  glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
  {
        const static std::map<GLenum, std::string> sources{
            {GL_DEBUG_SOURCE_API, "GL_DEBUG_SOURCE_API"},
            {GL_DEBUG_SOURCE_WINDOW_SYSTEM, "GL_DEBUG_SOURCE_WINDOW_SYSTEM"},
            {GL_DEBUG_SOURCE_SHADER_COMPILER, "GL_DEBUG_SOURCE_SHADER_COMPILER"},
            {GL_DEBUG_SOURCE_THIRD_PARTY, "GL_DEBUG_SOURCE_THIRD_PARTY"},
            {GL_DEBUG_SOURCE_APPLICATION, "GL_DEBUG_SOURCE_APPLICATION"},
            {GL_DEBUG_SOURCE_OTHER, "GL_DEBUG_SOURCE_OTHER"}
        };
        const static std::map<GLenum, std::string> types{
            {GL_DEBUG_TYPE_ERROR, "GL_DEBUG_TYPE_ERROR"},
            {GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, "GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR"},
            {GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, "GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR"},
            {GL_DEBUG_TYPE_PORTABILITY, "GL_DEBUG_TYPE_PORTABILITY"},
            {GL_DEBUG_TYPE_PERFORMANCE, "GL_DEBUG_TYPE_PERFORMANCE"},
            {GL_DEBUG_TYPE_MARKER, "GL_DEBUG_TYPE_MARKER"},
            {GL_DEBUG_TYPE_PUSH_GROUP, "GL_DEBUG_TYPE_PUSH_GROUP"},
            {GL_DEBUG_TYPE_POP_GROUP, "GL_DEBUG_TYPE_POP_GROUP"},
            {GL_DEBUG_TYPE_OTHER, "GL_DEBUG_TYPE_OTHER"}
        };
        const static std::map<GLenum, std::string> severities{
            {GL_DEBUG_SEVERITY_HIGH, "GL_DEBUG_SEVERITY_HIGH"},
            {GL_DEBUG_SEVERITY_MEDIUM, "GL_DEBUG_SEVERITY_MEDIUM"},
            {GL_DEBUG_SEVERITY_LOW, "GL_DEBUG_SEVERITY_LOW"},
            {GL_DEBUG_SEVERITY_NOTIFICATION, "GL_DEBUG_SEVERITY_NOTIFICATION"}
        };
        
        const std::string_view msg_str{message, static_cast<std::size_t>(length)};
        std::cout << std::format("OpenGL: {{ source: {}, type: {}, severity: {}, message: {} }}", sources.at(source), types.at(type), severities.at(severity), msg_str) << std::endl;
  }, nullptr);

  auto last_tp{ std::chrono::steady_clock::now() };
  std::cout << std::format("Took {}ms to initialize", std::chrono::duration_cast<std::chrono::microseconds>(last_tp - program_start).count() * 0.001) << std::endl;

  float t{0.f};
  while (!glfwWindowShouldClose(window))
  {
    const auto current_tp{ std::chrono::steady_clock::now() };
    const auto delta_t = std::chrono::duration_cast<std::chrono::milliseconds>(current_tp - last_tp).count() * 0.001;
    last_tp = current_tp;
    
    t += delta_t * 0.1;
    if (1.f < t)
      t -= 1.f;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

    glClearColor(0.2f, 0.3f, t, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}