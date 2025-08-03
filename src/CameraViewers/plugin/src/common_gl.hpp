#pragma once

#include <optional>
#include <memory>

// #include <glad/glad.h>
#include <glbinding/gl45core/gl.h>
using namespace gl;

struct Shader {
  GLuint id;

  ~Shader();
};

struct VAO {
  GLuint id;
  GLuint vbo;

  ~VAO();
};

class ExternalTexture {
private:
  std::optional<GLuint> id{};
  std::optional<GLuint> memory_id{};
  GLsizei width{ 0 };
  GLsizei height{ 0 };

public:
  ExternalTexture() = delete;
  ExternalTexture(GLuint _id, GLuint _memory_id, GLsizei _width, GLsizei _height);
  ExternalTexture(const ExternalTexture&) = delete;
  ExternalTexture(ExternalTexture&& rhs) noexcept;

  ExternalTexture& operator=(const ExternalTexture&) = delete;
  ExternalTexture& operator=(ExternalTexture&& rhs) noexcept;

  auto getId() const { return *id; }

  auto getWidth() const { return width; }
  auto getHeight() const { return height; }

  ~ExternalTexture();
};

class Semaphore {
private:
  std::optional<GLuint> id{};

public:
  Semaphore(std::optional<GLuint> _id = {}) : id{_id} {}
  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&& rhs);

  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore& operator=(Semaphore&& rhs);

  void wait(const ExternalTexture& texture);
  void signal(const ExternalTexture& texture);

  GLuint getId() const;

  ~Semaphore();
};

class Framebuffer {
private:
  GLuint id{};
  GLuint depth_tex_id{};

  GLsizei width{}, height{};

  bool reading_bound{ false };
  bool drawing_bound{ false };

public:
  Framebuffer() = default;
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  explicit Framebuffer(const ExternalTexture& texture);

  void bind(bool reading = true, bool drawing = true);
  void unbind(bool reading = true, bool drawing = true);

  void blit_to_screen(GLint screen_width, GLint screen_height);

  ~Framebuffer();
};

std::string get_driver_and_devices();

struct ConnectionData;

std::pair<Semaphore, Semaphore> create_semaphores_from_connection_data(
  const std::vector<ConnectionData>& connection_data);

std::unique_ptr<ExternalTexture> create_texture_from_connection_data(const std::vector<ConnectionData>& connection_data);
