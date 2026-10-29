#include "cursiv/lsp/mcp/LspMcpService.hpp"

#include <lsp/json/json.h>
#include <lsp/types.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/mcp/McpServer.hpp"
#include "cursiv/mcp/McpToolKit.hpp"

namespace cursiv {

namespace json = lsp::json;

LspMcpService::LspMcpService(SharedLspServices& shared,
                             McpServiceOptions options)
    : DefaultMcpService(shared, std::move(options)) {}

std::unique_ptr<McpServer> LspMcpService::createServer() const {
  std::unique_ptr<McpServer> server = DefaultMcpService::createServer();
  registerFormatTool(*server);
  server->addInstructions(
      "'format' answers the whole document brought to the language's shape.");
  return server;
}

void LspMcpService::registerFormatTool(McpServer& server) const {
  json::Object properties;
  properties["file"] =
      tool_property("string", "path or file:// uri of the document");
  properties["tabSize"] =
      tool_property("integer", "spaces per level (default 2)");
  properties["insertSpaces"] =
      tool_property("boolean", "spaces rather than tabs (default true)");
  json::Array required;
  required.emplace_back(json::String("file"));
  server.registerTool(
      {.name = "format",
       .title = "Format Document",
       .description =
           "The whole document reformatted to the language's shape, answered "
           "as the FULL new text -- nothing is applied to the workspace. "
           "A language without a formatter refuses.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = tool_argument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        bool hasFormatter = false;
        McpToolResult result = route_document_tool(
            shared, tool_file_uri(file->string()), DocumentState::Parsed, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const auto* const lspLanguage =
                  dynamic_cast<const LspServices*>(&language);
              const Formatter* const formatter =
                  lspLanguage == nullptr ? nullptr
                                         : lspLanguage->formatter.get();
              if (formatter == nullptr) return;
              hasFormatter = true;
              lsp::DocumentFormattingParams params;
              params.options.tabSize = 2;
              params.options.insertSpaces = true;
              if (const json::Value* const tabSize =
                      tool_argument(arguments, "tabSize");
                  tabSize != nullptr && tabSize->isInteger() &&
                  tabSize->integer() > 0)
                params.options.tabSize =
                    static_cast<lsp::Uint>(tabSize->integer());
              if (const json::Value* const insertSpaces =
                      tool_argument(arguments, "insertSpaces");
                  insertSpaces != nullptr && insertSpaces->isBoolean())
                params.options.insertSpaces = insertSpaces->boolean();
              const std::vector<lsp::TextEdit> edits =
                  formatter->formatDocument(document, params, token);
              std::vector<TextEdit> converted;
              converted.reserve(edits.size());
              for (const lsp::TextEdit& edit : edits)
                converted.emplace_back(from_lsp(edit.range), edit.newText);
              out =
                  TextDocument::applyEdits(document.textDocument(), converted);
            });
        if (!result.isError && !hasFormatter)
          return McpToolResult{"this language installs no formatter", true};
        return result;
      });
}

}  // namespace cursiv
