# MCP server

The Model Context Protocol (MCP) server lets an assistant inspect and validate
your language documents.

## Connect to the server

**Through LSP.** The language server handles `cursiv/mcp` requests on the
editor's connection, so the tools see the editor's workspace, including
unsaved changes. The scaffold's VS Code extension makes this server available
to assistants in VS Code.

**Through stdio.** `<id>_lsp --mcp --folder <dir>` starts a separate MCP
server on standard input and output. It reads the saved files in `<dir>`, or
in the current directory without `--folder`.

## Available tools

| Tool | Returns |
|---|---|
| `languages` | the registered languages and their file extensions |
| `grammar` | the grammar as text |
| `rule` | one rule and the rules it depends on |
| `documents` | the documents of the workspace |
| `text-document-content` | one document's text |
| `get-diagnostics` | workspace diagnostics, optionally restricted by directory |
| `validate` | the diagnostics of a text, linked against the workspace and then discarded |
| `document-symbols` | one document's outline |
| `symbols` | a name search across the workspace |
| `references` | every place that refers to a declaration |
| `hover` | the declaration, type and documentation at a position |
| `rename` | the edits a rename would make, without checking the language's naming rules |
| `expected-at` | what may be written at a position |
| `scope-candidates` | the declarations a reference could resolve to |
| `format` | the formatted document text; needs an installed formatter |

None of these tools saves text or applies edits. Each grammar is also a text
resource at `cursiv://grammar/<languageId>`.

## Select a language or document

`grammar` and `rule` take a `languageId`. `validate` takes the `text` and a
`languageId` or a `file` name, whose extension or name selects the language.
It needs one of the two even when only one language is registered. With both,
`languageId` selects the language and `file` names the document. The other
tools take the `file` path or URI of a document in the workspace.

Positions use **1-based lines and columns**, with columns in UTF-16 code
units; ranges exclude their end.

## Advanced: add a tool

The language server installs the standard tools. To add tools, derive from
`LspMcpService` and extend `createServer`:

```cpp
#include <memory>

#include "cursiv/lsp/mcp/LspMcpService.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/mcp/McpServer.hpp"

class MyMcpService final : public cursiv::LspMcpService {
 public:
  using LspMcpService::LspMcpService;

  std::unique_ptr<cursiv::McpServer> createServer() const override {
    auto server = LspMcpService::createServer();
    server->registerTool(
        {.name = "language-info",
         .title = "Language Info",
         .description = "Returns the language's version."},
        [](const lsp::json::Value&, const cursiv::CancellationToken&) {
          return cursiv::McpToolResult{"MyLang 0.1.0"};
        });
    return server;
  }
};

void install_mcp_service(cursiv::SharedLspServices& sharedServices) {
  sharedServices.mcpService = std::make_unique<MyMcpService>(
      sharedServices, cursiv::McpServiceOptions{"mylang", "0.1.0"});
}
```

Call `install_mcp_service` from the function that registers your languages
(`register_lsp_services` in the scaffold), so it runs before the server
starts. A core-only application derives from `DefaultMcpService`, which has
the same tools except `format`.
