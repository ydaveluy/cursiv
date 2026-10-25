#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace cursiv {

namespace {

std::filesystem::path local_path(std::string_view uri) {
  const auto path = file_uri_to_path(uri);
  if (!path.has_value())
    throw FileSystemError(std::format("cursiv: '{}' names no local path", uri));
  return std::filesystem::path(*path);
}

// Use the reported size for the first allocation, then read to EOF. A file
// may grow after stat, and some virtual files report zero for nonempty data.
template <class Bytes>
Bytes read_whole(std::string_view uri, std::ios::openmode mode) {
  const std::filesystem::path path = local_path(uri);
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec)
    throw FileSystemError(
        std::format("cursiv: cannot read '{}': {}", uri, ec.message()));
  if (size > Bytes{}.max_size() ||
      size > static_cast<std::uintmax_t>(
                 std::numeric_limits<std::streamsize>::max()))
    throw FileSystemError(std::format("cursiv: '{}' is too large to read", uri));
  std::ifstream in(path, mode);
  if (!in) throw FileSystemError(std::format("cursiv: cannot read '{}'", uri));
  Bytes bytes(static_cast<std::size_t>(size), typename Bytes::value_type{});
  in.read(reinterpret_cast<char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<std::size_t>(in.gcount()));
  // A short read at EOF is normal (also for text-mode newline conversion).
  // Other failures must not turn a partial buffer into a successful read.
  std::array<char, 8192> tail;
  for (;;) {
    if (in.bad() || (in.fail() && !in.eof()))
      throw FileSystemError(std::format("cursiv: cannot read '{}'", uri));
    if (in.eof()) break;
    in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    const auto* begin =
        reinterpret_cast<const typename Bytes::value_type*>(tail.data());
    bytes.insert(bytes.end(), begin, begin + in.gcount());
  }
  return bytes;
}

}  // namespace

FileSystemNode LocalFileSystemProvider::stat(std::string_view uri) const {
  const std::filesystem::path path = local_path(uri);
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec)
    throw FileSystemError(
        std::format("cursiv: cannot stat '{}': {}", uri, ec.message()));
  return {std::filesystem::is_regular_file(status),
          std::filesystem::is_directory(status),
          path_to_file_uri(path.string())};
}

// False for what is absent; it throws only when it cannot tell.
bool LocalFileSystemProvider::exists(std::string_view uri) const {
  std::error_code ec;
  const bool there = std::filesystem::exists(local_path(uri), ec);
  if (ec)
    throw FileSystemError(std::format(
        "cursiv: cannot tell whether '{}' exists: {}", uri, ec.message()));
  return there;
}

std::vector<std::uint8_t> LocalFileSystemProvider::readBinary(
    std::string_view uri) const {
  return read_whole<std::vector<std::uint8_t>>(uri, std::ios::binary);
}

std::string LocalFileSystemProvider::readFile(std::string_view uri) const {
  return read_whole<std::string>(uri, std::ios::in);
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
    nodes.push_back({file, directory, path_to_file_uri(it->path().string())});
  }
  if (ec)
    throw FileSystemError(
        std::format("cursiv: cannot list '{}': {}", uri, ec.message()));
  return nodes;
}

}  // namespace cursiv
