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

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <cstdio>
#include <fcntl.h>
// #include <sys/syscall.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cerrno>
#endif

#include "Resources.hpp"
#include "GLCommon.hpp"

struct ConnectionData
{
  enum class Type
  {
    Semaphore,
    Image
  };

  Type type;
  std::string identifier;
#ifdef _WIN32
  HANDLE handle;
#elif __linux__
  int handle;
#endif
  std::string handle_type;
  std::optional<std::size_t> memory_allocation_size;
  std::optional<std::string> memory_image_format;
};

#ifdef _WIN32
std::pair<std::shared_ptr<HANDLE>, std::shared_ptr<HANDLE>> create_pipe() {
  SECURITY_ATTRIBUTES security_attributes {
    .nLength = sizeof(SECURITY_ATTRIBUTES),
    .lpSecurityDescriptor = NULL,
    .bInheritHandle = TRUE,
  };

  const auto deleter = [](auto* p){
    if (p) {
      CloseHandle(*p);
      delete p;
    }
  };
  std::shared_ptr<HANDLE> child_out_write{new HANDLE{NULL}, deleter}, child_out_read{new HANDLE{NULL}, deleter};
  if (!CreatePipe(child_out_read.get(), child_out_write.get(), &security_attributes, 0) )
  {
    std::cout << "Failed to create pipe!" << std::endl;
    return {};
  }

  if (!SetHandleInformation(*child_out_read, HANDLE_FLAG_INHERIT, 0))
  {
    std::cout << "Failed to set pipe flag!" << std::endl;
    return {};
  }

  DWORD mode = PIPE_NOWAIT;
  if (!SetNamedPipeHandleState(*child_out_read, &mode, NULL, NULL))
  {
    std::cout << "Failed to set pipe flag!" << std::endl;
    return {};
  }


  return std::make_pair(std::move(child_out_read), std::move(child_out_write));
}

std::optional<PROCESS_INFORMATION> create_child_process(std::string_view cmd, std::shared_ptr<HANDLE> std_out) {
  PROCESS_INFORMATION p_info;
  STARTUPINFO s_info;

  ZeroMemory( &p_info, sizeof(PROCESS_INFORMATION) );
  ZeroMemory( &s_info, sizeof(STARTUPINFO) );

  s_info.cb = sizeof(STARTUPINFO);
  s_info.hStdError = *std_out;
  s_info.hStdOutput = *std_out;
  //  s_info.hStdInput = g_hChildStd_IN_Rd;
  s_info.dwFlags |= STARTF_USESTDHANDLES;

  std::string mutableCmd{ cmd };

  if (!CreateProcess(NULL,
    mutableCmd.data(),
    NULL,     // process security attributes
    NULL,     // primary thread security attributes
    TRUE,     // handles are inherited
    0,        // creation flags
    NULL,     // use parent's environment
    NULL,     // use parent's current directory
    &s_info,
    &p_info
  ))
    return {};

  return p_info;
}

std::string extract_output_from_child_pipe(HANDLE* pipe) {
  static std::string buffer{};
  constexpr std::size_t BUFFER_SIZE = 4096;
  buffer.resize(BUFFER_SIZE);
  DWORD bytesRead;
  std::string out{};
  out.reserve(BUFFER_SIZE);

  while (ReadFile(*pipe, buffer.data(), BUFFER_SIZE, &bytesRead, NULL))
    out.append(std::string_view{buffer.data(), bytesRead});

  return out;
}


std::vector<ConnectionData> fetch_connection_data_from_child_channel(HANDLE* child_pipe)
{
  std::string child_pipe_output{ extract_output_from_child_pipe(child_output_channel) };
  print_child_pipe(child_pipe_output);
  return fetch_connection_data_from_child_pipe_output(child_pipe_output);
}
#elif __linux__
std::string random_alphanumeric_string(std::size_t len)
{
  constexpr std::string_view alphanum_characters =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";
  std::string tmp;
  tmp.reserve(len);

  static std::default_random_engine random_engine(std::random_device{}());
  std::uniform_int_distribution<std::size_t> distribution{0, alphanum_characters.size() - 1};

  for (int i = 0; i < len; ++i)
    tmp += alphanum_characters.at(distribution(random_engine));

  return tmp;
}

std::string extract_from_stdout_stream(FILE* stream)
{
  static std::string buffer{};
  constexpr std::size_t BUFFER_SIZE = 128;
  buffer.resize(BUFFER_SIZE);
  std::string out{};
  // TODO: "fread" blocks forever until stream returns EOF. I need to instead check if there's new data in the stream
  // (using a timeout?) and return early otherwise.
  // https://stackoverflow.com/questions/5616092/non-blocking-call-for-reading-descriptor
  for (auto bytes_read{fread(buffer.data(), sizeof(char), BUFFER_SIZE, stream)}; bytes_read > 0; bytes_read = fread(
         buffer.data(), sizeof(char), BUFFER_SIZE, stream))
    out.append(std::string_view{buffer.data(), bytes_read});
  return out;
}
#endif

