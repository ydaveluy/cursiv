// A workspace with no disk under it: the URIs a test puts in, answered as
// files, and the folders between them answered as directories -- the trie
// is what makes the prefix question one walk.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/UriTrie.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace cursiv::test {

class VirtualFileSystemProvider final : public FileSystemProvider {
 public:
  void insert(std::string_view uri, std::string content);
  // The subtree goes with it: erasing a folder erases what it holds.
  void erase(std::string_view uri);

  [[nodiscard]] FileSystemNode stat(std::string_view uri) const override;
  [[nodiscard]] bool exists(std::string_view uri) const override;
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override;
  [[nodiscard]] std::string readFile(std::string_view uri) const override;
  [[nodiscard]] std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const override;

 private:
  // The URI travels with the content: a directory listing names its children,
  // and the trie's prefix query answers values rather than paths.
  struct Entry {
    std::string uri;
    std::string content;
  };

  [[nodiscard]] bool isDirectory(std::string_view uri) const;

  UriTrie<Entry> _trie;
};

}  // namespace cursiv::test
