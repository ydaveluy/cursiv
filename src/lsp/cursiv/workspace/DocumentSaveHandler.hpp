// What the editor does around a save: tells the server before, asks it for
// edits, and tells it after. A client sends only what `saveOptions()` names.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// The save notifications a client is asked to send.
struct DocumentSaveOptions {
  bool willSave = false;
  bool willSaveWaitUntil = false;
  bool didSave = false;
};

class DocumentSaveHandler {
 public:
  virtual ~DocumentSaveHandler() noexcept = default;

  /**
   * The notifications announced at the handshake. Asked once.
   */
  [[nodiscard]] virtual DocumentSaveOptions saveOptions() const noexcept = 0;

  // The two notifications run in arrival order, with no lock held: a handler
  // that reads or writes the workspace takes the lock itself.
  virtual void willSaveDocument(const lsp::WillSaveTextDocumentParams&) {
    // nothing to do by default
  }
  virtual void didSaveDocument(const lsp::DidSaveTextDocumentParams&) {
    // nothing to do by default
  }

  // The edits the client applies, and waits for, before it saves an open
  // document. Called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> willSaveDocumentWaitUntil(
      const Document&, const lsp::WillSaveTextDocumentParams&,
      const CancellationToken&) const {
    return {};
  }
};

}  // namespace cursiv
