#pragma once

#include <lsp/json/json.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/mcp/McpServer.hpp"

// The tool-authoring kit: what a service -- this tree's or a language's own
// -- builds a door with. Schemas, argument access, uri normalization, and
// the document spine every positional tool shares.

namespace cursiv {

struct CoreServices;
struct Document;
struct SharedCoreServices;

[[nodiscard]] lsp::json::Value tool_property(std::string_view type,
                                             std::string_view description);
[[nodiscard]] lsp::json::Value tool_object_schema(lsp::json::Object properties,
                                                  lsp::json::Array required);
/**
 * `file` (required), `line` and `column` (1-based, utf-16 columns).
 */
[[nodiscard]] lsp::json::Value tool_position_schema();

[[nodiscard]] const lsp::json::Value* tool_argument(
    const lsp::json::Value& arguments, std::string_view key);

/**
 * A path becomes an absolute file:// uri; anything already a uri passes.
 */
[[nodiscard]] std::string tool_file_uri(std::string_view file);

// The three positional arguments, resolved to the store's vocabulary --
// 0-based here, 1-based on the wire.
struct McpToolPosition {
  std::string uri;
  std::uint32_t line = 0;
  std::uint32_t column = 0;
};
/**
 * Resolves the three positional arguments. Throws a model-readable message
 * on a miss; the endpoint turns it into an `isError` result.
 */
[[nodiscard]] McpToolPosition tool_position(const lsp::json::Value& arguments);

/**
 * `line:column`, 1-based, the convention every answer speaks.
 */
void append_tool_position(std::string& out, const Document& document,
                          TextOffset offset);

/**
 * The document-tool spine: the rung awaited off the lock, the admission
 * re-asked inside, the language resolved from the uri. `body` appends to
 * the answer; the caller words what an empty one means.
 */
[[nodiscard]] McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri, DocumentState rung,
    const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)> body);

}  // namespace cursiv
