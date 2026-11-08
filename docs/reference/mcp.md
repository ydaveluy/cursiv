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

Request ids are strings or integers that fit in 32 bits.

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

None of these tools saves text or applies edits. Each grammar is also a text
resource at `cursiv://grammar/<languageId>`.

## Select a language or document

`grammar` and `rule` take a `languageId`. `validate` takes the `text` and a
`languageId` or a `file` name, whose extension or name selects the language.
It needs one of the two even when only one language is registered. With both,
`languageId` selects the language and `file` names the document. While
`validate` runs, its text is part of the workspace, so other requests can see
its declarations. The other tools take the `file` path or URI of a document
in the workspace.

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

A tool about one document can call `route_document_tool` from
`cursiv/McpToolKit.hpp`. It runs your function once the document reaches a
`DocumentState`, or once the whole workspace does when you pass a
`WorkspaceState` such as `WorkspaceState::IndexedReferences`, which a tool
that reads references needs.

The same header helps you describe a tool's arguments and read them:

| Helper | Result |
|---|---|
| `tool_property(type, description)` | One property of an input schema, such as `tool_property("string", "the rule's name")` |
| `tool_object_schema(properties, required)` | An object schema for `inputSchema`; the names in `required` are mandatory |
| `tool_position_schema()` | The input schema of a position: `file`, `line` and `column`, all required; lines and columns count from 1, columns in UTF-16 units |
| `tool_argument(arguments, key)` | The argument named `key`, or `nullptr` if the call does not pass it |
| `tool_file_uri(file)` | A `file://` URI for a path; a URI is returned as is |
| `tool_position(arguments)` | The `McpToolPosition` the arguments name: its URI, and its line and column from 0. A missing or bad argument throws, and the call answers an error the model reads |
| `append_tool_position(out, document, offset)` | Appends the `line:column` of an offset, from 1, as the built-in tools write it |

```cpp
lsp::json::Object properties;
properties["file"] =
    cursiv::tool_property("string", "path or file:// uri of the document");
lsp::json::Array required;
required.emplace_back(lsp::json::String("file"));
server->registerTool(
    {.name = "byte-count",
     .description = "How many bytes a document holds.",
     .inputSchema = cursiv::tool_object_schema(std::move(properties),
                                               std::move(required))},
    [&shared = _shared](const lsp::json::Value& arguments,
                        const cursiv::CancellationToken& token) {
      const lsp::json::Value* file = cursiv::tool_argument(arguments, "file");
      if (file == nullptr || !file->isString())
        return cursiv::McpToolResult{"'file' names the document", true};
      return cursiv::route_document_tool(
          shared, cursiv::tool_file_uri(file->string()),
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

Each tool name and resource URI can be registered once: registering it again
throws `std::invalid_argument`. To replace or remove a standard tool,
override the method that registers it, such as `registerRenameTool`.
