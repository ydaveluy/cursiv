# MCP server

The Model Context Protocol (MCP) server lets an assistant inspect and validate
your language documents.

## Connect to the server {#two-transports}

**Through LSP.** The language server answers `cursiv/mcp` requests on the
editor's connection, so tools see the editor's workspace, unsaved buffers
included. The scaffold's VS Code client provides this bridge.

**Through stdio.** `<id>_lsp --mcp --folder <dir>` starts a separate MCP
session over newline-delimited JSON-RPC. It scans the saved files in `<dir>`,
or in the current directory without `--folder`.

## Available tools {#the-tools}

| Tool | Returns |
|---|---|
| `languages` | which languages this server registered, and their extensions |
| `grammar` | the grammar as text |
| `rule` | one rule and the rules it depends on |
| `documents` | what the workspace holds |
| `text-document-content` | one document's text |
| `get-diagnostics` | workspace diagnostics, optionally restricted by directory |
| `validate` | parse a text, link it, run the checks — and discard it |
| `document-symbols` | one document's outline |
| `symbols` | a name search across the workspace |
| `references` | every site naming a declaration |
| `hover` | the declaration, type and documentation at a position |
| `rename` | the edits a rename would make, without checking naming rules |
| `expected-at` | what may be written at a position |
| `scope-candidates` | what a cross reference could bind to |
| `format` | the formatted document text; needs an installed formatter |

None of these tools saves text or applies edits. Each grammar is also a text
resource at `cursiv://grammar/<languageId>`.

## Select a language or document {#picking-the-language}

`grammar` and `rule` take a `languageId`. `validate` takes `text` plus a
`languageId` or a `file` name whose extension or name selects the language,
even when only one language is registered; with both, `languageId` selects the
language and `file` names the document. Other tools take an existing
document's `file` path or URI.

Positions use **1-based lines and columns**, with columns in UTF-16 code
units; ranges exclude their end.

## Advanced: add a tool {#installing-your-own}

The scaffold installs the standard service. To add tools, derive from
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

Call `install_mcp_service` while registering your language services, before
the runner starts a session. A core-only application derives from
`DefaultMcpService`, which has the same tools except `format`.