std::vector<ConnectionData> fetch_connection_data_from_child_pipe_output(std::string_view child_pipe_output)
{
  const static std::regex line_match{R"(Connection data: \{[^\}]+\})"};
  const static std::regex sub_match{R"(\"[a-zA-Z0-9_^\"]*\")"};
  std::vector<ConnectionData> data;

  for (std::regex_iterator it{child_pipe_output.begin(), child_pipe_output.end(), line_match}; it != decltype(it){}; ++
       it)
  {
    auto match{*it};
    const auto sub_str{match.str()};

    std::regex_iterator sub_match_it{sub_str.begin(), sub_str.end(), sub_match};
    const auto sub_matches = std::ranges::subrange{sub_match_it, decltype(sub_match_it){}} | std::views::transform(
      [](auto match)
      {
        std::string s{match.str()};
        return s.substr(1, s.size() - 2);
      }) | std::ranges::to<std::vector<std::string>>();

#ifdef _WIN32
    static_assert(8 == sizeof(HANDLE), "Program requires 64 bit pointer types");
    static_assert(8 == sizeof(unsigned long long));

    const auto mem_address{ std::stoull(sub_matches.at(3), nullptr, 16) };
    if (mem_address <= 0)
      continue;

    HANDLE handle{ reinterpret_cast<HANDLE>(mem_address) };

    unsigned long handle_info{0u};
    if (GetHandleInformation(handle, &handle_info) == 0) {
      LPTSTR error_msg{ nullptr };
      if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &error_msg,
        0, NULL) == 0)
          throw std::runtime_error{ std::format("Ironically, fetching the error string failed with error code {:x}", GetLastError()) };

      throw std::runtime_error{ std::format("Handle is invalid. Error: {}", error_msg) };
    }
#elif __linux__
    const int handle = std::stoi(sub_matches.at(3));
#endif

    data.push_back(ConnectionData{
      .type = sub_matches.at(0) == "semaphore" ? ConnectionData::Type::Semaphore : ConnectionData::Type::Image,
      .identifier = sub_matches.at(1),
      .handle = handle,
      .handle_type = sub_matches.at(2),
      .memory_allocation_size = 4 < sub_matches.size()
                                  ? std::make_optional<std::size_t>(std::stoull(sub_matches.at(4)))
                                  : std::nullopt,
      .memory_image_format = 5 < sub_matches.size() ? std::make_optional(sub_matches.at(5)) : std::nullopt,
    });
  }

  return data;
}

void print_child_pipe(std::string_view child_pipe_output)
{
  if (child_pipe_output.empty())
    return;

  std::cout << "Child process:" << std::endl;
  for (auto line : child_pipe_output | std::views::split('\n') | std::views::transform([](auto&& subrange)
  {
    return std::string_view{subrange.begin(), subrange.end()};
  }))
    std::cout << "\t\t" << line << std::endl;
}

std::pair<Semaphore, Semaphore> create_semaphores_from_connection_data(
  const std::vector<ConnectionData>& connection_data)
{
  Semaphore begin, end;
  for (const auto& data : connection_data)
  {
    if (data.identifier != "OGL_begin" && data.identifier != "OGL_end")
      continue;

    auto& semaphore{data.identifier == "OGL_begin" ? begin : end};
    GLuint id;
    glGenSemaphoresEXT(1, &id);
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32")
      throw std::logic_error{"Handle type is not implemented"};
    glImportSemaphoreWin32HandleEXT(id, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, data.handle);
#endif
    semaphore = {id};
    if (!glIsSemaphoreEXT(semaphore.getId()))
      throw std::runtime_error{"Semaphore object is invalid!"};
  }

  return {std::move(begin), std::move(end)};
}

