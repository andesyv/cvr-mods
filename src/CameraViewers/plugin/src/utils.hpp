#pragma once

#include <string>
#include <cstdint>

template <typename T>
  requires (sizeof(T) == 1)
std::string serialize_uuid(const std::array<T, 16>& bytes)
{
  constexpr static std::string_view lookup = "0123456789abcdef";

  std::string out;
  out.reserve(16 + 4); // 16 characters + 4 hyphens
  for (std::size_t i{0}, j{0}; j < bytes.size(); ++i, ++j)
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
