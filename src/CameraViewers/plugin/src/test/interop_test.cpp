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
#elif __linux__
#include <cstdio>
#include <fcntl.h> // Linux FD manipulation utility (fcntl(2)
// #include <sys/syscall.h>
#include <unistd.h>
#include <sys/socket.h> // Unix socket API
#include <sys/un.h> // Unix domain socket address structure (sockaddr_un(3))
#include <cerrno> // Unix error codes
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

  struct ImageData
  {
    std::uint32_t width;
    std::uint32_t height;
    std::size_t memory_allocation_size;
    std::string memory_format;
  };

  Type type;
  std::string identifier;
#ifdef _WIN32
  HANDLE handle;
#elif __linux__
  int handle;
#endif
  std::string handle_type;
  std::optional<ImageData> image_data;
};

std::vector<ConnectionData> parse_connection_data_from_child_output(std::string_view child_output)
{
  const static std::regex line_match{R"(Connection data: \{[^\}]+\})"};
  const static std::regex sub_match{R"(\"[a-zA-Z0-9_^\"]*\")"};
  std::vector<ConnectionData> data;

  for (std::regex_iterator it{child_output.begin(), child_output.end(), line_match}; it != decltype(it){}; ++
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

    const auto type{ sub_matches.at(0) == "semaphore" ? ConnectionData::Type::Semaphore : ConnectionData::Type::Image};
    const bool has_image_data = 3 + 4 < sub_matches.size();
    if (type == ConnectionData::Type::Image && !has_image_data)
      throw std::runtime_error{ "Image connection data is missing required fields (width, height, memory allocation size, memory image format)" };

    data.push_back(ConnectionData{
      .type = type,
      .identifier = sub_matches.at(1),
      .handle = handle,
      .handle_type = sub_matches.at(2),
      .image_data = has_image_data ? std::optional{ConnectionData::ImageData{
        .width = static_cast<std::uint32_t>(std::stoul(sub_matches.at(4))),
        .height = static_cast<std::uint32_t>(std::stoul(sub_matches.at(5))),
        .memory_allocation_size = std::stoull(sub_matches.at(6)),
        .memory_format = sub_matches.at(7),
      }} : std::nullopt,
    });
  }

  return data;
}

void print_child_pipe(std::string_view child_string_output)
{
  if (child_string_output.empty())
    return;

  std::cout << "Child process:" << std::endl;
  for (auto line : child_string_output | std::views::split('\n') | std::views::transform([](auto&& subrange)
  {
    return std::string_view{subrange.begin(), subrange.end()};
  }))
    std::cout << "\t\t" << line << std::endl;
}

template <std::convertible_to<std::uint8_t> T>
requires (sizeof(T) == 1)
std::string serialize_uuid(const std::array<T, 16>& bytes)
{
  constexpr static std::string_view lookup = "0123456789abcdef";

  std::string out;
  out.reserve(16 + 4); // 16 characters + 4 hyphens
  for (std::size_t i{ 0 }, j{ 0 }; j < bytes.size(); ++i, ++j)
  {
    if (i == 4 || i == 6 + 1 || i == 8 + 2 || i == 10 + 3)
    {
      out.push_back('-');
      --j;
      continue;
    }

    out.push_back(lookup.at(static_cast<std::uint8_t>(bytes[j]) / 16));
    out.push_back(lookup.at(static_cast<std::uint8_t>(bytes[j]) % 16));
  }

  return out;
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
  std::string child_string_output{ extract_output_from_child_pipe(child_output_channel) };
  print_child_pipe(child_string_output);
  return fetch_connection_data_from_child_string_output(child_string_output);
}

// Apparently, simply writing out the bytes of the handle and then reconstructing a handle from that string of bytes
// does not work. So here's some alternative pseudocode stolen from
// https://medium.com/@s12deff/share-windows-handle-using-inter-process-connection-061e51097758
// The plan would then maybe be to pass in a temporary file mapped to shared memory where the viewer can copy it's
// handles to. After which the plugin can then read back from.

