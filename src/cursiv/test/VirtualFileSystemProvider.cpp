#include "cursiv/test/VirtualFileSystemProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace cursiv::test {

namespace {

// Both spellings of one folder name it: `mem:///src` and `mem:///src/` differ
// by a byte and by nothing else.
std::string folder_prefix(std::string_view uri) {
  std::string prefix = normalize_uri(uri);
  if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
  return prefix;
}

}  // namespace

void VirtualFileSystemProvider::insert(std::string_view uri,
                                       std::string content) {
  _trie.insert(uri, Entry{normalize_uri(uri), std::move(content)});
}

void VirtualFileSystemProvider::erase(std::string_view uri) {
  _trie.erase(uri);
}

bool VirtualFileSystemProvider::isDirectory(std::string_view uri) const {
  return _trie.find(uri) == nullptr && !_trie.findAll(uri).empty();
}

FileSystemNode VirtualFileSystemProvider::stat(std::string_view uri) const {
  if (_trie.find(uri) != nullptr) return {true, false, normalize_uri(uri)};
  if (isDirectory(uri)) return {false, true, normalize_uri(uri)};
  throw FileSystemError("cursiv: nothing at '" + std::string(uri) + "'");
}

bool VirtualFileSystemProvider::exists(std::string_view uri) const {
  return _trie.find(uri) != nullptr || isDirectory(uri);
}

std::vector<std::uint8_t> VirtualFileSystemProvider::readBinary(
    std::string_view uri) const {
  const std::string text = readFile(uri);
  return {text.begin(), text.end()};
}

std::string VirtualFileSystemProvider::readFile(std::string_view uri) const {
  const Entry* const entry = _trie.find(uri);
  if (entry == nullptr)
    throw FileSystemError("cursiv: cannot read '" + std::string(uri) + "'");
  return entry->content;
}

std::vector<FileSystemNode> VirtualFileSystemProvider::readDirectory(
    std::string_view uri) const {
  const std::vector<Entry> under = _trie.findAll(uri);
  if (under.empty() && _trie.find(uri) == nullptr)
    throw FileSystemError("cursiv: no directory at '" + std::string(uri) + "'");

  const std::string prefix = folder_prefix(uri);
  std::vector<FileSystemNode> nodes;
  for (const Entry& entry : under) {
    const std::string_view name(entry.uri);
    if (!name.starts_with(prefix)) continue;
    const std::string_view rest = name.substr(prefix.size());
    if (rest.empty()) continue;
    const std::size_t slash = rest.find('/');
    // A deeper key implies the folder between: named once, whatever the walk's
    // order.
    std::string child = slash == std::string_view::npos
                            ? entry.uri
                            : prefix + std::string(rest.substr(0, slash));
    const bool isFile = slash == std::string_view::npos;
    if (std::ranges::none_of(nodes, [&child](const FileSystemNode& seen) {
          return seen.uri == child;
        }))
      nodes.push_back(
          {.isFile = isFile, .isDirectory = !isFile, .uri = std::move(child)});
  }
  std::ranges::sort(nodes, {}, &FileSystemNode::uri);
  return nodes;
}

}  // namespace cursiv::test
