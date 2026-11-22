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
in the current directory without `--folder`, as they are when it starts:
restart it to see later changes on disk.

Cursiv supports MCP `2026-07-28` with `server/discover`. Clients using
`initialize` can select `2025-11-25`, `2025-06-18`, `2025-03-26` or
`2024-11-05`. Use stdio for `2024-11-05`. The editor bridge uses
Streamable HTTP.

## Available tools

| Tool | Returns |
|---|---|
| `languages` | the registered languages and their file extensions |
| `grammar` | the grammar as text |
| `rule` | one rule and the rules it depends on |
| `documents` | the documents of the workspace |
| `text-document-content` | one document's text |
| `get-diagnostics` | workspace diagnostics, optionally restricted by directory |
| `validate` | the diagnostics of a text, linked against the workspace and then discarded; every validation category runs |
| `document-symbols` | one document's outline, nested as in the editor |
| `symbols` | a name search across the workspace |
| `references` | every place that refers to a declaration |
| `hover` | the declaration, type and documentation at a position |
| `rename` | the edits a rename would make, without checking the language's naming rules |
| `expected-at` | what may be written at a position |
| `scope-candidates` | the declarations each reference at a position could resolve to |
| `format` | the formatted document text; needs an installed formatter |

Each grammar is also a text resource. Use the URI returned by `languages` or
`resources/list`. For example, the ID `my language` gives
`cursiv://grammar/my%20language`. The tools leave workspace files untouched.

## Select a language or document

`grammar` and `rule` take a `languageId`. `validate` takes the `text` and a
`languageId` or a `file` name, whose extension or name selects the language.
It needs one of the two even when only one language is registered. With both,
`languageId` selects the language and `file` names the document. While
`validate` runs, its text is part of the workspace, so other requests can see
its declarations. Document tools such as `hover` take the `file` path or URI
of a workspace document. Use `documents` to find its URI and `symbols` to
search exported names across files.

Positions use **1-based lines and columns**, with columns in UTF-16 code
units; ranges exclude their end.

## Advanced: add a tool

The language server installs the standard tools. To add tools, derive from
`LspMcpService` and extend `createServer`:

```cpp
#include <memory>

#include "cursiv/mcp/LspMcpService.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/McpServer.hpp"

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

A tool about one document can call `routeDocumentTool` from
`cursiv/McpToolKit.hpp`. It runs your function once the document reaches a
`DocumentState`, or once the whole workspace does when you pass a
`WorkspaceState` such as `WorkspaceState::kIndexedReferences`, which a tool
that reads references needs.

The same header helps you describe a tool's arguments and read them:

| Helper | Result |
|---|---|
| `toolProperty(type, description)` | One property of an input schema, such as `toolProperty("string", "the rule's name")` |
| `toolObjectSchema(properties, required)` | An object schema for `inputSchema`; the names in `required` are mandatory |
| `toolPositionSchema()` | The input schema of a position: `file`, `line` and `column`, all required; lines and columns count from 1, columns in UTF-16 units |
| `toolArgument(arguments, key)` | The argument named `key`, or `nullptr` if the call does not pass it |
| `toolFileUri(file)` | A `file://` URI for a path; a URI is returned as is |
| `toolPosition(arguments)` | The `McpToolPosition` the arguments name: its URI, and its line and column from 0. A missing or bad argument throws, and the call answers an error the model reads |
| `appendToolPosition(out, document, offset)` | Appends the `line:column` of an offset, from 1, as the built-in tools write it |

```cpp
lsp::json::Object properties;
properties["file"] =
    cursiv::toolProperty("string", "path or file:// uri of the document");
lsp::json::Array required;
required.emplace_back(lsp::json::String("file"));
server->registerTool(
    {.name = "byte-count",
     .description = "How many bytes a document holds.",
     .inputSchema = cursiv::toolObjectSchema(std::move(properties),
                                               std::move(required))},
    [&shared = sharedServices](const lsp::json::Value& arguments,
                        const cursiv::CancellationToken& token) {
      const lsp::json::Value* file = cursiv::toolArgument(arguments, "file");
      if (file == nullptr || !file->isString())
        return cursiv::McpToolResult{"'file' names the document", true};
      return cursiv::routeDocumentTool(
          shared, cursiv::toolFileUri(file->string()),
          cursiv::DocumentState::Parsed, token,
          [](const cursiv::Document& document, const cursiv::CoreServices&,
             std::string& out) {
            out = std::to_string(document.textDocument().getText().size());
          });
    });
```

Call `install_mcp_service` from the function that registers your languages
(`register_lsp_services` in the scaffold), so it runs before the server
starts. A core-only application derives from `DefaultMcpService`, which has
the same tools except `format`.

Give each tool a unique name and each resource a unique URI. To replace or
remove a standard tool, override its registration method, such as
`registerRenameTool`.
