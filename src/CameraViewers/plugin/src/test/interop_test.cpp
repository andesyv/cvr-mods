#include <iostream>
#include <chrono>
#include <map>
#include <string_view>
#include <array>
#include <memory>
#include <filesystem>
#include <ranges>
#include <regex>
#include <thread>
#include <random>
#include <future>
#include <unordered_set>

// #include <glad/glad.h>
#include <glbinding/glbinding.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include "Resources.hpp"
#include "common_gl.hpp"
#include "ipc.hpp"

std::unique_ptr<Shader> create_test_shader()
{
  GLuint vs_shader{glCreateShader(GL_VERTEX_SHADER)}, fs_shader{glCreateShader(GL_FRAGMENT_SHADER)};
  constexpr std::array vs_shader_sources{vs_shader_source.data()}, fs_shader_sources{fs_shader_source.data()};
  glShaderSource(vs_shader, 1, vs_shader_sources.data(), nullptr);
  glCompileShader(vs_shader);
  glShaderSource(fs_shader, 1, fs_shader_sources.data(), nullptr);
  glCompileShader(fs_shader);

  int success;
  std::string infoLog{};
  GLsizei error_str_len{};
  infoLog.resize(512);
  glGetShaderiv(vs_shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    glGetShaderInfoLog(vs_shader, 512, &error_str_len, infoLog.data());
    std::cout << std::format("ERROR: Vertex shader compilation failed:\n{}",
                             std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  glGetShaderiv(fs_shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    glGetShaderInfoLog(fs_shader, 512, &error_str_len, infoLog.data());
    std::cout << std::format("ERROR: Fragment shader compilation failed:\n{}",
                             std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  GLuint shader_id{glCreateProgram()};
  glAttachShader(shader_id, vs_shader);
  glAttachShader(shader_id, fs_shader);
  glLinkProgram(shader_id);

  glGetProgramiv(shader_id, GL_LINK_STATUS, &success);
  if (!success)
  {
    glGetProgramInfoLog(shader_id, 512, &error_str_len, infoLog.data());
    std::cout << std::format("ERROR: Shader program link failed:\n{}",
                             std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  return std::make_unique<Shader>(shader_id);
}

std::unique_ptr<VAO> create_screen_spaced_plane()
{
  static constexpr std::array plane_vertices{
    1.f, 1.f, // top right
    -1.f, 1.f, // top left
    -1.f, -1.f, // bottom left
    -1.f, -1.f, // bottom left
    1.f, -1.f, // bottom right
    1.f, 1.f // top right
  };

  unsigned int plane_vbo, plane_vao;
  glGenVertexArrays(1, &plane_vao);
  glGenBuffers(1, &plane_vbo);

  glBindVertexArray(plane_vao);

  glBindBuffer(GL_ARRAY_BUFFER, plane_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(plane_vertices), plane_vertices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);

  return std::make_unique<VAO>(plane_vao, plane_vbo);
}

int main()
{
  const auto program_start = std::chrono::steady_clock::now();

  // glfw: initialize and configure
  // ------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

  GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Viewer test", NULL, NULL);

  if (window == NULL)
  {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  constexpr float FOV = 45.f;
  static auto p_mat{glm::perspective(FOV, 800.f / 600.f, 0.1f, 100.f)};
  static int window_width{WIDTH}, window_height{HEIGHT};
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height)
  {
    glViewport(0, 0, width, height);
    p_mat = glm::perspective(FOV, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    window_width = width;
    window_height = height;
  });

  // if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  // {
  //   std::cout << "Failed to initialize GLAD" << std::endl;
  //   return -1;
  // }
  glbinding::initialize(glfwGetProcAddress);

  const auto getGLString = [](GLenum type)
  {
    return std::string_view{reinterpret_cast<const char*>(glGetString(GL_RENDERER))};
  };

  std::cout << std::format("Running OpenGL version {}, on a {}", getGLString(GL_VERSION), getGLString(GL_RENDERER)) <<
    std::endl;

  std::unordered_set<std::string> supported_extensions;
  GLint supported_extension_count;
  glGetIntegerv(GL_NUM_EXTENSIONS, &supported_extension_count);
  for (GLuint i{0}; i < supported_extension_count; ++i)
    supported_extensions.insert(std::string{reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i))});

  if (!supported_extensions.contains("GL_EXT_memory_object") || !supported_extensions.contains("GL_EXT_semaphore"))
  {
    std::cout << "Extension GL_EXT_memory_object or GL_EXT_semaphore is missing." << std::endl;
    glfwTerminate();
    return -1;
  }

#ifdef _WIN32
  if (!supported_extensions.contains("GL_EXT_memory_object_win32") || !supported_extensions.contains("GL_EXT_semaphore_win32"))
  {
    std::cout << "Extension GL_EXT_memory_object_win32 or GL_EXT_semaphore_win32 is missing." << std::endl;
    glfwTerminate();
    return -1;
  }
#elif __linux__
  if (!supported_extensions.contains("GL_EXT_memory_object_fd") || !supported_extensions.
    contains("GL_EXT_semaphore_fd"))
  {
    std::cout << "Extension GL_EXT_memory_object_fd or GL_EXT_semaphore_fd is missing." << std::endl;
    glfwTerminate();
    return -1;
  }
#endif

  // Fetch driver and devices
  const std::string driver_and_devices{ get_driver_and_devices() };
  std::cout << driver_and_devices << std::endl;

  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                            const GLchar* message, const void* userParam)
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
    const auto msg = std::format("OpenGL: {{ source: {}, type: {}, severity: {}, message: {} }}", sources.at(source),
                                 types.at(type), severities.at(severity), msg_str);
    std::cout << msg << std::endl;
    if (type != GL_DEBUG_TYPE_PERFORMANCE && severity != GL_DEBUG_SEVERITY_NOTIFICATION && severity !=
      GL_DEBUG_SEVERITY_LOW)
      throw std::runtime_error{msg};
  }, nullptr);

  {
    // Setup subprocess + pipe
    if (!std::filesystem::exists(std::filesystem::path{VIEWER_PATH}))
    {
      std::cout << "Could not find viewer executable!" << std::endl;
      return -1;
    }

#ifdef _WIN32
    auto [child_std_read, child_std_write] = create_pipe();
    if (!child_std_read)
      return -1;

    auto process = create_child_process(VIEWER_PATH, std::move(child_std_write));
    if (!process)
    {
      std::cout << "Failed to spawn process" << std::endl;
      return -1;
    }
    std::vector<ConnectionData> connection_data{};
    for (unsigned int attempts { 0u }; attempts < 10u && connection_data.empty(); ++attempts) {
      connection_data = fetch_connection_data_from_child_channel(child_std_read);
      if (!connection_data.empty())
        break;

      std::this_thread::sleep_for(std::chrono::seconds{1u});
    }
    if (connection_data.empty())
    {
      std::cout << "Failed to fetch connection data after 10 attempts" << std::endl;
      return -1;
    }
#elif __linux__
    std::vector<ConnectionData> connection_data{};
    ChildProcess child_process;
    IPCSemaphore child_process_semaphore{};
    if (auto result{init_child_process_and_fetch_connection_data(VIEWER_PATH, driver_and_devices)})
    {
      connection_data = std::move(std::get<0>(*result));
      child_process = std::move(std::get<1>(*result));
      child_process_semaphore = std::move(std::get<2>(*result));
    }
    else
    {
      return -1;
    }
#endif

    std::cout << "Found connection data: " << std::endl;
    for (const auto& data : connection_data)
      std::cout << std::format("{{ type: {}, identifier: {} }}",
                               data.type == ConnectionData::Type::Semaphore ? "Semaphore" : "Image",
                               data.identifier) << std::endl;

    auto [begin_semaphore, end_semaphore] = create_semaphores_from_connection_data(connection_data);
    const auto shared_texture = create_texture_from_connection_data(connection_data);
    if (!shared_texture)
      throw std::runtime_error{"Couldn't fetch shared texture"};

    // Setup scene:
    const auto shader{create_test_shader()};
    if (!shader)
      return -1;

    glUseProgram(shader->id);
    auto plane{create_screen_spaced_plane()};

    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    Framebuffer shared_texture_framebuffer{*shared_texture};

    auto last_tp{std::chrono::steady_clock::now()};
    std::cout << std::format("Took {}ms to initialize",
                             std::chrono::duration_cast<std::chrono::microseconds>(last_tp - program_start).count() *
                             0.001) << std::endl;

    float t{0.f};
    // We let the child process know we are ready to start with the rendering by issuing a first signal
    child_process_semaphore.signal();
    while (!glfwWindowShouldClose(window))
    {
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

      // std::this_thread::sleep_for(std::chrono::seconds{1});

      const auto current_tp{std::chrono::steady_clock::now()};
      const auto delta_t = std::chrono::duration_cast<std::chrono::milliseconds>(current_tp - last_tp).count() * 0.001;
      last_tp = current_tp;

      t += delta_t * 0.3;

      const auto v_mat{
        glm::lookAt(glm::vec3{std::sin(t) * 10.f, 1.f, std::cos(t) * 10.f}, {}, glm::vec3{0.f, 1.f, 0.f})
      };
      const auto mvp{glm::inverse(p_mat * v_mat)};

      // We don't have to do any synchronisation to set up the global GL drawing state:
      shared_texture_framebuffer.bind();
      glClearColor(0.2f, 0.3f, t - std::floor(t), 1.0f);
      glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
      glUniform1f(1, t * 10.0);

      // Wait for rendering to be ready
      child_process_semaphore.wait();
      begin_semaphore.wait(*shared_texture);

      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 6);

      // Blit the framebuffer to the screen to verify what's being passed to the viewer program corresponds with what
      // was written
      shared_texture_framebuffer.blit_to_screen(window_width, window_height);

      // Tell the OpenGL server we're done with rendering
      end_semaphore.signal(*shared_texture);
      // OpenGL usually chooses itself when to flush commands to the GPU, but since the client is waiting for
      // synchronization from us, we need to explicitly flush commands to the GPU every frame.
      glFlush();
      // Now tell the client we're done on our end
      child_process_semaphore.signal();

#ifdef _WIN32
      print_child_pipe(extract_from_child_pipe(*child_output_channel));
#endif

      glfwSwapBuffers(window);
      glfwPollEvents();
    }


#ifdef _WIN32
    // Notify the child process it should close
    if (!PostThreadMessageA(process->dwThreadId, WM_QUIT, NULL, NULL))
      std::cout << "Failed to notify the child process it should close!" << std::endl;

    CloseHandle(process->hProcess);
    CloseHandle(process->hThread);
#elif __linux__
    // if (pclose(child_output_channel) == -1)
    // {
    //   std::cerr << "Failed to close child process" << std::endl;
    //   return -1;
    // }
    child_process_semaphore = {};
    // Close the semaphore (possibly forcing the client to run into a "pipe destroyed" signal)
    if (auto child_process_return_code{child_process.close()}; child_process_return_code < 0)
    {
      std::cerr << "Child process successfully closed but returned error code: " << child_process_return_code <<
        std::endl;
      return -1;
    }
#endif

    glUseProgram(0);
    glBindVertexArray(0);
  }

  glfwTerminate();
  return 0;
}
