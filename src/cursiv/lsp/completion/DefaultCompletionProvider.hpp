#pragma once

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/lsp/completion/CompletionContext.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"

// Not templated by the grammar: what may be written at a caret is the state
// the leaf before it carries, read through the parser, and what each answer is
// -- a literal's bytes, a token's name, its prose -- is the grammar model's
// answer through the same container.

namespace cursiv {

class Cst;
struct NextFeature;

class DefaultCompletionProvider : public CompletionProvider {
 public:
  // `LspServices` and not `CoreServices`: a reference candidate is filtered by
  // the fuzzy matcher and iconed by the node-kind provider, both of this
  // storey.
  explicit DefaultCompletionProvider(const LspServices& services);
  DefaultCompletionProvider(const DefaultCompletionProvider&) = delete;
  DefaultCompletionProvider& operator=(const DefaultCompletionProvider&) =
      delete;

  [[nodiscard]] lsp::CompletionList getCompletion(
      const Document& document, lsp::Position position,
      const CancellationToken& cancelToken = {}) const override;

 protected:
  // Whether a keyword is offered at all; by default every one is, punctuation
  // included.
  [[nodiscard]] virtual bool filterKeyword(const CompletionContext& context,
                                           std::string_view keyword) const;

  virtual void initialize(const lsp::InitializeParams& params);

  // Both ranges where the client chooses between inserting and replacing,
  // the insert range alone otherwise. Takes the ranges already measured for
  // the caret.
  [[nodiscard]] virtual lsp::OneOf<lsp::TextEdit, lsp::InsertReplaceEdit>
  buildCompletionTextEdit(lsp::InsertReplaceEdit edit) const;

  std::optional<lsp::CompletionClientCapabilities> clientCapabilities;

 private:
  // The scope's answer where the member being filled holds a reference, as
  // items. False when it holds none, so the caller falls back to naming the
  // token. `edit` carries the two ranges every item of this caret shares, and
  // each candidate fills in its own `newText`.
  [[nodiscard]] bool offerReferences(
      const Document& document, const NextFeature& next, AstNode* container,
      const lsp::InsertReplaceEdit& edit, std::string_view filter,
      std::unordered_set<std::string>& seen, lsp::CompletionList& list,
      const CancellationToken& cancelToken) const;

  const LspServices& services;
  // What the server's `onInitialize` callback reaches, gone with the
  // provider: a provider replaced before the handshake is not called.
  std::shared_ptr<DefaultCompletionProvider*> _self =
      std::make_shared<DefaultCompletionProvider*>(this);

  // Not the caret: a caret mid-word is still typing that token, and asked
  // there the answer would be what follows it, not what may replace it.
  // `from`/`to` bound the word (past the caret when editing one), `prefix`
  // is what has been typed of it. A word that spans lines is kept whole here;
  // the completion edit takes only its part on the caret's line.
  struct Anchor {
    TextOffset from = 0;
    TextOffset to = 0;
    std::string_view prefix{};
  };
  [[nodiscard]] static Anchor anchorAt(const Cst& cst, TextOffset offset);
};

}  // namespace cursiv
