#pragma once

#include <lsp/types.h>

#include <pegium/core/workspace/DocumentUpdate.hpp>
#include <pegium/core/workspace/TextDocument.hpp>

namespace lsp {
class MessageHandler;
}

namespace pegium::workspace {
class TextDocumentProvider;
}

namespace pegium {

/// Converts one pegium diagnostic to its LSP form, using `positionDocument` to
/// turn text offsets into line/character positions. Related-information entries
/// whose `uri` names another document resolve their range against that document
/// via `crossFileProvider`; when it is absent or cannot resolve the entry, the
/// range is `{0,0}` rather than a wrong position computed against
/// `positionDocument`. All arguments are read synchronously and need not
/// outlive the call.
[[nodiscard]] ::lsp::Diagnostic
to_lsp_diagnostic(const workspace::TextDocument &positionDocument,
                  const Diagnostic &diagnostic,
                  const workspace::TextDocumentProvider *crossFileProvider =
                      nullptr);

/// Publishes one diagnostics snapshot to the connected client.
/// `crossFileProvider` resolves cross-file related-information ranges.
void publish_diagnostics(
    ::lsp::MessageHandler *messageHandler,
    const workspace::DocumentDiagnosticsSnapshot &snapshot,
    const workspace::TextDocumentProvider *crossFileProvider = nullptr);

} // namespace pegium