// #include <Windows.h>
// #include <iostream>
//
// using namespace std;
//
// class Serialitzator {
// private:
//   string fileName;
// public:
//   // Constructor
//   Serialitzator(string fileName) {
//     this->fileName = fileName;
//   }
//
//   // Getters
//   string getFileName() {
//     return this->fileName;
//   }
//   // Setters
//   void setFileName(string fileName) {
//     this->fileName = fileName;
//   }
//
//   // Methods
//   bool serializeHandle(HANDLE targetHandle) {
//     HANDLE mappedFile = NULL;
//     LPVOID mappedView;
//     mappedFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(targetHandle), "ipcObject");
//     if (mappedFile == NULL) {
//       cout << "Error creating the mapped file";
//       return false;
//     }
//
//     mappedView = MapViewOfFile(mappedFile, FILE_MAP_WRITE, 0, 0, sizeof(targetHandle));
//     if (mappedView == NULL) {
//       cout << "Error creating the view";
//       return false;
//     }
//
//     RtlMoveMemory(mappedView, &targetHandle, sizeof(targetHandle));
//     UnmapViewOfFile(mappedView);
//     return true;
//   }
//
//   HANDLE deserialitzateHandle(string mappedFileName) {
//     HANDLE mappedFile = NULL;
//     LPVOID mappedView;
//     mappedFile = OpenFileMappingA(FILE_MAP_WRITE, FALSE, mappedFileName.c_str());
//     if (mappedFile == NULL) {
//       cout << "Error opening the Mapped File";
//       return mappedFile;
//     }
//
//     mappedView = MapViewOfFile(mappedFile, FILE_MAP_WRITE, 0, 0, sizeof(mappedFile));
//     if (mappedView == NULL) {
//       cout << "Error creating the view";
//       return mappedFile;
//     }
//
//     HANDLE targetHandle;
//     RtlMoveMemory(&targetHandle, mappedView, sizeof(mappedFile));
//     UnmapViewOfFile(mappedView);
//     return targetHandle;
//   }
//
// };

// From https://lackingrhoticity.blogspot.com/2015/05/passing-fds-handles-between-processes.html:
// > On Unix, FD objects can be sent via sockets in messages. On Windows, handle objects cannot be sent in messages;
//   only handle numbers can.
// Windows fills this gap by allowing one process to read or modify another process's handle table synchronously using
// the DuplicateHandle() API. Using this API involves one process dealing with another process's handle numbers.
//
// In contrast, Unix has no equivalent to DuplicateHandle(). A Unix process's FD table is private to the process.
// Consequently, on Unix it is much rarer for a process to have dealings with another process's FD numbers.
//
// On Windows, to send a handle to another process, the sender will generally call two system calls:
//  - Firstly, the sender must call DuplicateHandle() to copy the handle to the destination process. This requires the
//    sender to have a process handle for the destination process. DuplicateHandle() will return a handle number
//    indexing into the destination process's handle table.
//  - Secondly, the sender must communicate the handle number to the destination process, e.g. by sending a message
//    containing the number via a pipe using WriteFile().

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
  for (auto bytes_read{fread(buffer.data(), sizeof(char), BUFFER_SIZE, stream)}; bytes_read > 0; bytes_read = fread(
         buffer.data(), sizeof(char), BUFFER_SIZE, stream))
    out.append(std::string_view{buffer.data(), bytes_read});
  return out;
}

