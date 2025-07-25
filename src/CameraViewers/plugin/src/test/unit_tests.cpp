#include <string_view>
#include <string>
#include <optional>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

#include <gtest/gtest.h>

struct ConnectionData {
  enum class Type {
    Semaphore,
    Image
  };

  Type type;
  std::string identifier;
// #ifdef _WIN32
//   HANDLE handle;
// #elif __linux__
  int handle;
// #endif
  std::string handle_type;
  std::optional<std::size_t> memory_allocation_size;
  std::optional<std::string> memory_image_format;

  constexpr auto operator<=>(const ConnectionData&) const = default;
};

std::vector<ConnectionData> fetch_connection_data_from_child_pipe_output(std::string_view child_pipe_output) {
  const static std::regex line_match{R"(Connection data: \{[^\}]+\})"};
  const static std::regex sub_match{R"(\"[a-zA-Z0-9_^\"]*\")"};
  std::vector<ConnectionData> data;

  for (std::regex_iterator it{child_pipe_output.begin(), child_pipe_output.end(), line_match}; it != decltype(it){}; ++it) {
    auto match{ *it };
    const auto sub_str{ match.str() };

    std::regex_iterator sub_match_it{sub_str.begin(), sub_str.end(), sub_match};
    const auto sub_matches = std::ranges::subrange{sub_match_it, decltype(sub_match_it){}} | std::views::transform([](auto match){
      std::string s{ match.str() };
      return s.substr(1, s.size() - 2);
    }) | std::ranges::to<std::vector<std::string>>();

    const int handle = std::stoi(sub_matches.at(3));
    data.push_back(ConnectionData {
      .type = sub_matches.at(0) == "semaphore" ? ConnectionData::Type::Semaphore : ConnectionData::Type::Image,
      .identifier = sub_matches.at(1),
      .handle = handle,
      .handle_type = sub_matches.at(2),
      .memory_allocation_size = 4 < sub_matches.size() ? std::make_optional<std::size_t>(std::stoull(sub_matches.at(4))) : std::nullopt,
      .memory_image_format = 5 < sub_matches.size() ? std::make_optional(sub_matches.at(5)) : std::nullopt,
    });
  }

  return data;
}

TEST(inter_op, connection_data_regex)
{
  const std::string test_str = R"(...
  Connection data: ?
  Connection data: {"semaphore", "OGL_begin", handle type: "OpaqueFd", "53"}
  ...)";
  std::smatch base_match;
  bool did_match = std::regex_search(test_str, base_match, std::regex{ R"(Connection data: \{[^\}]+\})" });
  EXPECT_TRUE(did_match);
  EXPECT_EQ(base_match.size(), 1);
  EXPECT_EQ(base_match[0].str(), R"(Connection data: {"semaphore", "OGL_begin", handle type: "OpaqueFd", "53"})");
}

TEST(inter_op, parsing_connection_data)
{
  const std::string_view mock_child_process_std_output = R"(...
  Handle Type: OpaqueWin32Kmt
  Properties: ExternalBufferProperties { external_memory_properties: ExternalMemoryProperties { dedicated_only: false, exportable: false, importable: false, export_from_imported_handle_types: empty(), compatible_handle_types: empty() } }
  Handle Type: OpaqueFd
  Properties: ExternalBufferProperties { external_memory_properties: ExternalMemoryProperties { dedicated_only: false, exportable: true, importable: true, export_from_imported_handle_types: OPAQUE_FD, compatible_handle_types: OPAQUE_FD } }
  Connection data: {"semaphore", "OGL_begin", handle type: "OpaqueFd", "53"}
  Connection data: {"semaphore", "OGL_end", handle type: "OpaqueFd", "55"}
  Connection data: {"image", "OGL_buffer", handle type: "OpaqueFd", "56", size: "4096000", format: "R16G16B16A16_UNORM" }
  Setup took 139 milliseconds to complete"
  ...)";

  const auto connection_data = fetch_connection_data_from_child_pipe_output(mock_child_process_std_output);
  EXPECT_EQ(connection_data.size(), 3);

  ConnectionData expected{
    .type = ConnectionData::Type::Semaphore,
    .identifier = "OGL_begin",
    .handle = 53,
    .handle_type = "OpaqueFd",
    .memory_allocation_size = std::nullopt,
    .memory_image_format = std::nullopt,
  };
  EXPECT_EQ(connection_data[0], expected);

  expected = ConnectionData{
    .type = ConnectionData::Type::Semaphore,
    .identifier = "OGL_end",
    .handle = 55,
    .handle_type = "OpaqueFd",
    .memory_allocation_size = std::nullopt,
    .memory_image_format = std::nullopt,
  };
  EXPECT_EQ(connection_data[1], expected);

  expected = ConnectionData{
    .type = ConnectionData::Type::Image,
    .identifier = "OGL_buffer",
    .handle = 56,
    .handle_type = "OpaqueFd",
    .memory_allocation_size = 4096000,
    .memory_image_format = "R16G16B16A16_UNORM",
  };
  EXPECT_EQ(connection_data[2], expected);
}