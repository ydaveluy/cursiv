#include "cursiv/mcp/McpToolKit.hpp"

#include <lsp/json/json.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"

namespace cursiv {

namespace json = lsp::json;

json::Value tool_property(std::string_view type, std::string_view description) {
  json::Object out;
  out["type"] = json::Value(json::String(type));
  out["description"] = json::Value(json::String(description));
  return json::Value(std::move(out));
}

json::Value tool_object_schema(json::Object properties, json::Array required) {
  json::Object out;
  out["type"] = json::Value(json::String("object"));
  out["properties"] = json::Value(std::move(properties));
  if (!required.empty()) out["required"] = json::Value(std::move(required));
  return json::Value(std::move(out));
}

json::Value tool_position_schema() {
  json::Object properties;
  properties["file"] =
      tool_property("string", "path or file:// uri of the document");
  properties["line"] = tool_property("integer", "1-based line");
  properties["column"] =
      tool_property("integer", "1-based column, in utf-16 units");
  json::Array required;
  required.emplace_back(json::String("file"));
  required.emplace_back(json::String("line"));
  required.emplace_back(json::String("column"));
  return tool_object_schema(std::move(properties), std::move(required));
}

const json::Value* tool_argument(const json::Value& arguments,
                                 std::string_view key) {
  return arguments.isObject() ? arguments.object().find(key) : nullptr;
}

std::string tool_file_uri(std::string_view file) {
  if (file.find("://") != std::string_view::npos) return std::string(file);
  return path_to_file_uri(file);
}

McpToolPosition tool_position(const json::Value& arguments) {
  const json::Value* const file = tool_argument(arguments, "file");
  const json::Value* const line = tool_argument(arguments, "line");
  const json::Value* const column = tool_argument(arguments, "column");
  if (file == nullptr || !file->isString())
    throw std::invalid_argument("'file' names the document");
  if (line == nullptr || !line->isInteger() || line->integer() < 1)
    throw std::invalid_argument("'line' is 1-based");
  if (column == nullptr || !column->isInteger() || column->integer() < 1)
    throw std::invalid_argument("'column' is 1-based");
  return {tool_file_uri(file->string()),
          static_cast<std::uint32_t>(line->integer() - 1),
          static_cast<std::uint32_t>(column->integer() - 1)};
}

void append_tool_position(std::string& out, const Document& document,
                          TextOffset offset) {
  const Position position = document.textDocument().positionAt(offset);
  out += std::to_string(position.line + 1);
  out += ':';
  out += std::to_string(position.character + 1);
}

McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri, DocumentState rung,
    const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)>
        body) {
  // The store is a write's to rehash: its lookup is a read like any other,
  // and over before the wait.
  DocumentId id = kNoDocument;
  if (!shared.workspaceLock->read(
          [&] { id = shared.documents->getDocumentId(uri); }, token,
          WorkspaceLock::ReadPriority::Immediate))
    return {"the request was cancelled", true};
  if (id == kNoDocument)
    return {uri + " is not in the workspace; the 'documents' tool lists it",
            true};
  if (shared.documentBuilder->waitUntil(rung, id, token) !=
      WaitOutcome::Reached)
    return {"the document never reached the state this tool reads at", true};
  std::string out;
  (void)shared.workspaceLock->read(
      [&] {
        const std::shared_ptr<Document> document =
            shared.documents->getDocument(id);
        if (document == nullptr || document->state.load() < rung) return;
        if (document->language == nullptr) return;
        body(*document, *document->language, out);
      },
      token, WorkspaceLock::ReadPriority::Immediate);
  return {std::move(out)};
}

}  // namespace cursiv
