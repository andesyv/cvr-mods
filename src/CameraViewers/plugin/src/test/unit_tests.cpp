#include <string_view>
#include <string>
#include <optional>
#include <regex>

#include <gtest/gtest.h>

#include "ipc.hpp"
#include "utils.hpp"

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

  const auto connection_data = ConnectionData::parse(mock_child_process_std_output);
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