// https://stackoverflow.com/questions/2358684/can-i-share-a-file-descriptor-to-another-process-on-linux-or-are-they-local-to-t
ssize_t read_fd(int fd, void* ptr, size_t nbytes, int& recvfd)
{
  // #ifdef  HAVE_MSGHDR_MSG_CONTROL
  // union {
  //   struct cmsghdr    cm;
  //   char              control[CMSG_SPACE(sizeof(int))];
  // } control_un;
  char control[CMSG_SPACE(sizeof(int))];
  static_assert(sizeof(control) >= sizeof(cmsghdr));

  iovec iov[1]
  {
    iovec{
      .iov_base = ptr,
      .iov_len = nbytes,
    },
  };
  // msghdr msg;
  // msg.msg_control = control;
  // msg.msg_controllen = sizeof(control);
  // // #else
  // //     msg.msg_accrights = (caddr_t) &newfd;
  // //     msg.msg_accrightslen = sizeof(int);
  // // #endif
  //
  // msg.msg_name = nullptr;
  // msg.msg_namelen = 0;
  // msg.msg_iov = iov;
  // msg.msg_iovlen = 1;

  msghdr msg
  {
    .msg_name = nullptr,
    .msg_namelen = 0,
    .msg_iov = iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = sizeof(control),
  };

  const auto bytes_received{recvmsg(fd, &msg, 0)};
  if (bytes_received < 0)
    return bytes_received;

  // #ifdef  HAVE_MSGHDR_MSG_CONTROL
  auto cmptr{CMSG_FIRSTHDR(&msg)};
  if (cmptr == nullptr)
    return -1;

  cmptr->cmsg_len == CMSG_LEN(sizeof(int));
  if (cmptr->cmsg_level != SOL_SOCKET)
  {
    std::cerr << "cmsg_level != SOL_SOCKET" << std::endl;
    return -1;
  }

  if (cmptr->cmsg_type != SCM_RIGHTS)
  {
    std::cerr << "cmsg_type != SCM_RIGHTS" << std::endl;
    return -1;
  }

  recvfd = *reinterpret_cast<int*>(CMSG_DATA(cmptr));

  // #else
  // /* *INDENT-OFF* */
  //   if (msg.msg_accrightslen == sizeof(int))
  //       recvfd = newfd;
  //   else
  //       recvfd = -1;       /* descriptor was not passed */
  // /* *INDENT-ON* */
  // #endif

  return bytes_received;
}

class IPCSemaphore
{
private:
  int server_fd{ -1 };
  int client_fd{ -1 };
  std::array<char, 10> dummy_buffer{};

public:
  IPCSemaphore() = default;
  IPCSemaphore(const IPCSemaphore&) = delete;
  IPCSemaphore(IPCSemaphore&& rhs) noexcept
  {
    std::swap(server_fd, rhs.server_fd);
    std::swap(client_fd, rhs.client_fd);
    std::swap(dummy_buffer, rhs.dummy_buffer);
  }

  IPCSemaphore(int _server_fd, int _client_fd) : server_fd{ _server_fd }, client_fd{ _client_fd }, dummy_buffer{}
  {
    // Set client fd to be blocking
    if (fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) & ~O_NONBLOCK) < 0)
      std::cerr << "Failed to set blocking flag for socket" << std::endl;
  }

  IPCSemaphore& operator=(const IPCSemaphore&) = delete;
  IPCSemaphore& operator=(IPCSemaphore&& rhs) noexcept
  {
    std::swap(server_fd, rhs.server_fd);
    std::swap(client_fd, rhs.client_fd);
    std::swap(dummy_buffer, rhs.dummy_buffer);
    return *this;
  }

  void wait()
  {
#ifdef _DEBUG
    if (fcntl(client_fd, F_GETFD) < 0)
    {
      std::cerr << "Client socket connection has expired" << std::endl;
      return;
    }
#endif

    const auto bytes_read{ read(client_fd, dummy_buffer.data(), dummy_buffer.size()) };
    if (bytes_read == 0)
      return;

    if (bytes_read < 0)
      std::cerr << "Failed to read from IPC semaphore socket. Error: " << errno << std::endl;

    // Sanity check
    if (bytes_read != 1)
      std::cerr << "Unexpected amount of bytss read. This could mean we somehow have more signal's than wait's." << std::endl;

    if (dummy_buffer[0] != '\0')
      std::cerr << "Received unexpected data from client. Have the client socket forgotten to transition to \"semaphore\" mode?" << std::endl;
  }

  void signal()
  {
#ifdef _DEBUG
    if (fcntl(client_fd, F_GETFD) < 0)
    {
      std::cerr << "Client socket connection has expired" << std::endl;
      return;
    }
#endif

    char dummy_data = 0;
    const auto bytes_written{ write(client_fd, &dummy_data, 1) };
    if (bytes_written == 1)
      return;

    if (bytes_written < 0)
      std::cerr << "Failed to write to IPC semaphore socket. Error: " << errno << std::endl;
    else
      std::cerr << "Unexpected amount of bytes written" << std::endl;
  }

  ~IPCSemaphore()
  {
    if (client_fd == -1 || server_fd == -1)
      return;

    // Perform a last signal to free any possibly waiting threads
    signal();

    if (close(client_fd) < 0 || close(server_fd) < 0)
      std::cerr << "Failed to close IPC semaphore socket:" << errno << std::endl;
  }
};

