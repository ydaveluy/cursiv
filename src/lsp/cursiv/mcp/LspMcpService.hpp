#pragma once

#include <memory>

#include "cursiv/DefaultMcpService.hpp"

// The default MCP tools plus the formatter. A language extends it the same
// way: override `createServer` and register on top of the base's server.

namespace cursiv {

class McpServer;
struct SharedLspServices;

class LspMcpService : public DefaultMcpService {
 public:
  LspMcpService(SharedLspServices& shared, McpServiceOptions options);

  [[nodiscard]] std::unique_ptr<McpServer> createServer() const override;

 protected:
  virtual void registerFormatTool(McpServer& server) const;
};

}  // namespace cursiv
