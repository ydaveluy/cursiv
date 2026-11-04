#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// How a workspace reads what it did not open. One synchronous form, since
// nothing here awaits.

namespace cursiv {

// What an access could not do. `exists` answers false for an absent path and
// throws only when it cannot tell.
class FileSystemError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct FileSystemNode {
  bool isFile = false;
  bool isDirectory = false;
  std::string uri;
};

class FileSystemProvider {
 public:
  virtual ~FileSystemProvider() noexcept = default;

  [[nodiscard]] virtual FileSystemNode stat(std::string_view uri) const = 0;
  [[nodiscard]] virtual bool exists(std::string_view uri) const = 0;
  [[nodiscard]] virtual std::vector<std::uint8_t> readBinary(
      std::string_view uri) const = 0;
  [[nodiscard]] virtual std::string readFile(std::string_view uri) const = 0;
  [[nodiscard]] virtual std::vector<FileSystemNode> readDirectory(
      std::string_view uri) const = 0;
};

}  // namespace cursiv