class ChildProcess
{
private:
  std::atomic<FILE*> process_stream{ nullptr };
  std::future<int> return_code;

public:
  ChildProcess() = default;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&& rhs)
    : process_stream{ rhs.process_stream.exchange(nullptr) }, return_code{ std::move(rhs.return_code) }
  {
    // The current implementation of ChildProcess can invoke undefined behaviour due to a dangling this pointer captured
    // in the async lambda if the ChildProcess is moved before the process_stream was set. We therefore have to assert
    // this here.
    assert(process_stream.load() != nullptr);
  }

  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess& operator=(ChildProcess&& rhs)
  {
    // Steal the stream
    process_stream.store(rhs.process_stream.exchange(nullptr));

    // The current implementation of ChildProcess can invoke undefined behaviour due to a dangling this pointer captured
    // in the async lambda if the ChildProcess is moved before the process_stream was set. We therefore have to assert
    // this here.
    assert(process_stream.load() != nullptr);

    std::swap(return_code, rhs.return_code);
    return *this;
  }

  explicit ChildProcess(const std::string& socket_addr_path, std::string_view driver_and_devices)
  {
    // The current thread will block while waiting for incoming socket connections. So for debugging purposes we start
    // and observe the child process in a separate thread so we can pipe its output to the standard output
    return_code = std::async(std::launch::async, [this, socket_addr_path, driver_and_devices = std::string{ driver_and_devices }]
    {
      const std::string cmd{std::format("{} \"{}\" {}", VIEWER_PATH, driver_and_devices, socket_addr_path)};
      auto child_std_output = popen(cmd.c_str(), "r");
      if (child_std_output == nullptr)
      {
        std::cout << "Failed to spawn child process" << std::endl;
        return -1;
      }

      this->process_stream.store(child_std_output);

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
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }

      return 0;
    });
  }

  int close()
  {
    const auto unique_process_stream{ process_stream.exchange(nullptr) };
    if (unique_process_stream == nullptr)
      return 0;

    const auto command_return_code{ pclose(unique_process_stream) };
    if (command_return_code < 0)
      return command_return_code;
    return return_code.get();
  }

  ~ChildProcess()
  {
    close();
  }
};

