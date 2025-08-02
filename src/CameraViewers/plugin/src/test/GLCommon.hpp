#include <optional>

// #include <glad/glad.h>
#include <glbinding/gl45core/gl.h>
using namespace gl;

constexpr std::size_t WIDTH = 800;
constexpr std::size_t HEIGHT = 600;

struct Shader {
  GLuint id;

  ~Shader() {
    glDeleteProgram(id);
  }
};

struct VAO {
  GLuint id;
  GLuint vbo;

  ~VAO() {
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &id);
  }
};

class ExternalTexture {
private:
  std::optional<GLuint> id{};
  std::optional<GLuint> memory_id{};
  GLsizei width{ 0 };
  GLsizei height{ 0 };

public:
  ExternalTexture() = delete;
  ExternalTexture(GLuint _id, GLuint _memory_id, GLsizei _width, GLsizei _height)
    : id{ _id }, memory_id{ _memory_id }, width { _width }, height{ _height } {}
  ExternalTexture(const ExternalTexture&) = delete;
  ExternalTexture(ExternalTexture&& rhs) noexcept
  {
    std::swap(id, rhs.id);
    std::swap(memory_id, rhs.memory_id);
    std::swap(width, rhs.width);
    std::swap(height, rhs.height);
  }

  ExternalTexture& operator=(const ExternalTexture&) = delete;
  ExternalTexture& operator=(ExternalTexture&& rhs) noexcept
  {
    std::swap(id, rhs.id);
    std::swap(memory_id, rhs.memory_id);
    std::swap(width, rhs.width);
    std::swap(height, rhs.height);
    return *this;
  }

  auto getId() const { return *id; }

  auto getWidth() const { return width; }
  auto getHeight() const { return height; }

  ~ExternalTexture() {
    if (id)
      glDeleteTextures(1, &*id);
    if (memory_id)
      glDeleteMemoryObjectsEXT(1, &*memory_id);
  }
};

class Semaphore {
private:
  std::optional<GLuint> id{};

public:
  Semaphore(std::optional<GLuint> _id = {}) : id{_id} {}
  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&& rhs) {
    std::swap(id, rhs.id);
  }

  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore& operator=(Semaphore&& rhs) {
    std::swap(id, rhs.id);
    return *this;
  }

  void wait(const ExternalTexture& texture) {
    // GLenum src_layout = GL_LAYOUT_COLOR_ATTACHMENT_EXT; // We will be using the texture as framebuffer backing
    // Our current client written using Vulkano can only handle the general layout
    GLenum src_layout = GL_LAYOUT_GENERAL_EXT;
    const std::array textures{ texture.getId() };
    glWaitSemaphoreEXT(*id, 0, nullptr, 1, textures.data(), &src_layout);
  }

  void signal(const ExternalTexture& texture) {
    GLenum dst_layout = GL_LAYOUT_GENERAL_EXT;
    // GLenum dst_layout = GL_LAYOUT_SHADER_READ_ONLY_EXT; // A shader will be using the texture next on the Vulkan side
    const std::array textures{ texture.getId() };
    glSignalSemaphoreEXT(*id, 0, nullptr, 1, textures.data(), &dst_layout);
  }

  GLuint getId() const {
    return *id;
  }

  ~Semaphore() {
    if (id)
      glDeleteSemaphoresEXT(1, &(*id));
  }
};

class Framebuffer {
private:
  GLuint id{};
  GLuint depth_tex_id{};

  GLsizei width{}, height{};

  static constexpr GLenum bind_state_to_framebuffer_target(bool reading, bool drawing)
  {
    if (reading && drawing || !reading && !drawing)
      return GL_FRAMEBUFFER;

    if (reading)
      return GL_READ_FRAMEBUFFER;

    return GL_DRAW_FRAMEBUFFER;
  }

  bool reading_bound{ false };
  bool drawing_bound{ false };

public:
  Framebuffer() = default;
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  explicit Framebuffer(const ExternalTexture& texture)
    : width{ texture.getWidth() }, height{ texture.getHeight() }
  {
    glGenFramebuffers(1, &id);
    glBindFramebuffer(GL_FRAMEBUFFER, id);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.getId(), 0);

    // Depthbuffer
    glGenRenderbuffers(1, &depth_tex_id);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_tex_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_tex_id);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error{"Failed to create framebuffer"};

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bind(bool reading = true, bool drawing = true)
  {
    if (!reading && !drawing || reading_bound && drawing_bound)
      return;

    // If the current state is the desired state, we can exit early
    if (reading == reading_bound && drawing == drawing_bound)
      return;

    glBindFramebuffer(bind_state_to_framebuffer_target(reading, drawing), id);

    reading_bound = reading;
    drawing_bound = drawing;
  }

  void unbind(bool reading = true, bool drawing = true)
  {
    if (!reading && !drawing || !reading_bound && !drawing_bound)
      return;

    // If the current state is the desired state, we can exit early
    if (reading == !reading_bound && drawing == !drawing_bound)
      return;

    glBindFramebuffer(bind_state_to_framebuffer_target(reading, drawing), 0);

    reading_bound = !reading;
    drawing_bound = !drawing;
  }

  void blit_to_screen(GLint screen_width, GLint screen_height)
  {
    bind(true, false);
    unbind(false, true);
    glBlitFramebuffer(0, 0, width, height, 0, 0, screen_width, screen_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    unbind();
  }

  ~Framebuffer()
  {
    bind();
    glDeleteRenderbuffers(1, &depth_tex_id);
    glDeleteFramebuffers(1, &id);
    unbind();
  }
};

std::unique_ptr<Shader> create_shader() {
  GLuint vs_shader{ glCreateShader(GL_VERTEX_SHADER) }, fs_shader{ glCreateShader(GL_FRAGMENT_SHADER) };
  constexpr std::array vs_shader_sources{ vs_shader_source.data() }, fs_shader_sources{ fs_shader_source.data() };
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
    std::cout << std::format("ERROR: Vertex shader compilation failed:\n{}", std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  glGetShaderiv(fs_shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    glGetShaderInfoLog(fs_shader, 512, &error_str_len, infoLog.data());
    std::cout << std::format("ERROR: Fragment shader compilation failed:\n{}", std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  GLuint shader_id{ glCreateProgram() };
  glAttachShader(shader_id, vs_shader);
  glAttachShader(shader_id, fs_shader);
  glLinkProgram(shader_id);

  glGetProgramiv(shader_id, GL_LINK_STATUS, &success);
  if (!success)
  {
    glGetProgramInfoLog(shader_id, 512, &error_str_len, infoLog.data());
    std::cout << std::format("ERROR: Shader program link failed:\n{}", std::string_view{infoLog.data(), static_cast<std::size_t>(error_str_len)}) << std::endl;
    return {};
  }

  return std::make_unique<Shader>(shader_id);
}

std::unique_ptr<VAO> create_plane() {
  static constexpr std::array plane_vertices{
    1.f, 1.f,   // top right
    -1.f, 1.f,   // top left
    -1.f, -1.f, // bottom left
    -1.f, -1.f, // bottom left
    1.f, -1.f,  // bottom right
    1.f, 1.f   // top right
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