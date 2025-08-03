#include "common_gl.hpp"
#include "utils.hpp"
#include "ipc.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
  constexpr GLenum bind_state_to_framebuffer_target(bool reading, bool drawing)
  {
    if (reading && drawing || !reading && !drawing)
      return GL_FRAMEBUFFER;

    if (reading)
      return GL_READ_FRAMEBUFFER;

    return GL_DRAW_FRAMEBUFFER;
  }

  constexpr GLenum format_from_vk_format(std::string_view vk_format)
  {
    if (vk_format == "R16G16B16A16_UNORM")
      return GL_RGBA16;
    std::unreachable();
  }
}

Shader::~Shader()
{
  glDeleteProgram(id);
}

VAO::~VAO()
{
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &id);
}

ExternalTexture::ExternalTexture(GLuint _id, GLuint _memory_id, GLsizei _width, GLsizei _height)
  : id{ _id }, memory_id{ _memory_id }, width { _width }, height{ _height }
{}

ExternalTexture::ExternalTexture(ExternalTexture&& rhs) noexcept
{
  std::swap(id, rhs.id);
  std::swap(memory_id, rhs.memory_id);
  std::swap(width, rhs.width);
  std::swap(height, rhs.height);
}

ExternalTexture& ExternalTexture::operator=(ExternalTexture&& rhs) noexcept
{
  std::swap(id, rhs.id);
  std::swap(memory_id, rhs.memory_id);
  std::swap(width, rhs.width);
  std::swap(height, rhs.height);
  return *this;
}

ExternalTexture::~ExternalTexture()
{
  if (id)
    glDeleteTextures(1, &*id);
  if (memory_id)
    glDeleteMemoryObjectsEXT(1, &*memory_id);
}

Semaphore::Semaphore(Semaphore&& rhs)
{
  std::swap(id, rhs.id);
}

Semaphore& Semaphore::operator=(Semaphore&& rhs)
{
  std::swap(id, rhs.id);
  return *this;
}

void Semaphore::wait(const ExternalTexture& texture)
{
  // GLenum src_layout = GL_LAYOUT_COLOR_ATTACHMENT_EXT; // We will be using the texture as framebuffer backing
  // Our current client written using Vulkano can only handle the general layout
  GLenum src_layout = GL_LAYOUT_GENERAL_EXT;
  const std::array textures{ texture.getId() };
  glWaitSemaphoreEXT(*id, 0, nullptr, 1, textures.data(), &src_layout);
}

void Semaphore::signal(const ExternalTexture& texture)
{
  GLenum dst_layout = GL_LAYOUT_GENERAL_EXT;
  // GLenum dst_layout = GL_LAYOUT_SHADER_READ_ONLY_EXT; // A shader will be using the texture next on the Vulkan side
  const std::array textures{ texture.getId() };
  glSignalSemaphoreEXT(*id, 0, nullptr, 1, textures.data(), &dst_layout);
}

GLuint Semaphore::getId() const
{
  return *id;
}

Semaphore::~Semaphore()
{
  if (id)
    glDeleteSemaphoresEXT(1, &(*id));
}

Framebuffer::Framebuffer(const ExternalTexture& texture): width{ texture.getWidth() }, height{ texture.getHeight() }
{
  glGenFramebuffers(1, &id);
  glBindFramebuffer(GL_FRAMEBUFFER, id);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.getId(), 0);

  // Depthbuffer
  glGenRenderbuffers(1, &depth_tex_id);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_tex_id);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_tex_id);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error{"Failed to create framebuffer"};

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind(bool reading, bool drawing)
{
  if (!reading && !drawing || reading_bound && drawing_bound)
    return;

  // If the current state is the desired state, we can exit early
  if (reading == reading_bound && drawing == drawing_bound)
    return;

  glBindFramebuffer(bind_state_to_framebuffer_target(reading, drawing), id);

  reading_bound = reading;
  drawing_bound = drawing;

  glViewport(0, 0, width, height);
}

void Framebuffer::unbind(bool reading, bool drawing)
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

void Framebuffer::blit_to_screen(GLint screen_width, GLint screen_height)
{
  bind(true, false);
  unbind(false, true);
  glBlitFramebuffer(0, 0, width, height, 0, 0, screen_width, screen_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  unbind();
  glViewport(0, 0, screen_width, screen_height);
}

Framebuffer::~Framebuffer()
{
  bind();
  glDeleteRenderbuffers(1, &depth_tex_id);
  glDeleteFramebuffers(1, &id);
  unbind();
}

std::string get_driver_and_devices()
{
  std::stringstream ss;
  std::array<GLubyte, 16> driver_uuid{};
  glGetUnsignedBytevEXT(GL_DRIVER_UUID_EXT, driver_uuid.data());
  ss << "driver: " << serialize_uuid(driver_uuid) << ", devices: ";
  GLint num_devices{0};
  glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &num_devices);
  for (GLuint i{0}; i < num_devices; ++i)
  {
    std::array<GLubyte, 16> device_uuid{};
    glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, i, device_uuid.data());
    ss << serialize_uuid(device_uuid);
    if (i != num_devices - 1)
      ss << ",";
  }

  return ss.str();
}

std::pair<Semaphore, Semaphore> create_semaphores_from_connection_data(
  const std::vector<ConnectionData>& connection_data)
{
  Semaphore begin, end;
  for (const auto& data : connection_data)
  {
    if (data.identifier != "host_begin_sem" && data.identifier != "host_end_sem")
      continue;

    auto& semaphore{data.identifier == "host_begin_sem" ? begin : end};
    GLuint id;
    glGenSemaphoresEXT(1, &id);
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32")
      throw std::logic_error{"Handle type is not implemented"};
    glImportSemaphoreWin32HandleEXT(id, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, data.handle);
#elif __linux__
    if (data.handle_type != "OpaqueFd")
      throw std::logic_error{"Handle type is not implemented"};
    glImportSemaphoreFdEXT(id, GL_HANDLE_TYPE_OPAQUE_FD_EXT, data.handle);
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

    if (!data.image_data.has_value())
      throw std::logic_error{"Image data is missing"};

    const auto& image_data{*data.image_data};

    GLuint texture_id, memory_id;
    glCreateMemoryObjectsEXT(1, &memory_id);
    if (!glIsMemoryObjectEXT(memory_id))
      throw std::runtime_error{"Failed to create external memory object!"};

    constexpr GLint true_value{GL_TRUE};
    glMemoryObjectParameterivEXT(memory_id, GL_DEDICATED_MEMORY_OBJECT_EXT, &true_value);

#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryWin32HandleEXT(memory_id, image_data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, data.handle);
#elif __linux__
    if (data.handle_type != "OpaqueFd")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryFdEXT(memory_id, image_data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, data.handle);
#endif


    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, format_from_vk_format(image_data.memory_format),
                         static_cast<GLsizei>(image_data.width), static_cast<GLsizei>(image_data.height), memory_id,
                         0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return std::make_unique<ExternalTexture>(texture_id, memory_id, static_cast<GLsizei>(image_data.width),
                                             static_cast<GLsizei>(image_data.height));
  }

  return {};
}
