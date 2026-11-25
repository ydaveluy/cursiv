#include "cursiv/workspace/LocalFileSystemProvider.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"

namespace cursiv {

namespace {

std::filesystem::path local_path(std::string_view uri) {
  const auto path = fileUriToPath(uri);
  if (!path.has_value())
    throw FileSystemError("cursiv: '" + std::string(uri) +
                          "' names no local path");
  return std::filesystem::path(*path);
}

struct FileCloser {
  void operator()(std::FILE* file) const { std::fclose(file); }
};

// Use the reported size for the first allocation, then read to EOF. A file
// may grow after stat, and some virtual files report zero for nonempty data.
template <class Bytes>
Bytes read_whole(std::string_view uri, std::size_t limit = Bytes{}.max_size()) {
  const std::filesystem::path path = local_path(uri);
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec)
    throw FileSystemError("cursiv: cannot read '" + std::string(uri) +
                          "': " + ec.message());
  limit = std::min(limit, Bytes{}.max_size());
  if (size > limit)
    throw FileSystemError("cursiv: '" + std::string(uri) +
                          "' is too large to read");
#ifdef _WIN32
  const std::unique_ptr<std::FILE, FileCloser> file(
      _wfopen(path.c_str(), L"rb"));
#else
  const std::unique_ptr<std::FILE, FileCloser> file(
      std::fopen(path.c_str(), "rb"));
#endif
  if (!file)
    throw FileSystemError("cursiv: cannot read '" + std::string(uri) + "'");
  Bytes bytes(static_cast<std::size_t>(size), typename Bytes::value_type{});
  bytes.resize(std::fread(bytes.data(), 1, bytes.size(), file.get()));
  // A short read at EOF is normal. Other failures must not turn a partial
  // buffer into a successful read.
  std::array<typename Bytes::value_type, 8192> tail;
  for (;;) {
    if (std::ferror(file.get()) != 0)
      throw FileSystemError("cursiv: cannot read '" + std::string(uri) + "'");
    if (std::feof(file.get()) != 0) break;
    const std::size_t count =
        std::fread(tail.data(), 1, tail.size(), file.get());
    if (count > limit - bytes.size())
      throw FileSystemError("cursiv: '" + std::string(uri) +
                            "' is too large to read");
    bytes.insert(bytes.end(), tail.begin(),
                 tail.begin() + static_cast<std::ptrdiff_t>(count));
  }
  return bytes;
}

}  // namespace

// The entry itself, as `readDirectory` lists it: a symlink is neither a file
// nor a folder, so an event about one selects what the scan selects.
FileSystemNode LocalFileSystemProvider::stat(std::string_view uri) const {
  const std::filesystem::path path = local_path(uri);
  std::error_code ec;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(path, ec);
  if (ec)
    throw FileSystemError("cursiv: cannot stat '" + std::string(uri) +
                          "': " + ec.message());
  return {std::filesystem::is_regular_file(status),
          std::filesystem::is_directory(status),
          pathToFileUri(path.string())};
}

// False for what is absent; it throws only when it cannot tell.
bool LocalFileSystemProvider::exists(std::string_view uri) const {
  std::error_code ec;
  const bool there = std::filesystem::exists(local_path(uri), ec);
  if (ec)
    throw FileSystemError("cursiv: cannot tell whether '" + std::string(uri) +
                          "' exists: " + ec.message());
  return there;
}

std::vector<std::uint8_t> LocalFileSystemProvider::readBinary(
    std::string_view uri) const {
  return read_whole<std::vector<std::uint8_t>>(uri);
}

std::string LocalFileSystemProvider::readFile(std::string_view uri) const {
  return read_whole<std::string>(uri, _maxTextBytes);
}

std::vector<FileSystemNode> LocalFileSystemProvider::readDirectory(
    std::string_view uri) const {
  std::error_code ec;
  std::vector<FileSystemNode> nodes;
  // Every step takes the error code: the plain forms throw
  // `filesystem_error`, which is not this interface's.
  for (auto it = std::filesystem::directory_iterator(local_path(uri), ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    std::error_code kind;
    // The entry itself, not what a symlink leads to: following a directory
    // link can loop forever. One entry that cannot be inspected is skipped,
    // never fatal to the scan.
    const auto status = it->symlink_status(kind);
    if (kind) continue;
    const bool file = std::filesystem::is_regular_file(status);
    const bool directory = std::filesystem::is_directory(status);
    nodes.emplace_back(file, directory, pathToFileUri(it->path().string()));
  }
  if (ec)
    throw FileSystemError("cursiv: cannot list '" + std::string(uri) +
                          "': " + ec.message());
  return nodes;
}

}  // namespace cursiv
