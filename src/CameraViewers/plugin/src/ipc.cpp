#include "ipc.hpp"
#include "common_gl.hpp"

#include <regex>
#include <iostream>
#include <format>
#if __linux__
#include <random>
#include <cassert>
#include <cstring>
#include <fcntl.h> // Linux FD manipulation utility (fcntl(2)
// #include <sys/syscall.h>
#include <sys/socket.h> // Unix socket API
#include <sys/un.h> // Unix domain socket address structure (sockaddr_un(3))
#include <cerrno> // Unix error codes
#endif

#if defined(_WIN32) && !defined(UNIT_TEST_MOCK)
#include <windows.h>
#endif

namespace
{
#if __linux__
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
#endif
}

std::vector<ConnectionData> ConnectionData::parse(std::string_view child_output)
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

#if defined(__linux__) || defined(UNIT_TEST_MOCK)
    const int handle = std::stoi(sub_matches.at(3));
#elif _WIN32
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
#endif

    const auto type{sub_matches.at(0) == "semaphore" ? Type::Semaphore : Type::Image};
    const bool has_image_data = 3 + 4 < sub_matches.size();
    if (type == Type::Image && !has_image_data)
      throw std::runtime_error{
        "Image connection data is missing required fields (width, height, memory allocation size, memory image format)"
      };

    data.push_back(ConnectionData{
      .type = type,
      .identifier = sub_matches.at(1),
      .handle = handle,
      .handle_type = sub_matches.at(2),
      .image_data = has_image_data
                      ? std::optional{
                        ImageData{
                          .width = static_cast<std::uint32_t>(std::stoul(sub_matches.at(4))),
                          .height = static_cast<std::uint32_t>(std::stoul(sub_matches.at(5))),
                          .memory_allocation_size = std::stoull(sub_matches.at(6)),
                          .memory_format = sub_matches.at(7),
                        }
                      }
                      : std::nullopt,
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
IPCSemaphore::IPCSemaphore(IPCSemaphore&& rhs) noexcept
{
  std::swap(server_fd, rhs.server_fd);
  std::swap(client_fd, rhs.client_fd);
  std::swap(dummy_buffer, rhs.dummy_buffer);
}

IPCSemaphore::IPCSemaphore(int _server_fd, int _client_fd)
  : server_fd{_server_fd}, client_fd{_client_fd}, dummy_buffer{}
{
  // Set client fd to be blocking
  if (fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) & ~O_NONBLOCK) < 0)
    std::cerr << "Failed to set blocking flag for socket" << std::endl;
}

IPCSemaphore& IPCSemaphore::operator=(IPCSemaphore&& rhs) noexcept
{
  std::swap(server_fd, rhs.server_fd);
  std::swap(client_fd, rhs.client_fd);
  std::swap(dummy_buffer, rhs.dummy_buffer);
  return *this;
}

void IPCSemaphore::wait()
{
#ifdef _DEBUG
    if (fcntl(client_fd, F_GETFD) < 0)
    {
      std::cerr << "Client socket connection has expired" << std::endl;
      return;
    }
#endif

  const auto bytes_read{read(client_fd, dummy_buffer.data(), dummy_buffer.size())};
  if (bytes_read == 0)
    return;

  if (bytes_read < 0)
    std::cerr << "Failed to read from IPC semaphore socket. Error: " << errno << std::endl;

  // Sanity check
  if (bytes_read != 1)
    std::cerr << "Unexpected amount of bytss read. This could mean we somehow have more signal's than wait's." <<
      std::endl;

  if (dummy_buffer[0] != '\0')
    std::cerr <<
      "Received unexpected data from client. Have the client socket forgotten to transition to \"semaphore\" mode?" <<
      std::endl;
}

void IPCSemaphore::signal()
{
#ifdef _DEBUG
    if (fcntl(client_fd, F_GETFD) < 0)
    {
      std::cerr << "Client socket connection has expired" << std::endl;
      return;
    }
#endif

  char dummy_data = 0;
  const auto bytes_written{write(client_fd, &dummy_data, 1)};
  if (bytes_written == 1)
    return;

  if (bytes_written < 0)
    std::cerr << "Failed to write to IPC semaphore socket. Error: " << errno << std::endl;
  else
    std::cerr << "Unexpected amount of bytes written" << std::endl;
}

IPCSemaphore::~IPCSemaphore()
{
  if (client_fd == -1 || server_fd == -1)
    return;

  // Perform a last signal to free any possibly waiting threads
  signal();

  if (close(client_fd) < 0 || close(server_fd) < 0)
    std::cerr << "Failed to close IPC semaphore socket:" << errno << std::endl;
}

ChildProcess::ChildProcess(ChildProcess&& rhs): process_stream{rhs.process_stream.exchange(nullptr)},
                                                return_code{std::move(rhs.return_code)}
{
  // The current implementation of ChildProcess can invoke undefined behaviour due to a dangling this pointer captured
  // in the async lambda if the ChildProcess is moved before the process_stream was set. We therefore have to assert
  // this here.
  if (process_stream.load() == nullptr)
    throw std::logic_error{"ChildProcess was moved before the process_stream was set, causing undefined behavior"};
}

ChildProcess& ChildProcess::operator=(ChildProcess&& rhs)
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

ChildProcess::ChildProcess(const std::string& viewer_path, const std::string& socket_addr_path, std::string_view driver_and_devices)
{
  // The current thread will block while waiting for incoming socket connections. So for debugging purposes we start
  // and observe the child process in a separate thread so we can pipe its output to the standard output
  return_code = std::async(std::launch::async,
                           [this, viewer_path, socket_addr_path, driver_and_devices = std::string{driver_and_devices}]
                           {
                             const std::string cmd{
                               std::format("{} \"{}\" {}", viewer_path, driver_and_devices, socket_addr_path)
                             };
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

                             if (fcntl(child_process_fd, F_SETFL,
                                       fcntl(child_process_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
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

int ChildProcess::close()
{
  const auto unique_process_stream{process_stream.exchange(nullptr)};
  if (unique_process_stream == nullptr)
    return 0;

  const auto command_return_code{pclose(unique_process_stream)};
  if (command_return_code < 0)
    return command_return_code;
  return return_code.get();
}

ChildProcess::~ChildProcess()
{
  close();
}

std::optional<std::tuple<std::vector<ConnectionData>, ChildProcess, IPCSemaphore>>
init_child_process_and_fetch_connection_data(std::string_view viewer_path, std::string_view driver_and_devices)
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

  ChildProcess child_process{std::string{ viewer_path }, socket_addr_path.string(), driver_and_devices};

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
    for (unsigned int i{0}; i < 3; ++i)
    {
      const auto bytes_read{read_fd(client_fd, buffer.data(), buffer.size(), file_fd)};
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
      auto sub_connection_data{ConnectionData::parse(formatted_message)};
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
  IPCSemaphore semaphore{server_fd, client_fd};
  return std::tuple{std::move(connection_data), std::move(child_process), std::move(semaphore)};
}
#endif
