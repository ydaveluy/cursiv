#pragma once

#include <lsp/json/json.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"
#include "cursiv/McpServer.hpp"

// The tool-authoring kit: what a service -- this tree's or a language's own
// -- builds a door with. Schemas, argument access, uri normalization, and
// the document spine every positional tool shares.

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

/**
 * The document-tool spine: the requirement awaited off the lock, the
 * admission re-asked inside, the language the document was opened with.
 * A bare `DocumentState` waits for the document alone; a `WorkspaceState`
 * waits for the whole workspace, as an answer read from every document's
 * index (references, rename) must. `body` runs once, on the document at that
 * rung, and appends to the answer; the caller words what an empty one means.
 * A cancelled request, or a document that is gone or never gets there,
 * answers an error and `body` does not run.
 */
[[nodiscard]] McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri,
    ServiceRequirement requirement, const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)> body);

}  // namespace cursiv
