#pragma once

#include <lsp/json/json.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/McpServer.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"

// The tool-authoring kit for a service, this tree's or a language's own:
// schemas, argument access, uri normalization, and the document routing every
// positional tool shares.

namespace cursiv {

struct CoreServices;
struct Document;
struct SharedCoreServices;

// One property of an input schema: its JSON type and what it holds.
[[nodiscard]] lsp::json::Value tool_property(std::string_view type,
                                             std::string_view description);
// An object schema of `properties`, the names in `required` mandatory.
[[nodiscard]] lsp::json::Value tool_object_schema(lsp::json::Object properties,
                                                  lsp::json::Array required);
// The schema of a position: `file`, `line` and `column`, all required.
[[nodiscard]] lsp::json::Value tool_position_schema();
// The argument named `key`; null when the call did not pass it.
[[nodiscard]] const lsp::json::Value* tool_argument(
    const lsp::json::Value& arguments, std::string_view key);

/**
 * A path becomes an absolute file:// uri; anything already a uri passes.
 */
[[nodiscard]] std::string tool_file_uri(std::string_view file);

// A position as the store reads it: 0-based, where the call is 1-based.
struct McpToolPosition {
  std::string uri;
  std::uint32_t line = 0;
  std::uint32_t column = 0;
};
// The position the arguments name; a bad one throws a message the model reads.
[[nodiscard]] McpToolPosition tool_position(const lsp::json::Value& arguments);
// Appends `line:column`, 1-based, the form every answer uses.
void append_tool_position(std::string& out, const Document& document,
                          TextOffset offset);

namespace detail {

// An `integer` argument as JSON Schema reads one: a number with no fractional
// part, such as `1.0`, within `[minimum, INT32_MAX]`.
[[nodiscard]] std::optional<std::int32_t> tool_integer(
    const lsp::json::Value* value, std::int32_t minimum);

}  // namespace detail

// Awaits `requirement` (a `WorkspaceState` waits for the whole workspace),
// checks it again under the lock, then runs `body` once to append the answer.
// A cancelled request, or a document gone or never there, answers an error.
[[nodiscard]] McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri,
    ServiceRequirement requirement, const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)> body);

}  // namespace cursiv