std::optional<std::tuple<std::vector<ConnectionData>, ChildProcess, IPCSemaphore>> init_child_process_and_fetch_connection_data(std::string_view driver_and_devices)
{
  // Create a temp path we will use for the socket connection
  std::error_code ec;
  // 20 characters is probably enough to guarantee uniqueness
  const auto socket_addr_path{
    std::filesystem::temp_directory_path(ec) / std::format("{}.sock", random_alphanumeric_string(20))
  };
  assert(!ec); // We don't expect this to fail very often

  if (std::filesystem::exists(socket_addr_path, ec) || ec)
  {
    std::cerr <<
      "Despite all odds, the unique path dedicated for our socket connection somehow exists or failed. Error code " <<
      ec.value() << std::endl;
    return {};
  }

  // Create the socket
  int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket. Error code: " << errno << std::endl;
    return {};
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
    return {};
  }

  if (listen(server_fd, 1) < 0)
  {
    std::cerr << "Failed to set socket to listen. Error code: " << errno << std::endl;
    return {};
  }

  ChildProcess child_process{ socket_addr_path.string(), driver_and_devices };

  std::cout << "Waiting for child process..." << std::endl;
  int client_fd = accept(server_fd, nullptr, nullptr); // Blocks until a connection is made
  // After receiving an incoming connection, delete the address path to prevent other clients from connecting
  if (!std::filesystem::remove(socket_addr_path, ec))
  {
    std::cerr << std::format("Failed to remove socket addr file {}. Error code: {}", socket_addr_path.string(),
                             ec.value()) << std::endl;
    return {};
  }

  std::cout << "Got a connection!" << std::endl;

  // Read some test data from child
  std::vector<ConnectionData> connection_data{};
  {
    std::array<char, 256> buffer{};
    int file_fd{-1};
    // Read a maximum of 3 connection data messages
    for (unsigned int i{ 0 }; i < 3; ++i)
    {
      const auto bytes_read{ read_fd(client_fd, buffer.data(), buffer.size(), file_fd) };
      if (bytes_read == 0)
        break;

      if (bytes_read < 0)
      {
        // Special case: Client connection was interrupted. This is likely intentional, so just break out of the loop
        if (fcntl(client_fd, F_GETFD) < 0)
          break;
        std::cerr << "Failed to receive socket message from child process. Error: " << errno << std::endl;
        return {};
      }
      if (file_fd < 0 || fcntl(file_fd, F_GETFD) < 0)
      {
        std::cerr << "File descriptor received from child process is invalid" << std::endl;
        return {};
      }

      const std::string_view formatted_message{buffer.data(), static_cast<std::size_t>(bytes_read)};
      auto sub_connection_data{parse_connection_data_from_child_output(formatted_message)};
      if (sub_connection_data.size() != 1)
      {
        std::cerr << "Unexpected amount of connection data received from child process" << std::endl;
        return {};
      }


      sub_connection_data.front().handle = file_fd;
      file_fd = -1;
      connection_data.push_back(std::move(sub_connection_data.front()));
      std::cout << "Received connection data from the child process" << std::endl;
    }
  }

  // Transition the socket connection into a "semaphore"
  IPCSemaphore semaphore{ server_fd, client_fd };
  return std::tuple{ std::move(connection_data), std::move(child_process), std::move(semaphore) };
}
#endif

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

constexpr GLenum format_from_vk_format(std::string_view vk_format)
{
  if (vk_format == "R16G16B16A16_UNORM")
    return GL_RGBA16;
  std::unreachable();
}

