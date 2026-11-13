#include "cursiv/McpToolKit.hpp"

#include <lsp/json/json.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/text/Position.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"

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
  // The domain `tool_position` accepts, said where a client reads it.
  const auto coordinate = [](std::string_view description) {
    json::Value property = tool_property("integer", description);
    property.object()["minimum"] = json::Value(json::Integer{1});
    property.object()["maximum"] =
        json::Value(std::numeric_limits<json::Integer>::max());
    return property;
  };
  properties["line"] = coordinate("1-based line");
  properties["column"] = coordinate("1-based column, in utf-16 units");
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

namespace {

// A scheme is a letter, then letters, digits, '+', '-' or '.', then ':' --
// `untitled:Note` and `file:/w/a` as much as `file:///w/a`. One letter is a
// drive, as in `C:\w`, so a path.
bool has_scheme(std::string_view text) {
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos || colon < 2 || !letter(text.front()))
    return false;
  return std::ranges::all_of(text.substr(1, colon - 1), [&](char c) {
    return letter(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' ||
           c == '.';
  });
}

}  // namespace

std::string tool_file_uri(std::string_view file) {
  if (has_scheme(file)) return std::string(file);
  return path_to_file_uri(file);
}

McpToolPosition tool_position(const json::Value& arguments) {
  const json::Value* const file = tool_argument(arguments, "file");
  const json::Value* const line = tool_argument(arguments, "line");
  const json::Value* const column = tool_argument(arguments, "column");
  if (file == nullptr || !file->isString())
    throw std::invalid_argument("'file' names the document");
  const std::optional<std::int32_t> lineNumber = detail::tool_integer(line, 1);
  if (!lineNumber.has_value()) throw std::invalid_argument("'line' is 1-based");
  const std::optional<std::int32_t> columnNumber =
      detail::tool_integer(column, 1);
  if (!columnNumber.has_value())
    throw std::invalid_argument("'column' is 1-based");
  return {tool_file_uri(file->string()),
          static_cast<std::uint32_t>(*lineNumber - 1),
          static_cast<std::uint32_t>(*columnNumber - 1)};
}

std::optional<std::int32_t> detail::tool_integer(const json::Value* value,
                                                 std::int32_t minimum) {
  if (value == nullptr) return std::nullopt;
  if (value->isInteger()) {
    const json::Integer number = value->integer();
    return number >= minimum ? std::optional(number) : std::nullopt;
  }
  if (!value->isDecimal()) return std::nullopt;
  const double number = value->decimal();
  // A NaN or an infinity fails the range test.
  if (!(number >= minimum &&
        number <= std::numeric_limits<std::int32_t>::max()) ||
      std::trunc(number) != number)
    return std::nullopt;
  return static_cast<std::int32_t>(number);
}

void append_tool_position(std::string& out, const Document& document,
                          TextOffset offset) {
  const Position position = document.textDocument().positionAt(offset);
  out += std::to_string(position.line + 1);
  out += ':';
  out += std::to_string(position.character + 1);
}

namespace {

McpToolResult cancelled() { return {"the request was cancelled", true}; }

McpToolResult not_held(const std::string& uri) {
  return {uri + " is not in the workspace; the 'documents' tool lists it",
          true};
}

}  // namespace

// `body` runs once, on a document at the requirement's state. Every other end
// is an error the model reads, never an empty answer.
McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri,
    ServiceRequirement requirement, const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)>
        body) {
  const DocumentState target = requirement.state;
  const bool workspace =
      requirement.type == ServiceRequirement::Type::Workspace;
  // The store is a write's to rehash: its lookup is a read like any other,
  // and over before the wait.
  DocumentId id = kNoDocument;
  if (!shared.workspaceLock->read(
          [&] {
            if (const std::shared_ptr<Document> document =
                    shared.documents->getDocument(uri))
              id = document->id;
          },
          token, WorkspaceLock::ReadPriority::Immediate))
    return cancelled();
  if (id == kNoDocument) return not_held(uri);
  const DocumentBuilder& builder = *shared.documentBuilder;
  for (;;) {
    const WaitOutcome reached = workspace
                                    ? builder.waitUntil(target, token)
                                    : builder.waitUntil(target, id, token);
    if (reached == WaitOutcome::Cancelled) return cancelled();
    if (reached != WaitOutcome::Reached)
      return {"the document never reached the state this tool reads at", true};
    enum class Admission : std::uint8_t { Behind, Ran, Gone, Lagging };
    Admission admission = Admission::Behind;
    std::string out;
    // Admission checks again: a write may have reset or removed the document
    // since the wait. A workspace state is read under a queued read, which no
    // write runs beside.
    if (!shared.workspaceLock->read(
            [&] {
              using enum Admission;
              const std::shared_ptr<Document> document =
                  shared.documents->getDocument(id);
              if (document == nullptr) {
                admission = Gone;
                return;
              }
              if (document->language == nullptr) {
                admission = Lagging;
                return;
              }
              if (workspace && builder.currentState() < target) return;
              // Under a queued read the build is over: a document still below
              // the workspace's state was left out of it.
              if (document->state.load() < target) {
                if (workspace) admission = Lagging;
                return;
              }
              body(*document, *document->language, out);
              admission = Ran;
            },
            token,
            workspace ? WorkspaceLock::ReadPriority::Normal
                      : WorkspaceLock::ReadPriority::Immediate))
      return cancelled();
    switch (admission) {
      case Admission::Ran:
        return {std::move(out)};
      case Admission::Gone:
        return not_held(uri);
      case Admission::Lagging:
        return {"the document never reached the state this tool reads at",
                true};
      case Admission::Behind:
        break;
    }
  }
}

}  // namespace cursiv
