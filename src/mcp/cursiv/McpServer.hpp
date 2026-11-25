#pragma once

#include <lsp/json/json.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

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

// Omitted arguments reach the tool as an empty object.
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

  // Appends a paragraph to the instructions returned by server/discover.
  void addInstructions(std::string_view paragraph);

  // Handles a message after registration. A transport sharing this server
  // between clients can supply the version of this request.
  [[nodiscard]] std::optional<lsp::json::Value> handle(
      lsp::json::Value message, const CancellationToken& = {},
      std::string_view transportVersion = {}) const;
  // `handle` over serialized text; a parse failure answers as JSON-RPC
  // requires.
  [[nodiscard]] std::optional<std::string> handleText(
      std::string_view text, const CancellationToken& = {}) const;

  // Serves newline-delimited JSON until EOF, draining accepted requests.
  // Returns false when a response could not be written.
  [[nodiscard]] bool serveStdio(std::FILE* input, std::FILE* output) const;

 private:
  template <class Handler>
  struct Entry {
    lsp::json::Object descriptor;
    Handler handle;
    std::size_t order;
  };

  std::string _name;
  std::string _version;
  std::string _instructions;
  // The handshake selects a static version for unversioned messages on this
  // connection.
  alignas(std::atomic_ref<const char*>::required_alignment) mutable const
      char* _negotiated = nullptr;
  std::unordered_map<std::string, Entry<McpToolFunction>> _tools;
  std::unordered_map<std::string, Entry<McpResourceFunction>> _resources;

  [[nodiscard]] std::optional<lsp::json::Value> handleMessage(
      const lsp::json::Value& message, const CancellationToken& token,
      std::string_view wire, bool rememberVersion) const;
};

}  // namespace cursiv
