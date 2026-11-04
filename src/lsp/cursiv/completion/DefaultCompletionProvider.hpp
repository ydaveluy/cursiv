#pragma once

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/completion/CompletionContext.hpp"
#include "cursiv/completion/CompletionProvider.hpp"
#include "cursiv/services/LspServices.hpp"

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
  // The ways of reading a caret, in the order they are tried: the data rule it
  // writes in, the token ending at it, what follows that token, and the token
  // holding it. `visit` returns whether to try the next.
  virtual void buildContexts(
      const Document& document, lsp::Position position,
      const CancellationToken& cancelToken,
      function_ref<bool(const CompletionContext&)> visit) const;

  // What may stand at `offset`, where the document's text resumes at `end`.
  virtual void findFeaturesAt(const Document& document, TextOffset offset,
                              TextOffset end,
                              std::vector<NextFeature>& features) const;

  // Whether what may replace a token ending at the caret is worth asking: by
  // default not where it spells no word. All that could match it is a keyword
  // spelling no letter, which `filterKeyword` leaves out, and the question costs
  // a walk of the document at every punctuation typed.
  [[nodiscard]] virtual bool performCurrentTokenCompletion(
      const Document& document, std::string_view text, TextOffset offset,
      TextOffset end) const;

  // Whether what follows a token ending at the caret is worth asking: not where
  // it ends in a letter, since it is still being typed.
  [[nodiscard]] virtual bool performNextTokenCompletion(
      const Document& document, std::string_view text, TextOffset offset,
      TextOffset end) const;

  // Where the outermost data rule a caret writes in begins and ends. The tokens
  // of such a rule say nothing of what may stand there.
  [[nodiscard]] virtual std::optional<std::pair<TextOffset, TextOffset>>
  findDataTypeRuleStart(const Cst& cst, TextOffset offset) const;

  [[nodiscard]] virtual CompletionBacktrackingInformation backtrackToAnyToken(
      const Cst& cst, TextOffset offset) const;

  // Whether the next way of reading the caret is tried, given the items so far.
  // By default only where there are none.
  [[nodiscard]] virtual bool continueCompletion(
      const std::vector<lsp::CompletionItem>& items) const;

  // By default one item per kind, label and detail.
  [[nodiscard]] virtual std::vector<lsp::CompletionItem> deduplicateItems(
      std::vector<lsp::CompletionItem> items) const;

  // What one feature offers: the candidates of the cross-reference it fills,
  // else its keywords. A feature that is neither, such as a terminal read by a
  // pattern, offers nothing.
  virtual void completionFor(const CompletionContext& context,
                             const NextFeature& next,
                             CompletionAcceptor accept) const;

  virtual void completionForCrossReference(const CompletionContext& context,
                                           const NextFeature& next,
                                           const ReferenceInfo& reference,
                                           CompletionAcceptor accept) const;

  // The cross-reference a feature fills, if its member holds one and the caret
  // has a node: a caret with none answers no scope -- not the global one, which
  // would hand out symbols a local scope excludes.
  // Where the caret opens the rule that writes the member, or the member belongs
  // to a class the caret's node is not, a node of that class is made for the
  // occasion, as a child of the caret's node, and `entered` owns it: the answer's
  // `container` holds as long as `entered` does.
  [[nodiscard]] std::optional<ReferenceInfo> referenceInfoOf(
      const CompletionContext& context, const NextFeature& next,
      AstNodePtr& entered) const;

  // The candidates of a cross-reference: what the scope sees, by default. A
  // visitor returning false stops the walk.
  virtual void visitReferenceCandidates(
      const ReferenceInfo& reference, const CompletionContext& context,
      function_ref<bool(const AstNodeDescription&)> visitor) const;

  [[nodiscard]] virtual CompletionValueItem createReferenceCompletionItem(
      const AstNodeDescription& candidate, const ReferenceInfo& reference,
      const CompletionContext& context) const;

  [[nodiscard]] virtual std::optional<lsp::MarkupContent>
  getReferenceDocumentation(const AstNodeDescription& candidate) const;

  // `next` says which token the keyword is.
  virtual void completionForKeyword(const CompletionContext& context,
                                    const NextFeature& next,
                                    std::string_view keyword,
                                    CompletionAcceptor accept) const;

  [[nodiscard]] virtual lsp::CompletionItemKind getKeywordCompletionItemKind(
      std::string_view keyword) const;

  // Whether a keyword is offered at all; by default those holding a letter.
  [[nodiscard]] virtual bool filterKeyword(const CompletionContext& context,
                                           std::string_view keyword) const;

  // Gives a value its label, and its edit unless it has one. False where no
  // label is known or what is typed does not match it.
  [[nodiscard]] virtual bool fillCompletionItem(
      const CompletionContext& context, CompletionValueItem& value) const;

  // Both ranges where the client chooses between inserting and replacing, the
  // insert range alone otherwise.
  [[nodiscard]] virtual lsp::OneOf<lsp::TextEdit, lsp::InsertReplaceEdit>
  buildCompletionTextEdit(const CompletionContext& context,
                          std::string newText) const;

  virtual void initialize(const lsp::InitializeParams& params);

  std::optional<lsp::CompletionClientCapabilities> clientCapabilities;

  const LspServices& services;

 private:
  // What the server's `onInitialize` callback reaches, gone with the
  // provider: a provider replaced before the handshake is not called.
  std::shared_ptr<DefaultCompletionProvider*> _self =
      std::make_shared<DefaultCompletionProvider*>(this);
};

}  // namespace cursiv
