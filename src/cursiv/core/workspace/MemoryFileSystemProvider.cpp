#include "cursiv/core/workspace/MemoryFileSystemProvider.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace cursiv {

namespace {

// Both spellings of one folder name it: `mem:///src` and `mem:///src/` differ
// by a byte and by nothing else.
[[nodiscard]] std::string folder_prefix(std::string_view uri) {
  std::string prefix(uri);
  if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
  return prefix;
}

}  // namespace

void MemoryFileSystemProvider::addFile(std::string uri, std::string text) {
  _files.insert_or_assign(std::move(uri), std::move(text));
}

bool MemoryFileSystemProvider::isFolder(std::string_view uri) const {
  const std::string prefix = folder_prefix(uri);
  const auto at = _files.lower_bound(prefix);
  return at != _files.end() && std::string_view(at->first).starts_with(prefix);
}

FileSystemNode MemoryFileSystemProvider::stat(std::string_view uri) const {
  if (_files.contains(uri)) return {true, false, std::string(uri)};
  if (isFolder(uri)) return {false, true, std::string(uri)};
  throw FileSystemError(std::format("cursiv: nothing at '{}'", uri));
}

bool MemoryFileSystemProvider::exists(std::string_view uri) const {
  return _files.contains(uri) || isFolder(uri);
}

std::vector<std::uint8_t> MemoryFileSystemProvider::readBinary(
    std::string_view uri) const {
  const std::string text = readFile(uri);
  return {text.begin(), text.end()};
}

std::string MemoryFileSystemProvider::readFile(std::string_view uri) const {
  const auto it = _files.find(uri);
  if (it == _files.end())
    throw FileSystemError(std::format("cursiv: cannot read '{}'", uri));
  return it->second;
}

std::vector<FileSystemNode> MemoryFileSystemProvider::readDirectory(
    std::string_view uri) const {
  std::vector<FileSystemNode> nodes;
  const std::string prefix = folder_prefix(uri);
  // The keys under one prefix are a contiguous run of an ordered map: the walk
  // starts at its edge and stops at the first key that leaves it.
  for (auto at = _files.lower_bound(prefix); at != _files.end(); ++at) {
    const std::string_view name(at->first);
    if (!name.starts_with(prefix)) break;
    const std::string_view rest = name.substr(prefix.size());
    if (rest.empty()) continue;
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos) {
      nodes.push_back({.isFile = true, .isDirectory = false, .uri = at->first});
      continue;
    }
    // A deeper key implies the folder between: synthesized once -- every key
    // under one child is contiguous too, so the previous node is the only
    // duplicate to test against.
    std::string child(name.substr(0, prefix.size() + slash));
    if (nodes.empty() || nodes.back().uri != child)
      nodes.push_back(
          {.isFile = false, .isDirectory = true, .uri = std::move(child)});
  }
  return nodes;
}

}  // namespace cursiv
