# MCP server

Use the Model Context Protocol (MCP) to let an assistant inspect and validate
your language documents. The scaffolded server provides tools for grammar
lookup, diagnostics, navigation and formatting.

## Connect to the server {#two-transports}

**Through LSP.** The server handles `cursiv/mcp` requests on the editor's
existing connection. Tools see the same workspace and unsaved buffers.
The scaffold's VS Code client provides this bridge.

**Through stdio.** Run `<id>_lsp --mcp --folder <dir>` to start an independent
MCP session over standard input/output. It scans saved files in that folder;
omitting `--folder` scans the current directory. Messages are newline-delimited
JSON-RPC.

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
| `rename` | the edits a rename would make |
| `expected-at` | what may be written at a position |
| `scope-candidates` | what a cross reference could bind to |
| `format` | the full formatted document text, without applying it |

`format` is added by `LspMcpService` in both transports. It requires a formatter
installed for the document's language. `rename` returns proposed edit spans;
it does not check language-specific naming rules. Neither tool applies edits.

`validate` parses draft text against the current workspace, runs the checks
and removes its temporary document afterwards. It does not save the text.

Generated grammars are also exposed as text resources at
`cursiv://grammar/<languageId>`.

## Select a language or document {#picking-the-language}

For `validate`, pass `text` and either `languageId` or a `file` name that selects
the language by extension or registered file name. This choice is required
even when only one language is registered. `grammar` and `rule` require
`languageId` directly.

When both `languageId` and `file` are supplied to `validate`, `languageId`
selects the language and `file` supplies the temporary document's name.

Tools that inspect an existing document take its `file` path or URI. Positional
tools use **1-based lines and columns**, with columns measured in UTF-16 code
units and end-exclusive ranges.

## Advanced: add a tool {#installing-your-own}

You only need a custom service when adding your own MCP tools. The scaffold
uses the standard service automatically.

The runner keeps an existing `mcpService` or installs `LspMcpService` when the
slot is empty. Derive from `LspMcpService` to retain all the tools above and
add another in `createServer`:

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

Call the installation function while registering your language services,
before the runner starts a session. For a core-only application, derive from
`DefaultMcpService`; it supplies the same tools except `format`.