std::unique_ptr<ExternalTexture> create_texture_from_connection_data(const std::vector<ConnectionData>& connection_data)
{
  for (const auto& data : connection_data)
  {
    if (data.type != ConnectionData::Type::Image)
      continue;

    GLuint texture_id, memory_id;
    glCreateMemoryObjectsEXT(1, &memory_id);
    if (!data.memory_allocation_size)
      throw std::logic_error{"Memory connection data requires a memory allocation size"};
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryWin32HandleEXT(memory_id, *data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, data.handle);
#endif

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset
    if (data.memory_image_format != "R16G16B16A16_UNORM")
      throw std::logic_error{"Unsupported format"};
    glTextureStorageMem2DEXT(texture_id, 1, GL_RGBA16UI, WIDTH, HEIGHT, memory_id, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!glIsMemoryObjectEXT(texture_id))
      throw std::runtime_error{"Failed to create external memory object!"};

    return std::make_unique<ExternalTexture>(texture_id, memory_id);
  }

  return {};
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
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height)
  {
    glViewport(0, 0, width, height);
    p_mat = glm::perspective(FOV, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
  });

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  const auto getGLString = [](GLenum type)
  {
    return std::string_view{reinterpret_cast<const char*>(glGetString(GL_RENDERER))};
  };

  std::cout << std::format("Running OpenGL version {}, on a {}", getGLString(GL_VERSION), getGLString(GL_RENDERER)) <<
    std::endl;

  if (glGenSemaphoresEXT == nullptr || glCreateMemoryObjectsEXT == nullptr)
  {
    std::cout << "Extension GL_EXT_memory_object or GL_EXT_semaphore is missing." << std::endl;
    glfwTerminate();
    return -1;
  }

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
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION && severity != GL_DEBUG_SEVERITY_LOW)
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
    auto [child_output_channel, child_std_write] = create_pipe();
    if (!child_output_channel)
      return -1;

    auto process = create_child_process(VIEWER_PATH, std::move(child_std_write));
    if (!process)
    {
      std::cout << "Failed to spawn process" << std::endl;
      return -1;
    }
