#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/workspace/FileSystemProvider.hpp"

// A folder that is a map: the same questions answered out of memory, a URI to
// its text and a prefix walk for the enumeration. It is the second real
// implementation that makes `FileSystemProvider` an interface rather than a
// spelling of `std::filesystem`, and it goes through one manager, one scan and
// one build like the disk does.
//
// The URIs are whatever the caller puts in, of any scheme, and `readDirectory`
// treats the folder as a plain prefix -- the whole of what a hierarchy is here.
// Not synchronized: one thread at a time touches a given instance.

namespace cursiv {

class MemoryFileSystemProvider final : public FileSystemProvider {
 public:
  // Adds or replaces one file. The URI is the key the store will use.
  void addFile(std::string uri, std::string text);

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
