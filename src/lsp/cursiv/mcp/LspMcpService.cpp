#include "cursiv/mcp/LspMcpService.hpp"

#include <lsp/json/json.h>
#include <lsp/types.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/McpServer.hpp"
#include "cursiv/McpToolKit.hpp"
#include "cursiv/formatting/Formatter.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/TextDocument.hpp"

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
      toolProperty("string", "path or file:// uri of the document");
  properties["tabSize"] =
      detail::positive_integer_property("spaces per level (default 2)");
  properties["insertSpaces"] =
      toolProperty("boolean", "spaces rather than tabs (default true)");
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = toolArgument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        const json::Value* tabSize = toolArgument(arguments, "tabSize");
        const auto width = tabSize ? detail::tool_integer(tabSize, 1) : 2;
        if (!width)
          throw std::invalid_argument(
              "'tabSize' must be a positive 32-bit integer");
        const json::Value* spaces = toolArgument(arguments, "insertSpaces");
        if (spaces && !spaces->isBoolean())
          throw std::invalid_argument("'insertSpaces' must be a boolean");
        bool hasFormatter = false;
        McpToolResult result = routeDocumentTool(
            shared, toolFileUri(file->string()), DocumentState::Parsed, token,
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
              params.textDocument.uri = lsp::DocumentUri::parse(document.uri);
              params.options.tabSize = static_cast<lsp::Uint>(*width);
              params.options.insertSpaces = !spaces || spaces->boolean();
              auto edits = formatter->formatDocument(document, params, token);
              std::vector<TextEdit> converted;
              converted.reserve(edits.size());
              for (lsp::TextEdit& edit : edits)
                converted.emplace_back(fromLsp(edit.range),
                                       std::move(edit.newText));
              out =
                  TextDocument::applyEdits(document.textDocument(), converted);
            });
        if (!result.isError && !hasFormatter)
          return McpToolResult{"this language installs no formatter", true};
        return result;
      });
}

}  // namespace cursiv
