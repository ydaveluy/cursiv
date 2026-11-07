#pragma once

#include <lsp/types.h>

#include <optional>
#include <span>
#include <string_view>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

// The caret a completion is asked at, as the hooks see it: the token being
// written -- `tokenOffset`..`tokenEndOffset`, both `offset` when none is --
// what may stand there, the caret itself, and how the client asked.

namespace cursiv {

struct CompletionContext {
  CompletionContext(const Document& document,
                    std::span<const NextFeature> features,
                    TextOffset tokenOffset, TextOffset tokenEndOffset,
                    TextOffset nodeOffset, TextOffset offset,
                    lsp::Position position, std::string_view identifier,
                    lsp::Range insertRange, lsp::Range replaceRange,
                    const std::optional<lsp::CompletionContext>& trigger,
                    const CancellationToken& cancelToken)
      : document(document),
        textDocument(document.textDocument()),
        features(features),
        tokenOffset(tokenOffset),
        tokenEndOffset(tokenEndOffset),
        offset(offset),
        position(position),
        identifier(identifier),
        insertRange(insertRange),
        replaceRange(replaceRange),
        trigger(trigger),
        cancelToken(cancelToken),
        _nodeOffset(nodeOffset) {}

  const Document& document;
  const TextDocument& textDocument;
  std::span<const NextFeature> features;
  TextOffset tokenOffset;
  TextOffset tokenEndOffset;
  TextOffset offset;
  lsp::Position position;
  // What an item must match: what is typed of the token.
  std::string_view identifier;
  // What an item replaces, on the caret's line as the protocol requires: up to
  // the caret, or the whole token.
  lsp::Range insertRange;
  lsp::Range replaceRange;
  // How the client asked, as its request says: `triggerKind`, and the
  // `triggerCharacter` typed. Absent when the client does not say.
  const std::optional<lsp::CompletionContext>& trigger;
  const CancellationToken& cancelToken;

  // The node of the leaf at the last token begun before the caret, or at the
  // start of a data rule the caret writes in. Null where no leaf stands there
  // or before it. It decodes the tree, so it is found only when asked for.
  [[nodiscard]] AstNode* node() const;

 private:
  TextOffset _nodeOffset;
  mutable std::optional<AstNode*> _node;
};

// What a hook offers: an item whose label a node or a description may give.
struct CompletionValueItem : lsp::CompletionItem {
  const AstNode* node = nullptr;
  const AstNodeDescription* nodeDescription = nullptr;
  // What is typed is already known to match its label.
  bool matched = false;
};

// Takes one value. False once the answer is full: nothing more is worth
// offering.
using CompletionAcceptor =
    function_ref<bool(const CompletionContext&, CompletionValueItem&&)>;

// The tokens around a caret. The next is the one holding the caret, empty at
// the caret where none does; the previous is the last one before it.
struct CompletionBacktrackingInformation {
  std::optional<TextOffset> previousTokenStart;
  std::optional<TextOffset> previousTokenEnd;
  TextOffset nextTokenStart = 0;
  TextOffset nextTokenEnd = 0;
};

}  // namespace cursiv
