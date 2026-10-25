#pragma once

#include <memory>

#include "cursiv/mcp/McpService.hpp"

// The default tool set over one workspace. Every tool is a facade on the core
// services; a language subclasses, overrides `createServer` and registers its
// own on top of what `super` returns.

namespace cursiv {

class McpServer;
struct SharedCoreServices;

class DefaultMcpService : public McpService {
 public:
  DefaultMcpService(SharedCoreServices& shared, McpServiceOptions options);

  [[nodiscard]] std::unique_ptr<McpServer> createServer() const override;

 protected:
  // One method per default tool, so a language keeps some and replaces others.
  virtual void registerDiagnosticsTool(McpServer& server) const;
  virtual void registerValidateTool(McpServer& server) const;
  virtual void registerDocumentsTool(McpServer& server) const;
  virtual void registerTextDocumentContentTool(McpServer& server) const;
  virtual void registerDocumentSymbolsTool(McpServer& server) const;
  virtual void registerSymbolsTool(McpServer& server) const;
  virtual void registerReferencesTool(McpServer& server) const;
  virtual void registerHoverTool(McpServer& server) const;
  virtual void registerRenameTool(McpServer& server) const;
  virtual void registerExpectedAtTool(McpServer& server) const;
  virtual void registerScopeCandidatesTool(McpServer& server) const;
  virtual void registerLanguagesTool(McpServer& server) const;
  // One resource per language whose module serves a `GrammarSource`.
  virtual void registerGrammarResources(McpServer& server) const;
  virtual void registerGrammarTool(McpServer& server) const;
  virtual void registerRuleTool(McpServer& server) const;

  SharedCoreServices& _shared;
  McpServiceOptions _options;
};

}  // namespace cursiv
