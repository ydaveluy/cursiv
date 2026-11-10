#pragma once

#include <lsp/json/json.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"

// The MCP endpoint: tools and resources registered by a service, one JSON-RPC
// message in, at most one response out. A stdio loop, an LSP tunnel and a
// test all use the same entry.

namespace cursiv {

// `inputSchema` is the JSON Schema of the call's arguments; null means "any
// object". `readOnlyHint` defaults to true: every default tool, `validate`
// included, leaves the workspace as it found it.
struct McpTool {
  std::string name;
  std::string title = {};
  std::string description = {};
  lsp::json::Value inputSchema = {};
  bool readOnlyHint = true;
};

// A tool failure is a result, not a protocol error: the model reads it.
struct McpToolResult {
  std::string text;
  bool isError = false;
};

using McpToolFunction = std::function<McpToolResult(
    const lsp::json::Value& arguments, const CancellationToken&)>;

struct McpResource {
  std::string uri;
  std::string name;
  std::string description = {};
  std::string mimeType = {};
};

using McpResourceFunction = std::function<std::string()>;

class McpServer {
 public:
  McpServer(std::string name, std::string version);

  // One entry per tool name and resource uri: a second registration throws
  // `std::invalid_argument`. A service replaces a default tool by overriding
  // its register method.
  void registerTool(McpTool tool, McpToolFunction execute);
  void registerResource(McpResource resource, McpResourceFunction read);

  // Appends a paragraph to the guide the handshake gives the model: the
  // workflow, not the tools, which describe themselves.
  void addInstructions(std::string_view paragraph);

  // Handles one JSON-RPC message: its response, or nothing for a notification.
  // Registrations all precede the first message. A request id is a string or a
  // 32-bit integer.
  [[nodiscard]] std::optional<lsp::json::Value> handle(
      lsp::json::Value message, const CancellationToken& = {}) const;
  // `handle` over serialized text; a parse failure answers as JSON-RPC
  // requires.
  [[nodiscard]] std::optional<std::string> handleText(
      std::string_view text, const CancellationToken& = {}) const;

 private:
  struct ToolEntry {
    McpTool tool;
    McpToolFunction execute;
  };
  struct ResourceEntry {
    McpResource resource;
    McpResourceFunction read;
  };

  std::string _name;
  std::string _version;
  std::string _instructions;
  std::vector<ToolEntry> _tools;
  std::vector<ResourceEntry> _resources;

  [[nodiscard]] lsp::json::Value initializeResult(
      const std::optional<lsp::json::Value>& params) const;
  [[nodiscard]] lsp::json::Value listTools() const;
  [[nodiscard]] lsp::json::Value listResources() const;
};

}  // namespace cursiv
