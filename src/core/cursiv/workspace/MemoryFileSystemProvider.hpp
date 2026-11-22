#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/workspace/FileSystemProvider.hpp"

// A file system in memory: a map from URI to text, enumerated by prefix. URIs
// of any scheme, kept as `normalizeUri` spells them. Reads may run on several
// threads at once, while mutations require exclusive access.

namespace cursiv {

class MemoryFileSystemProvider : public FileSystemProvider {
 public:
  // Adds or replaces one file, under `normalizeUri(uri)`: the spelling the
  // workspace keys its documents by, whatever case the scheme came in.
  void addFile(std::string_view uri, std::string text);

  // Erases a file or an entire directory subtree.
  void erase(std::string_view uri);

  [[nodiscard]] FileSystemNode stat(std::string_view uri) const override;
  [[nodiscard]] bool exists(std::string_view uri) const override;
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override;
  [[nodiscard]] std::string readFile(std::string_view uri) const override;

  // One level of the prefix hierarchy: the files directly under `uri`, and one
  // synthesized directory per distinct next segment -- a map of flat keys has
  // no folders, so the descent is derived from them.
  [[nodiscard]] std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const override;

 private:
  // Whether any key sits under `uri` as a folder. What makes a synthesized
  // directory answer `stat` and `exists` as one.
  [[nodiscard]] bool isFolder(std::string_view uri) const;

  // `std::map` and not a hash map: the enumeration order is the contract the
  // interface states, and an ordered container is that contract for free.
  std::map<std::string, std::string, std::less<>> _files;
};

}  // namespace cursiv
