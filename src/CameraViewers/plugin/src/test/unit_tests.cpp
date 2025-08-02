#include <string_view>
#include <string>
#include <optional>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

#include <gtest/gtest.h>

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

    constexpr auto operator<=> (const ImageData&) const noexcept = default;
  };

  Type type;
  std::string identifier;
  int handle;
  std::string handle_type;
  std::optional<ImageData> image_data;

  constexpr auto operator<=> (const ConnectionData&) const noexcept = default;
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

    const int handle = std::stoi(sub_matches.at(3));

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

TEST(inter_op, connection_data_regex)
{
  const std::string test_str = R"(...
  Connection data: ?
  Connection data: { type: "semaphore", identifier: "host_begin_sem", handle type: "OpaqueFd", handle id: "55" }
  ...)";
  std::smatch base_match;
  bool did_match = std::regex_search(test_str, base_match, std::regex{ R"(Connection data: \{[^\}]+\})" });
  EXPECT_TRUE(did_match);
  EXPECT_EQ(base_match.size(), 1);
  EXPECT_EQ(base_match[0].str(), R"(Connection data: { type: "semaphore", identifier: "host_begin_sem", handle type: "OpaqueFd", handle id: "55" })");
}

TEST(inter_op, parsing_connection_data)
{
  const std::string_view mock_child_process_std_output = R"(...
  Handle Type: OpaqueWin32Kmt
  Properties: ExternalBufferProperties { external_memory_properties: ExternalMemoryProperties { dedicated_only: false, exportable: false, importable: false, export_from_imported_handle_types: empty(), compatible_handle_types: empty() } }
  Handle Type: OpaqueFd
  Properties: ExternalBufferProperties { external_memory_properties: ExternalMemoryProperties { dedicated_only: false, exportable: true, importable: true, export_from_imported_handle_types: OPAQUE_FD, compatible_handle_types: OPAQUE_FD } }
  Connection data: { type: "semaphore", identifier: "host_begin_sem", handle type: "OpaqueFd", handle id: "55" }
  Connection data: { type: "semaphore", identifier: "host_end_sem", handle type: "OpaqueFd", handle id: "57" }
  Connection data: { type: "image", identifier: "shared_image", handle type: "OpaqueFd", handle id: "58", width: "800", height: "600", size: "4096000", format: "R16G16B16A16_UNORM" }
  Setup took 139 milliseconds to complete"
  ...)";

  const auto connection_data = parse_connection_data_from_child_output(mock_child_process_std_output);
  EXPECT_EQ(connection_data.size(), 3);

  ConnectionData expected{
    .type = ConnectionData::Type::Semaphore,
    .identifier = "host_begin_sem",
    .handle = 55,
    .handle_type = "OpaqueFd",
    .image_data = std::nullopt,
  };
  EXPECT_EQ(connection_data[0], expected);

  expected = ConnectionData{
    .type = ConnectionData::Type::Semaphore,
    .identifier = "host_end_sem",
    .handle = 57,
    .handle_type = "OpaqueFd",
    .image_data = std::nullopt,
  };
  EXPECT_EQ(connection_data[1], expected);

  expected = ConnectionData{
    .type = ConnectionData::Type::Image,
    .identifier = "shared_image",
    .handle = 58,
    .handle_type = "OpaqueFd",
    .image_data = ConnectionData::ImageData{
      .width = 800,
      .height = 600,
      .memory_allocation_size = 4096000,
      .memory_format = "R16G16B16A16_UNORM",
    },
  };
  EXPECT_EQ(connection_data[2], expected);
}

consteval std::byte operator ""_byte(unsigned long long val)
{
  // Now this is some cursed C++:
  if (std::numeric_limits<std::uint8_t>::max() <= val)
    throw std::runtime_error{ "Val too large"};
  return static_cast<std::byte>(val);
}

TEST(inter_op, serializing_uuid)
{
  constexpr std::array<std::byte, 16> empty{};
  auto sut = serialize_uuid(empty);
  EXPECT_EQ(sut, "00000000-0000-0000-0000-000000000000");

  constexpr std::array<std::byte, 16> uuid{ 44_byte, 195_byte, 118_byte, 88_byte, 33_byte, 162_byte, 78_byte, 104_byte, 143_byte, 181_byte, 30_byte, 15_byte, 202_byte, 219_byte, 116_byte, 35_byte };
  sut = serialize_uuid(uuid);
  EXPECT_EQ(sut, "2cc37658-21a2-4e68-8fb5-1e0fcadb7423");
}