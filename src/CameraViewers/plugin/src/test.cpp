#ifdef __cpp_lib_modules
import std;
#else
#include <iostream>
#include <chrono>
#include <map>
#include <string_view>
#include <array>
#include <memory>
#include <filesystem>
#include <ranges>
#include <regex>
#endif

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

constexpr std::size_t WIDTH = 800;
constexpr std::size_t HEIGHT = 600;

static constexpr std::string_view vs_shader_source = R"SHADER(#version 430 core
layout(location = 0) in vec2 pos;
layout(location = 0) out vec2 uv;

void main() {
  uv = pos;
  gl_Position = vec4(pos, 0.0, 1.0);
}
)SHADER";

// Raymarching / raytracing shader written by me
static constexpr std::string_view fs_shader_source = R"SHADER(#version 430 core
#define PI 3.14159
#define MAX_STEPS 50
#define EPSILON 0.01
#define TIME_MULT 0.3
#define SMOOTHNESS 1.1
#define SCENE_INTERP 0.8
#define MAX_BOUNCES 3

layout(location = 0) in vec2 uv;

layout(location = 0) uniform mat4 MVP = mat4(1.0);
layout(location = 1) uniform float t = 0.0;

layout(location = 0) out vec4 fragColor;

float sdf(vec4 s, vec3 p) {
    return length(s.xyz - p) - s.w;
}

float weightedAbs(float a, float b, float k, float w) {
    float d = a - b;
    return d < 0.0 ? -pow(2.0, w) * d: pow(2.0, -w) * d;
}

// polynomial smooth min (k = 0.1);
float smin( float a, float b, float k)
{
    float h = max( k-abs(a-b), 0.0 )/k;
    return min( a, b ) - h*h*k*(1.0/4.0);
}

float smix(float a, float b, float t, float k) {
    float d = b - a;
    float h = 1.0/k;
    return (-2.0 * d + 2.0 * h + 1.0) * t * t * t + (3.0 * d - h - 2.0) * t * t + (1.0 - h) * t + a;
}

vec3 smix(vec3 a, vec3 b, float t, float k) {
    vec3 d = b - a;
    float h = 1.0/k;
    return (-2.0 * d + 2.0 * h + 1.0) * t * t * t + (3.0 * d - h - 2.0) * t * t + (1.0 - h) * t + a;
}

// http://viniciusgraciano.com/blog/smin/ - More smin explanation
float smin2(float a, float b, float k) {
    float d = abs(a-b);
    float u = max(k - d, 0.0)/k;
    return min(a,b) - u*u*k;
}

vec4 spheres[3] = vec4[3](
    vec4(0., 2.0, 0., 1.3),
    vec4(3.0, 0.5, -0.8, 2.0),
    vec4(-0.9, -2.3, -0.1, 2.3)
);

vec4 spheres2[2] = vec4[2](
    vec4(0.3, -1.2, 0.4, 1.6),
    vec4(-2.0, 1.3, 2.0, 2.4)
);

float rayPlane(vec3 ro, vec3 rd, vec3 n, vec3 p0)
{
	// assuming vectors are all normalized
	float denom = dot(n, -rd);
	if (0.0001 < denom) {
		vec3 p2r = ro - p0;
		return dot(p2r, n) / denom;
	}

	return -1.0;
}

