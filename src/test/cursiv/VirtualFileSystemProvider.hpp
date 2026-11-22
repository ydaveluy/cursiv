#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cursiv/workspace/MemoryFileSystemProvider.hpp"

namespace cursiv::test {

class VirtualFileSystemProvider final : public MemoryFileSystemProvider {
 public:
  void insert(std::string_view uri, std::string content);
  [[nodiscard]] std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const override;
};

}  // namespace cursiv::test
