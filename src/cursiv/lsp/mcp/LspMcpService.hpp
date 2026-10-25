#pragma once

#include <memory>

#include "cursiv/mcp/DefaultMcpService.hpp"

// The default MCP tools plus what only this storey can serve -- today the
// formatter, an `LspServices` slot. This is also the extension pattern a
// language follows: subclass, override `createServer`, register on top of
// what `super` returns.

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
