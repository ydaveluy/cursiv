// The files a generator writes are replaced together: each is written beside
// its place, then all are renamed over their places, so a failed write changes
// nothing. A file whose content is unchanged is not touched.
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace cursiv::engine {

struct SourceFile {
  std::string path;
  std::string_view text;
};

// The place of the first file that failed to write, or `files.size()`.
inline std::size_t write_sources(std::span<const SourceFile> files) {
  const auto staged = [&](std::size_t i) { return files[i].path + ".tmp"; };
  std::vector<std::size_t> moved;
  // Drops the staged copies from the `from`-th moved file on, and returns
  // `failed`.
  const auto fail = [&](std::size_t from, std::size_t failed) {
    for (std::size_t k = from; k < moved.size(); ++k) {
      std::error_code ignored;
      std::filesystem::remove(staged(moved[k]), ignored);
    }
    return failed;
  };
  for (std::size_t i = 0; i < files.size(); ++i) {
    const SourceFile& file = files[i];
    if (std::error_code unread;
        std::filesystem::file_size(file.path, unread) == file.text.size()) {
      std::ifstream in(file.path, std::ios::binary);
      std::string old(file.text.size(), '\0');
      if (in.read(old.data(), static_cast<std::streamsize>(old.size())) &&
          old == file.text)
        continue;
    }
    moved.push_back(i);
    std::ofstream out(staged(i), std::ios::binary);
    out.write(file.text.data(), static_cast<std::streamsize>(file.text.size()));
    // A full disk fails on the close, not on the write.
    out.close();
    if (!out) return fail(0, i);
  }
  for (std::size_t k = 0; k < moved.size(); ++k) {
    std::error_code failed;
    std::filesystem::rename(staged(moved[k]), files[moved[k]].path, failed);
    if (failed) return fail(k, moved[k]);
  }
  return files.size();
}

}  // namespace cursiv::engine
