#include "cursiv/McpToolKit.hpp"

#include <lsp/json/json.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
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

namespace {

McpToolResult cancelled() { return {"the request was cancelled", true}; }

McpToolResult not_held(const std::string& uri) {
  return {uri + " is not in the workspace; the 'documents' tool lists it",
          true};
}

}  // namespace

// `body` runs once, on a document standing at the rung -- and for a workspace
// requirement, in a workspace standing there too. Every other end is an error
// the model reads, never an empty answer the caller would word as "nothing
// found".
McpToolResult route_document_tool(
    SharedCoreServices& shared, const std::string& uri,
    ServiceRequirement requirement, const CancellationToken& token,
    cursiv::function_ref<void(const Document&, const CoreServices&,
                              std::string&)>
        body) {
  const DocumentState rung = requirement.state;
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
    const WaitOutcome reached = workspace ? builder.waitUntil(rung, token)
                                          : builder.waitUntil(rung, id, token);
    if (reached == WaitOutcome::Cancelled) return cancelled();
    if (reached != WaitOutcome::Reached)
      return {"the document never reached the state this tool reads at", true};
    enum class Admission : std::uint8_t { Behind, Ran, Gone, Lagging };
    Admission admission = Admission::Behind;
    std::string out;
    // The wait's verdict is stale by the time of admission: a write may have
    // reset the document, or removed it, in between. A document set back is
    // awaited again; a workspace rung is read under a queued read, which no
    // write runs beside, so the whole index stands for the whole answer.
    if (!shared.workspaceLock->read(
            [&] {
              const std::shared_ptr<Document> document =
                  shared.documents->getDocument(id);
              if (document == nullptr) {
                admission = Admission::Gone;
                return;
              }
              if (document->language == nullptr) {
                admission = Admission::Lagging;
                return;
              }
              if (workspace && builder.currentState() < rung) return;
              // Under a queued read the build is over: a document still
              // below the workspace's rung was left out of it.
              if (document->state.load() < rung) {
                if (workspace) admission = Admission::Lagging;
                return;
              }
              body(*document, *document->language, out);
              admission = Admission::Ran;
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