bool raymarch(inout vec4 ro, inout vec4 rd, out vec3 normal, float interpolation)
{
  float farDist = rd.w;

  for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 p = ro.xyz + ro.w * rd.xyz;

        float dist = farDist;
        float dist2 = farDist;
        vec3 diff = vec3(farDist);
        vec3 diff2 = vec3(farDist);

        for (int j = 0; j < 3; j++)
            dist = smin(dist, sdf(spheres[j], p), SMOOTHNESS);
        for (int j = 0; j < 2; j++)
            dist2 = smin(dist2, sdf(spheres2[j], p), SMOOTHNESS);

        dist = smix(dist, dist2, interpolation, SCENE_INTERP);
        // dist = mix(dist, dist2, interpolation);


        if (dist < EPSILON) {
            // Calculate gradient
            vec3 g = vec3(0.0);
            vec3 g2 = vec3(0.0);

            for (int j = 0; j < 3; j++) {
                diff[0] = smin(diff[0], sdf(spheres[j], vec3(p.x+EPSILON, p.y, p.z)), SMOOTHNESS);
                diff[1] = smin(diff[1], sdf(spheres[j], vec3(p.x, p.y+EPSILON, p.z)), SMOOTHNESS);
                diff[2] = smin(diff[2], sdf(spheres[j], vec3(p.x, p.y, p.z+EPSILON)), SMOOTHNESS);
            }
            for (int j = 0; j < 2; j++) {
                diff2[0] = smin(diff2[0], sdf(spheres2[j], vec3(p.x+EPSILON, p.y, p.z)), SMOOTHNESS);
                diff2[1] = smin(diff2[1], sdf(spheres2[j], vec3(p.x, p.y+EPSILON, p.z)), SMOOTHNESS);
                diff2[2] = smin(diff2[2], sdf(spheres2[j], vec3(p.x, p.y, p.z+EPSILON)), SMOOTHNESS);
            }

            // Interpolate diff the same way dist was interpolated
            diff = smix(diff, diff2, interpolation, SCENE_INTERP);
            g = diff - vec3(dist); // Forward differences

            normal = normalize(g);
            return true;
        }

        ro.w += dist;
        if (farDist < ro.w)
            return false;
    }
}

