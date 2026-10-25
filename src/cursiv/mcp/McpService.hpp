#pragma once

#include <memory>
#include <string>

// A server per session: `createServer` builds the endpoint with every tool
// registered, and the transport above drives it.

namespace cursiv {

class McpServer;

struct McpServiceOptions {
  std::string name;
  std::string version;
};

/**
 * Builds one `McpServer` per session. The service outlives every server it
 * creates.
 */
class McpService {
 public:
  virtual ~McpService() noexcept = default;

  [[nodiscard]] virtual std::unique_ptr<McpServer> createServer() const = 0;
};

}  // namespace cursiv
