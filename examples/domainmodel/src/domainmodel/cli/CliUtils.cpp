#include "domainmodel/cli/CliUtils.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace dm {

namespace {

std::string sanitize_file_stem(std::string_view filePath) {
  std::string stem = std::filesystem::path(filePath).stem().string();
  const auto [cut, end] =
      std::ranges::remove_if(stem, [](char c) { return c == '.' || c == '-'; });
  stem.erase(cut, end);
  return stem.empty() ? std::string("generated") : stem;
}

}  // namespace

FilePathData extract_destination_and_name(
    std::string_view filePath, std::optional<std::string_view> destination) {
  return {.destination = destination.has_value() ? std::string(*destination)
                                                 : std::string("generated"),
          .name = sanitize_file_stem(filePath)};
}

}  // namespace dm
