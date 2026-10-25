# Contributor guide: LSP services

This directory implements Cursiv's Language Server Protocol (LSP) support.
To add editor support to your language, follow
[Editor integration](../../../docs/learn/editor-integration.md).

## Dependency boundary

Protocol-facing APIs use types from `<lsp/types.h>`, such as `lsp::Position`
and `lsp::Range`. Keep these dependencies in `cursiv::lsp`; core services use
Cursiv's text types and must remain independent of the protocol.

When adding a service interface, place it in the library that consumes it.
Check the boundary from the repository root:

```sh
rg 'cursiv/lsp|<lsp/' src/cursiv/core -g '*.hpp' -g '*.cpp'
```

The search should return no matches.