#elif __linux__
    // Create a temp path we will use for the socket connection
    std::error_code ec;
    // 14 characters is probably enough to guarantee uniqueness
    const auto socket_addr_path{
      std::filesystem::temp_directory_path(ec) / std::format("{}.sock", random_alphanumeric_string(14))
    };
    assert(!ec); // We don't expect this to fail very often

    if (std::filesystem::exists(socket_addr_path, ec) || ec)
    {
      std::cerr <<
        "Despite all odds, the unique path dedicated for our socket connection somehow exists or failed. Error code " <<
        ec.value() << std::endl;
      return -1;
    }

    // Create the socket
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
      std::cerr << "Failed to create server socket. Error code: " << errno << std::endl;
      return -1;
    }

    // Set fd to be non-blocking
    // if (fcntl(child_process_fd, F_SETFL, fcntl(child_process_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
    // {
    //   std::cerr << "Failed to set flags for child process" << std::endl;
    //   return -1;
    // }

    sockaddr_un server_addr{
      .sun_family = AF_UNIX,
      .sun_path = {},
    };
    std::strncpy(server_addr.sun_path, socket_addr_path.c_str(), socket_addr_path.string().size());
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(sockaddr_un)) < 0)
    {
      std::cerr << "Failed to bind socket. Error code: " << errno << std::endl;
      return -1;
    }

    if (listen(server_fd, 1) < 0)
    {
      std::cerr << "Failed to set socket to listen. Error code: " << errno << std::endl;
      return -1;
    }

    // The current thread will block while waiting for incoming socket connections. So for debugging purposes we start
    // and observe the child process in a separate thread so we can pipe its output to the standard output
    auto child_process_job = std::async(std::launch::async, [socket_addr_path]()
    {
      const std::string cmd{std::format("{} {}", VIEWER_PATH, socket_addr_path.string())};
      auto child_std_output = popen(cmd.c_str(), "r");
      if (child_std_output == nullptr)
      {
        std::cout << "Failed to spawn child process" << std::endl;
        return -1;
      }

      const auto child_process_fd = fileno(child_std_output);
      if (fcntl(child_process_fd, F_GETFD) < 0)
      {
        std::cerr << "Child process FD is invalid" << std::endl;
        return -1;
      }

      if (fcntl(child_process_fd, F_SETFL, fcntl(child_process_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
      {
        std::cerr << "Failed to set flags for child process" << std::endl;
        return -1;
      }

      while (fcntl(child_process_fd, F_GETFD) > -1)
      {
        auto output{extract_from_stdout_stream(child_std_output)};
        if (!output.empty())
          print_child_pipe(output);
        std::this_thread::sleep_for(std::chrono::seconds{1});
      }

      return 0;
    });

    std::cout << "Waiting for connection data from child process..." << std::endl;
    int client_fd = accept(server_fd, nullptr, nullptr); // Blocks until a connection is made
    // After receiving an incoming connection, delete the address path to prevent other clients from connecting
    if (!std::filesystem::remove(socket_addr_path, ec))
    {
      std::cerr << std::format("Failed to remove socket addr file {}. Error code: {}", socket_addr_path.string(),
                               ec.value()) << std::endl;
      return -1;
    }

    // Read some test data from child
    {
      std::string buffer;
      constexpr std::size_t buffer_size{128};
      buffer.resize(buffer_size);
      ssize_t bytes_read{0};
      for (bytes_read = read(client_fd, buffer.data(), buffer_size); bytes_read > 0; bytes_read = read(
             client_fd, buffer.data(), buffer_size))
      {
        std::cout << std::format("Received data from client: \"{}\"",
                                 std::string_view{buffer.data(), static_cast<std::size_t>(bytes_read)}) << std::endl;
      }

      if (bytes_read < 0)
      {
        std::cerr << std::format("Error received while trying to read from child process socket: {}", errno) <<
          std::endl;
        return -1;
      }
    }

    if (close(client_fd) < 0)
    {
      std::cerr << "Failed to close child process socket:" << errno << std::endl;
      return -1;
    }

    return 0;

    // Set fd to be non-blocking
    // if (fcntl(child_process_fd, F_SETFL, fcntl(child_process_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
    // {
    //   std::cerr << "Failed to set flags for child process" << std::endl;
    //   return -1;
    // }


#endif

    std::vector<ConnectionData> connection_data{};
    // for (unsigned int attempts { 0u }; attempts < 10u && connection_data.empty(); ++attempts) {
    //   // Note: child_output_channel is platform dependent but in all platforms represents some sort of "channel",
    //   // i.e. a directional data stream from the child process to the parent process. On Windows this is a pipe,
    //   // and on Linux based systems this is a socket.
    //   connection_data = fetch_connection_data_from_child_channel(child_output_channel);
    //   if (!connection_data.empty())
    //     break;
    //
    //   std::this_thread::sleep_for(std::chrono::seconds{1u});
    // }
    if (connection_data.empty())
    {
      std::cout << "Failed to fetch connection data after 10 attempts" << std::endl;
      return -1;
    }

    std::cout << "Found connection data: " << std::endl;
    for (const auto& data : connection_data)
      std::cout << std::format("{{ type: {}, identifier: {} }}",
                               data.type == ConnectionData::Type::Semaphore ? "Semaphore" : "Image",
                               data.identifier) << std::endl;

    // #if __linux__
    //     // Note: Initially I wanted to use pidfd_getfd because it's easier to use. However, due to security restrictions
    //     // around the non-safe pidfd_getfd syscall, a process is required to disable some extra security parameters
    //     // for this call to succeed.
    //     // TODO: Pass FD using Unix sockets :(
    //     for (auto& data : connection_data)
    //     {
    //       if (auto new_fd{syscall(SYS_pidfd_getfd, child_process_fd, data.handle, 0)}; new_fd >= 0)
    //       {
    //         data.handle = static_cast<int>(new_fd);
    //       }
    //       else
    //       {
    //         std::cerr << std::format("Failed to steal FD handle from child process with error: {}", errno) << std::endl;
    //         return -1;
    //       }
    //     }
    // #endif

    auto [begin_semaphore, end_semaphore] = create_semaphores_from_connection_data(connection_data);
    const auto shared_texture = create_texture_from_connection_data(connection_data);
    if (!shared_texture)
      throw std::runtime_error{"Couldn't fetch shared texture"};

    // Setup scene:
    const auto shader{create_shader()};
    if (!shader)
      return -1;

    glUseProgram(shader->id);
    auto plane{create_plane()};

    Framebuffer shared_texture_framebuffer{*shared_texture};

    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    end_semaphore.signal(*shared_texture);
    glFlush();

    auto last_tp{std::chrono::steady_clock::now()};
    std::cout << std::format("Took {}ms to initialize",
                             std::chrono::duration_cast<std::chrono::microseconds>(last_tp - program_start).count() *
                             0.001) << std::endl;

    float t{0.f};
    while (!glfwWindowShouldClose(window))
    {
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

      const auto current_tp{std::chrono::steady_clock::now()};
      const auto delta_t = std::chrono::duration_cast<std::chrono::milliseconds>(current_tp - last_tp).count() * 0.001;
      last_tp = current_tp;

      t += delta_t * 0.3;

      const auto v_mat{
        glm::lookAt(glm::vec3{std::sin(t) * 10.f, 1.f, std::cos(t) * 10.f}, {}, glm::vec3{0.f, 1.f, 0.f})
      };
      const auto mvp{glm::inverse(p_mat * v_mat)};

      // Wait for rendering to be ready
      begin_semaphore.wait(*shared_texture);
      // glFlush();

      shared_texture_framebuffer.bind();
      glClearColor(0.2f, 0.3f, t, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
      glUniform1f(1, t * 3.0);

      glDrawArrays(GL_TRIANGLES, 0, 6);
      shared_texture_framebuffer.unbind();

      // Signal the Vulkan app that OpenGL rendering is done
      end_semaphore.signal(*shared_texture);

      // OpenGL usually chooses itself when to flush commands to the GPU, but since the Vulkan implementation
      // is waiting for synchronization from OpenGL we need to explicitly flush commands to the GPU every frame.
      glFlush();

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
#endif

    glUseProgram(0);
    glBindVertexArray(0);
  }

  glfwTerminate();
  return 0;
}
