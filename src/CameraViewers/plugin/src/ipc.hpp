#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>
#include <vector>

#ifdef __linux__
#include <array>
#include <atomic>
#include <future>
#endif

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

    constexpr auto operator<=>(const ImageData&) const = default;
  };

  Type type;
  std::string identifier;
#if defined(__linux__) || defined(UNIT_TEST_MOCK)
  int handle;
#elif _WIN32
  HANDLE handle;
#endif
  std::string handle_type;
  std::optional<ImageData> image_data;

  static std::vector<ConnectionData> parse(std::string_view child_output);

  constexpr auto operator<=>(const ConnectionData&) const = default;
};

void print_child_pipe(std::string_view child_string_output);

#ifdef _WIN32
std::pair<std::shared_ptr<HANDLE>, std::shared_ptr<HANDLE>> create_pipe();
std::optional<PROCESS_INFORMATION> create_child_process(std::string_view cmd, std::shared_ptr<HANDLE> std_out);
std::vector<ConnectionData> fetch_connection_data_from_child_channel(HANDLE* child_pipe);
#elif __linux__
class IPCSemaphore
{
private:
  int server_fd{-1};
  int client_fd{-1};
  std::array<char, 10> dummy_buffer{};

public:
  IPCSemaphore() = default;
  IPCSemaphore(const IPCSemaphore&) = delete;

  IPCSemaphore(IPCSemaphore&& rhs) noexcept;

  IPCSemaphore(int _server_fd, int _client_fd);

  IPCSemaphore& operator=(const IPCSemaphore&) = delete;

  IPCSemaphore& operator=(IPCSemaphore&& rhs) noexcept;

  void wait();

  void signal();

  ~IPCSemaphore();
};

class ChildProcess
{
private:
  std::atomic<FILE*> process_stream{nullptr};
  std::future<int> return_code;

public:
  ChildProcess() = default;
  ChildProcess(const ChildProcess&) = delete;

  ChildProcess(ChildProcess&& rhs);

  ChildProcess& operator=(const ChildProcess&) = delete;

  ChildProcess& operator=(ChildProcess&& rhs);

  explicit ChildProcess(const std::string& viewer_path, const std::string& socket_addr_path, std::string_view driver_and_devices);

  int close();

  ~ChildProcess();
};

std::optional<std::tuple<std::vector<ConnectionData>, ChildProcess, IPCSemaphore>>
init_child_process_and_fetch_connection_data(std::string_view viewer_path, std::string_view driver_and_devices);
#endif
