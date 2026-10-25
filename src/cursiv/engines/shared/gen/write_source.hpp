// A generated file whose content did not move is not touched, or the build
// would recompile a tree that did not move.
#pragma once

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace cursiv {

inline bool write_source(const std::string& path, const std::string& text) {
  std::error_code unread;
  if (std::filesystem::file_size(path, unread) == text.size()) {
    std::ifstream in(path, std::ios::binary);
    std::string old(text.size(), '\0');
    if (in.read(old.data(), static_cast<std::streamsize>(old.size())) &&
        old == text)
      return true;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << text;
  // A full disk fails on the close, not on the write.
  out.close();
  return static_cast<bool>(out);
}

}  // namespace cursiv