std::unique_ptr<ExternalTexture> create_texture_from_connection_data(const std::vector<ConnectionData>& connection_data)
{
  for (const auto& data : connection_data)
  {
    if (data.type != ConnectionData::Type::Image)
      continue;

    if (!data.image_data.has_value())
      throw std::logic_error{"Image data is missing"};

    const auto& image_data{ *data.image_data };

    GLuint texture_id, memory_id;
    glCreateMemoryObjectsEXT(1, &memory_id);
#ifdef _WIN32
    if (data.handle_type != "OpaqueWin32")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryWin32HandleEXT(memory_id, image_data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, data.handle);
#elif __linux__
    if (data.handle_type != "OpaqueFd")
      throw std::logic_error{"Handle type is not implemented"};
    glImportMemoryFdEXT(memory_id, image_data.memory_allocation_size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, data.handle);
#endif

    if (!glIsMemoryObjectEXT(memory_id))
      throw std::runtime_error{"Failed to create external memory object!"};

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset
    glTextureStorageMem2DEXT(texture_id, 1, format_from_vk_format(image_data.memory_format),
                             static_cast<GLsizei>(image_data.width), static_cast<GLsizei>(image_data.height), memory_id,
                             0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return std::make_unique<ExternalTexture>(texture_id, memory_id, static_cast<GLsizei>(image_data.width), static_cast<GLsizei>(image_data.height));
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
  for (GLuint i{ 0 }; i < supported_extension_count; ++i)
    supported_extensions.insert(std::string{ reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)) });

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
  if (!supported_extensions.contains("GL_EXT_memory_object_fd") || !supported_extensions.contains("GL_EXT_semaphore_fd"))
  {
    std::cout << "Extension GL_EXT_memory_object_fd or GL_EXT_semaphore_fd is missing." << std::endl;
    glfwTerminate();
    return -1;
  }
#endif

  // Fetch driver and devices
  std::string driver_and_devices;
  {
    std::stringstream ss;
    std::array<GLubyte, 16> driver_uuid{};
    glGetUnsignedBytevEXT(GL_DRIVER_UUID_EXT, driver_uuid.data());
    ss << "driver: " << serialize_uuid(driver_uuid) << ", devices: ";
    GLint num_devices{ 0 };
    glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT, &num_devices);
    for (GLuint i{ 0 }; i < num_devices; ++i)
    {
      std::array<GLubyte, 16> device_uuid{};
      glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, i, device_uuid.data());
      ss << serialize_uuid(device_uuid);
      if (i != num_devices - 1)
        ss << ",";
    }
    driver_and_devices = ss.str();
  }
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
    if (type != GL_DEBUG_TYPE_PERFORMANCE && severity != GL_DEBUG_SEVERITY_NOTIFICATION && severity != GL_DEBUG_SEVERITY_LOW)
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
    if (auto result{ init_child_process_and_fetch_connection_data(driver_and_devices) })
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
    const auto shader{create_shader()};
    if (!shader)
      return -1;

    glUseProgram(shader->id);
    auto plane{create_plane()};

    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    // end_semaphore.signal(*shared_texture);
    // At this point both the host and the client will wait for both it's start and end semaphores. One of them has to
    // take a lead, and here we choose to start with the host (as the client uses the image data created by the host).
    // begin_semaphore.signal(*shared_texture);
    glFlush();

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
      Framebuffer shared_texture_framebuffer{ *shared_texture };
      shared_texture_framebuffer.bind();
      glClearColor(0.2f, 0.3f, t - std::floor(t), 1.0f);
      glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
      glUniform1f(1, t * 3.0);

      // Wait for rendering to be ready
      child_process_semaphore.wait();
      begin_semaphore.wait(*shared_texture);
      glFlush(); // Probably not necessary (as client will be blocked), but just in case
      std::cout << "Host started rendering" << std::endl;

      glClear(GL_COLOR_BUFFER_BIT);
      // glDrawArrays(GL_TRIANGLES, 0, 6);

      // Blit the framebuffer to the screen for debugging:
      shared_texture_framebuffer.blit_to_screen();

      // Signal the Vulkan app that OpenGL rendering is done
      std::cout << "Host done rendering" << std::endl;
      end_semaphore.signal(*shared_texture);
      // OpenGL usually chooses itself when to flush commands to the GPU, but since the Vulkan implementation
      // is waiting for synchronization from OpenGL we need to explicitly flush commands to the GPU every frame.
      glFinish(); // glFlush();
      child_process_semaphore.signal();

      // Draw an additional time directly to the screen to verify what's being passed to the viewer program corresponds
      // with what was written
      // shared_texture_framebuffer.unbind();
      // glClear(GL_COLOR_BUFFER_BIT);
      // glDrawArrays(GL_TRIANGLES, 0, 6);

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
    child_process_semaphore = {}; // Close the semaphore (possibly forcing the client to run into a "pipe destroyed" signal)
    if (auto child_process_return_code{ child_process.close() }; child_process_return_code < 0)
    {
      std::cerr << "Child process successfully closed but returned error code: " << child_process_return_code << std::endl;
      return -1;
    }
#endif

    glUseProgram(0);
    glBindVertexArray(0);
  }

  glfwTerminate();
  return 0;
}
