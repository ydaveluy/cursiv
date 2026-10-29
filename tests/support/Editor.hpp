// What an editor does to a language: opens a tab and replaces its text, asks
// the list at a marked caret and whether a label is in it, and takes both
// ranges of an edit.
#pragma once

#include <lsp/types.h>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/test/ExpectedCompletion.hpp"
#include "lsp/uri.h"

namespace support {

// A tab opened on `text`, at version 1.
inline void open(cursiv::LanguageServer& server, std::string_view uri,
                 std::string_view languageId, std::string_view text) {
  lsp::DidOpenTextDocumentParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(uri));
  params.textDocument.languageId = std::string(languageId);
  params.textDocument.version = 1;
  params.textDocument.text = std::string(text);
  server.didOpen(params);
}

// The whole text of the tab replaced, at `version`.
inline void change(cursiv::LanguageServer& server, std::string_view uri,
                   int version, std::string_view text) {
  lsp::DidChangeTextDocumentParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(uri));
  params.textDocument.version = version;
  lsp::TextDocumentContentChangeWholeDocument whole;
  whole.text = std::string(text);
  params.contentChanges.emplace_back(std::move(whole));
  server.didChange(params);
}

[[nodiscard]] inline bool offers(const lsp::CompletionList& list,
                                 std::string_view label) {
  return std::ranges::any_of(list.items, [label](const lsp::CompletionItem& i) {
    return i.label == label;
  });
}

// The list at the `<|>` of `text`, handed to `assertion`.
inline void offered(const cursiv::LspServices& language, std::string text,
                    std::function<void(const lsp::CompletionList&)> assertion) {
  (void)cursiv::test::expectCompletion(language,
                                       cursiv::test::ExpectedCompletionCallback{
                                           .text = std::move(text),
                                           .assertion = std::move(assertion),
                                           .disposeAfterCheck = true});
}

// An editor that chooses between inserting and replacing a word, so the
// replace range is sent at all.
inline void take_both_ranges(const cursiv::SharedLspServices& shared) {
  lsp::InitializeParams params;
  params.capabilities.textDocument.emplace()
      .completion.emplace()
      .completionItem.emplace()
      .insertReplaceSupport = true;
  (void)shared.languageServer->initialize(params);
}

}  // namespace support
