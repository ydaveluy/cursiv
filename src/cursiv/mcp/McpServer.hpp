#pragma once

#include <lsp/json/json.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"

// The MCP endpoint: tools and resources registered by a service, one JSON-RPC
// message in, at most one response out. Transport-agnostic -- a stdio loop, an
// LSP tunnel and a test feed the same door.

namespace cursiv {

// `inputSchema` is the JSON Schema of the call's arguments; null means "any
// object". `readOnlyHint` defaults to the truth about every default tool --
// even `validate` leaves the workspace as it found it.
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

  void registerTool(McpTool tool, McpToolFunction execute);
  void registerResource(McpResource resource, McpResourceFunction read);

  /**
   * Adds one paragraph to the guide the handshake hands the model; each
   * call appends. Documents the workflow, not the tool list -- the tools
   * describe themselves.
   */
  void addInstructions(std::string_view paragraph);

  /**
   * Handles one JSON-RPC message and returns the response, or nothing for
   * a notification. Const: registrations all precede the first message, so
   * concurrent requests read a registry nobody writes.
   */
  [[nodiscard]] std::optional<lsp::json::Value> handle(
      lsp::json::Value message, const CancellationToken& = {}) const;
  /**
   * The same door as `handle`, over serialized text; a parse failure
   * answers as JSON-RPC requires, so a transport never inspects what it
   * forwards.
   */
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
