#include <glad/glad.h>

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

struct ExternalTexture {
  GLuint id;
  GLuint memory_id;

  ExternalTexture(GLuint _id, GLuint _memory_id)
    : id{_id}, memory_id{_memory_id} {}

  ~ExternalTexture() {
    glDeleteTextures(1, &id);
  }
};

struct Semaphore {
  std::optional<GLuint> id{};

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
    GLenum src_layout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
    glWaitSemaphoreEXT(*id, 0, nullptr, 1, &texture.id, &src_layout);
  }

  void signal(const ExternalTexture& texture) {
    GLenum dst_layout = GL_LAYOUT_SHADER_READ_ONLY_EXT;
    glSignalSemaphoreEXT(*id, 0, nullptr, 1, &texture.id, &dst_layout);
  }

  ~Semaphore() {
    if (id)
      glDeleteSemaphoresEXT(1, &(*id));
  }
};

struct Framebuffer {
  GLuint id;
  GLuint depth_tex_id;

  Framebuffer(const ExternalTexture& texture) {
    glGenFramebuffers(1, &id);
    glBindFramebuffer(GL_FRAMEBUFFER, id);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);

    // Depthbuffer
    glGenRenderbuffers(1, &depth_tex_id);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_tex_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_tex_id);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error{"Failed to create framebuffer"};    

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, id);
  }

  void unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  ~Framebuffer() {
    glDeleteFramebuffers(1, &id);
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

VAO create_plane() {
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
  
  return { .id = plane_vao, .vbo = plane_vbo };
}