void main() {    
    vec4 near = MVP * vec4(uv, -1.0, 1.0);
    near /= near.w;
    vec4 far = MVP * vec4(uv, 1.0, 1.0);
    far /= far.w;


    vec4 ro = vec4(near.xyz, 0.0);
    vec4 rd = vec4((far - near).xyz, 1.0);
    rd.w = length(rd.xyz);
    // const float marchDistance = 300.0;
    // rd.w = marchDistance;
    rd.xyz /= rd.w;

    vec4 lightPos = MVP * vec4(0., 0., -1.0, 1.0);
    lightPos /= lightPos.w;

    float interpolation = sin(t * TIME_MULT) * sin(t * TIME_MULT);

    vec3 normal = vec3(0.0);
    vec3 color = vec3(0.0);
    vec3 p = ro.xyz;
    
    bool hitAnything = false;
    bool hitLast = false;
    for (int i = 0; i < 2; ++i) {
      bool marchHit = raymarch(ro, rd, normal, interpolation);
      float plane = rayPlane(ro.xyz, rd.xyz, vec3(0., 1., 0.), vec3(0., -5.4, 0.));

      if (!marchHit && plane <= 0.0)
        break;
      
      hitAnything = true;

      vec3 surfaceColor = vec3(0., 0.3, 0.8); //vec3(normal * 0.5 + 0.5);

      // Reset parameters to continue bounding from hit position
      
      // If plane is closer, probably hit plane
      if (0.0 < plane && (plane < ro.w || !marchHit)) {
        p = ro.xyz + plane * rd.xyz;
        normal = vec3(0., 1., 0.);
        surfaceColor = vec3(1.0, 0.0, 0.0);
      } else {
        p = ro.xyz + ro.w * rd.xyz;
      }

      vec3 lightDir = normalize(lightPos.xyz - p);
      float phong = max(dot(normal, lightDir), 0.15);
      float bounceMultiplier = pow(0.5, float(i));
      color = phong * vec3(normal * 0.5 + 0.5) * bounceMultiplier;

      rd.xyz = reflect(rd.xyz, normal);
      ro = vec4(p + rd.xyz * 0.3, 0.0);
    }

    if (hitAnything) {
      fragColor = vec4(color, 1.0);
      return;
    }


    fragColor = vec4(rd.xyz, 1.0);
}
)SHADER";

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

  ~Semaphore() {
    if (id)
      glDeleteSemaphoresEXT(1, &(*id));
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

struct ConnectionData {
  enum class Type {
    Semaphore,
    Image
  };

  Type type;
  std::string identifier;
#ifdef _WIN32
  HANDLE handle;
#endif
  std::string handle_type;
  std::optional<std::size_t> memory_allocation_size;
  std::optional<std::string> memory_image_format;
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

std::optional<PROCESS_INFORMATION> create_child_process(std::string cmd, std::shared_ptr<HANDLE> std_out) {
  PROCESS_INFORMATION p_info;
  STARTUPINFO s_info;
 
  ZeroMemory( &p_info, sizeof(PROCESS_INFORMATION) );
  ZeroMemory( &s_info, sizeof(STARTUPINFO) );
 
  s_info.cb = sizeof(STARTUPINFO); 
  s_info.hStdError = *std_out;
  s_info.hStdOutput = *std_out;
  //  s_info.hStdInput = g_hChildStd_IN_Rd;
  s_info.dwFlags |= STARTF_USESTDHANDLES;
 
  if (!CreateProcess(NULL, 
    cmd.data(),
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

std::string extract_from_child_pipe(HANDLE pipe) {
  static std::string buffer{};
  constexpr std::size_t BUFFER_SIZE = 4096;
  buffer.resize(BUFFER_SIZE);
  DWORD bytesRead;
  std::string out{};
  out.reserve(BUFFER_SIZE);
  
  while (ReadFile(pipe, buffer.data(), BUFFER_SIZE, &bytesRead, NULL))
    out.append(std::string_view{buffer.data(), bytesRead});

  return out;
}

std::vector<ConnectionData> fetch_connection_data_from_child_pipe_output(std::string_view child_pipe_output) {
  const static std::regex line_match{"Connection data: {.*}", std::regex_constants::basic};
  const static std::regex sub_match{"\"[a-zA-Z0-9_]*\"", std::regex_constants::basic};
  std::vector<ConnectionData> data;

  for (std::regex_iterator it{child_pipe_output.begin(), child_pipe_output.end(), line_match}; it != decltype(it){}; ++it) {
    auto match{ *it };
    const auto sub_str{ match.str() };

    std::regex_iterator sub_match_it{sub_str.begin(), sub_str.end(), sub_match};
    const auto sub_matches = std::ranges::subrange{sub_match_it, decltype(sub_match_it){}} | std::views::transform([](auto match){
      std::string s{ match.str() };
      return s.substr(1, s.size() - 2);
    }) | std::ranges::to<std::vector<std::string>>();

    static_assert(4 <= sizeof(std::size_t) && 4 <= sizeof(long long) && 4 <= sizeof(HANDLE), "Program requries 64 bit pointer types");

    const auto mem_address{ std::stoll(sub_matches.at(3), nullptr, 16) };
    if (mem_address <= 0)
      continue;

    HANDLE handle{ reinterpret_cast<void*>(static_cast<std::size_t>(mem_address)) };

    data.push_back(ConnectionData {
      .type = sub_matches.at(0) == "semaphore" ? ConnectionData::Type::Semaphore : ConnectionData::Type::Image,
      .identifier = sub_matches.at(1),
      .handle = handle,
      .handle_type = sub_matches.at(2),
      .memory_allocation_size = 4 < sub_matches.size() ? std::make_optional<std::size_t>(std::stoll(sub_matches.at(4))) : std::nullopt,
      .memory_image_format = 5 < sub_matches.size() ? std::make_optional(sub_matches.at(5)) : std::nullopt,
    });
  }

  return data;
}
#endif

void print_child_pipe(std::string_view child_pipe_output) {
  if (child_pipe_output.empty())
    return;

  std::cout << "Child process:" << std::endl;
  for (auto line : child_pipe_output | std::views::split('\n') | std::views::transform([](auto&& subrange){ return std::string_view{ subrange.begin(), subrange.end() }; }))
    std::cout << "\t\t" << line << std::endl;
}

std::pair<Semaphore, Semaphore> create_semaphores_from_connection_data(const std::vector<ConnectionData>& connection_data) {
  Semaphore begin, end;
  for (const auto& data : connection_data) {
    if (data.identifier != "OGL_begin" && data.identifier != "OGL_end")
      continue;

    auto& semaphore{ data.identifier == "OGL_begin" ? begin : end };
    GLuint id;
    glGenSemaphoresEXT(1, &id);
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32Kmt")
      throw std::logic_error{"Handle type is not implemented"};
    glImportSemaphoreWin32HandleEXT(id, GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT, data.handle);
#endif
    semaphore = {id};
  }

  return { std::move(begin), std::move(end) };
}

std::optional<ExternalTexture> create_texture_from_connection_data(const std::vector<ConnectionData>& connection_data) {
  for (const auto& data : connection_data) {
    if (data.type != ConnectionData::Type::Image)
      continue;

    GLuint texture_id, memory_id;
    glCreateMemoryObjectsEXT(1, &memory_id);
    if (!data.memory_allocation_size)
      throw std::logic_error{"Memory connection data requires a memory allocation size"};
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32Kmt")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryWin32HandleEXT(memory_id, *data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT, data.handle);
#endif

    glGenTextures(1, &texture_id);
	  glBindTexture(GL_TEXTURE_2D, texture_id);

    // GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset
    if (data.memory_image_format != "R16G16B16A16_UNORM")
      throw std::logic_error{"Unsupported format"};
    glTextureStorageMem2DEXT(texture_id, 1, GL_RGBA16UI, WIDTH, HEIGHT, memory_id, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    return ExternalTexture{ texture_id, memory_id };
  }

  return std::nullopt;
}

int main()
{
  const auto program_start = std::chrono::steady_clock::now();

  // glfw: initialize and configure
  // ------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

  GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "Viewer test", NULL, NULL);

  if (window == NULL)
  {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  constexpr float FOV = 45.f;
  static auto p_mat{ glm::perspective(FOV, 800.f / 600.f, 0.1f, 100.f) };
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow *window, int width, int height) {
    glViewport(0, 0, width, height);
    p_mat = glm::perspective(FOV, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
  });

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }
  else if (glGenSemaphoresEXT == nullptr || glCreateMemoryObjectsEXT == nullptr)
	{
		std::cout << "Extension GL_EXT_memory_object or GL_EXT_semaphore is not missing." << std::endl;
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

  {
    // Setup subprocess + pipe
    // This part is unfortunately only implemented for Windows
#ifdef _WIN32
    auto [child_std_read, child_std_write] = create_pipe();
    if (!child_std_read)
      return -1;

    constexpr static auto VIEWER_PATH = ROOT_DIR "\\..\\viewer\\target\\debug\\viewer.exe";
    if (!std::filesystem::exists(std::filesystem::path{VIEWER_PATH}))
    {
      std::cout << "Could not find viewer executable!" << std::endl;
      return -1;
    }
    auto process = create_child_process(VIEWER_PATH, std::move(child_std_write));
    if (!process)
    {
      std::cout << "Failed to spawn process" << std::endl;
      return -1;
    }
#endif

    std::vector<ConnectionData> connection_data{};
    while (connection_data.empty()) {
#ifdef _WIN32
      std::string child_pipe_output{ extract_from_child_pipe(*child_std_read) };
      print_child_pipe(child_pipe_output);
      connection_data = fetch_connection_data_from_child_pipe_output(child_pipe_output);
#endif
    }

    std::cout << "Found connection data: " << std::endl;
    for (const auto& data : connection_data)
      std::cout << std::format("{{ type: {}, identifier: {} }}", data.type == ConnectionData::Type::Semaphore ? "Semaphore" : "Image", data.identifier) << std::endl;

    auto [begin_semaphore, end_semaphore] = create_semaphores_from_connection_data(connection_data);
    const auto shared_texture = create_texture_from_connection_data(connection_data);
    if (!shared_texture)
      throw std::runtime_error{"Couldn't fetch shared texture"};

    // Setup scene:
    const auto shader{ create_shader() };
    if (!shader)
      return -1;

    glUseProgram(shader->id);
    auto plane{ create_plane() };

    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    auto last_tp{ std::chrono::steady_clock::now() };
    std::cout << std::format("Took {}ms to initialize", std::chrono::duration_cast<std::chrono::microseconds>(last_tp - program_start).count() * 0.001) << std::endl;

    float t{0.f};
    while (!glfwWindowShouldClose(window))
    {
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

      const auto current_tp{ std::chrono::steady_clock::now() };
      const auto delta_t = std::chrono::duration_cast<std::chrono::milliseconds>(current_tp - last_tp).count() * 0.001;
      last_tp = current_tp;
      
      t += delta_t * 0.3;

      const auto v_mat{ glm::lookAt(glm::vec3{std::sin(t) * 10.f, 1.f, std::cos(t) * 10.f}, {}, glm::vec3{0.f, 1.f, 0.f}) };
      const auto mvp{ glm::inverse(p_mat * v_mat) };

      glClearColor(0.2f, 0.3f, t, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
      glUniform1f(1, t * 3.0);

      glDrawArrays(GL_TRIANGLES, 0, 6);

#ifdef _WIN32
      print_child_pipe(extract_from_child_pipe(*child_std_read));
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
#endif

    glUseProgram(0);
    glBindVertexArray(0);
  }

  glfwTerminate();
  return 0;
}