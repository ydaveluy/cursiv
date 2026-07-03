#include <pegium/lsp/workspace/DefaultTextDocuments.hpp>

#include <cassert>
#include <utility>
#include <vector>

#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/core/utils/UriUtils.hpp>

namespace pegium {

using workspace::TextDocument;
using workspace::TextDocumentChangeEvent;
using workspace::TextDocumentContentChangeEvent;
using workspace::TextDocumentSaveReason;
using workspace::TextDocumentWillSaveEvent;
using workspace::TextEdit;

namespace {

std::vector<TextDocumentContentChangeEvent> to_text_document_changes(
    const std::vector<::lsp::TextDocumentContentChangeEvent> &changes) {
  std::vector<TextDocumentContentChangeEvent> textChanges;
  textChanges.reserve(changes.size());

  for (const auto &change : changes) {
    if (const auto *full =
            std::get_if<::lsp::TextDocumentContentChangeEvent_Text>(&change)) {
      textChanges.push_back({.text = full->text});
      continue;
    }

    const auto *range =
        std::get_if<::lsp::TextDocumentContentChangeEvent_Range_Text>(&change);
    assert(range != nullptr);

    textChanges.push_back(
        {.range = text::Range(
             text::Position{range->range.start.line, range->range.start.character},
             text::Position{range->range.end.line, range->range.end.character}),
         .rangeLength = range->rangeLength.has_value()
                            ? std::optional<TextOffset>(*range->rangeLength)
                            : std::nullopt,
         .text = range->text});
  }

  return textChanges;
}

TextDocumentSaveReason
to_text_document_save_reason(const ::lsp::TextDocumentSaveReasonEnum &reason) {
  using enum ::lsp::TextDocumentSaveReason;
  switch (static_cast<::lsp::TextDocumentSaveReason>(reason)) {
  case AfterDelay:
    return TextDocumentSaveReason::AfterDelay;
  case FocusOut:
    return TextDocumentSaveReason::FocusOut;
  case Manual:
  default:
    return TextDocumentSaveReason::Manual;
  }
}

} // namespace

std::shared_ptr<TextDocument>
DefaultTextDocuments::getNormalized(std::string_view normalizedUri) const {
  if (normalizedUri.empty()) {
    return nullptr;
  }

  std::scoped_lock lock(_mutex);
  const auto it = _documents.find(normalizedUri);
  return it == _documents.end() ? nullptr : it->second;
}

bool DefaultTextDocuments::set(std::shared_ptr<TextDocument> document) {
  assert(document != nullptr);
  assert(!document->uri().empty());
  assert(utils::normalize_uri(document->uri()) == document->uri());

  bool inserted = false;
  {
    std::scoped_lock lock(_mutex);
    inserted = _documents.insert_or_assign(document->uri(), document).second;
  }
  emitDidOpen(document);
  emitDidChangeContent(document);
  return inserted;
}

void DefaultTextDocuments::remove(std::string_view uri) {
  const auto normalizedUri = utils::normalize_uri(uri);
  if (normalizedUri.empty()) {
    return;
  }

  std::shared_ptr<TextDocument> removed;
  {
    std::scoped_lock lock(_mutex);
    const auto it = _documents.find(normalizedUri);
    if (it == _documents.end()) {
      return;
    }
    removed = it->second;
    _documents.erase(it);
  }
  emitDidClose(removed);
}

std::vector<std::shared_ptr<TextDocument>> DefaultTextDocuments::all() const {
  std::scoped_lock lock(_mutex);
  std::vector<std::shared_ptr<TextDocument>> documents;
  documents.reserve(_documents.size());
  for (const auto &[uri, document] : _documents) {
    (void)uri;
    documents.push_back(document);
  }
  return documents;
}

std::vector<std::string> DefaultTextDocuments::keys() const {
  std::scoped_lock lock(_mutex);
  std::vector<std::string> uris;
  uris.reserve(_documents.size());
  for (const auto &[uri, document] : _documents) {
    (void)document;
    uris.push_back(uri);
  }
  return uris;
}

utils::ScopedDisposable DefaultTextDocuments::listen(
    ::lsp::MessageHandler &messageHandler,
    const std::function<void()> &ensureInitialized) {
  messageHandler.add<::lsp::notifications::TextDocument_DidOpen>(
      [this](::lsp::DidOpenTextDocumentParams &&params) {
        auto normalizedUri =
            utils::normalize_uri(params.textDocument.uri.toString());
        if (normalizedUri.empty()) {
          return;
        }

        auto document = std::make_shared<TextDocument>(TextDocument::create(
            std::move(normalizedUri), std::move(params.textDocument.languageId),
            params.textDocument.version, std::move(params.textDocument.text)));
        (void)set(std::move(document));
      });

  messageHandler.add<::lsp::notifications::TextDocument_DidChange>(
      [this](const ::lsp::DidChangeTextDocumentParams &params) {
        if (params.contentChanges.empty()) {
          return;
        }

        const auto normalizedUri =
            utils::normalize_uri(params.textDocument.uri.toString());
        if (normalizedUri.empty()) {
          return;
        }
        const auto changes = to_text_document_changes(params.contentChanges);

        std::shared_ptr<TextDocument> document;
        {
          std::scoped_lock lock(_mutex);
          const auto it = _documents.find(normalizedUri);
          if (it == _documents.end()) {
            return;
          }
          document = std::make_shared<TextDocument>(it->second->clone());
          (void)TextDocument::update(*document, changes,
                                     params.textDocument.version);
          it->second = document;
        }
        emitDidChangeContent(document);
      });

  messageHandler.add<::lsp::notifications::TextDocument_DidSave>(
      [this](const ::lsp::DidSaveTextDocumentParams &params) {
        if (auto document = get(params.textDocument.uri.toString());
            document != nullptr) {
          emitDidSave(document);
        }
      });

  messageHandler.add<::lsp::notifications::TextDocument_DidClose>(
      [this](const ::lsp::DidCloseTextDocumentParams &params) {
        remove(params.textDocument.uri.toString());
      });

  messageHandler.add<::lsp::notifications::TextDocument_WillSave>(
      [this, ensureInitialized](const ::lsp::WillSaveTextDocumentParams &params) {
        if (ensureInitialized) {
          ensureInitialized();
        }
        if (const auto document = get(params.textDocument.uri.toString());
            document != nullptr) {
          emitWillSave(document, to_text_document_save_reason(params.reason));
        }
      });

  messageHandler.add<::lsp::requests::TextDocument_WillSaveWaitUntil>(
      [this, ensureInitialized](const ::lsp::WillSaveTextDocumentParams &params) {
        if (ensureInitialized) {
          ensureInitialized();
        }

        const auto document = get(params.textDocument.uri.toString());
        if (document == nullptr) {
          return ::lsp::TextDocument_WillSaveWaitUntilResult{};
        }

        const auto edits = emitWillSaveWaitUntil(
            document, to_text_document_save_reason(params.reason));
        if (edits.empty()) {
          return ::lsp::TextDocument_WillSaveWaitUntilResult{};
        }

        ::lsp::Array<::lsp::TextEdit> lspEdits;
        lspEdits.reserve(edits.size());
        for (const auto &edit : edits) {
          ::lsp::TextEdit lspEdit{};
          lspEdit.range.start = edit.range.start;
          lspEdit.range.end = edit.range.end;
          lspEdit.newText = edit.newText;
          lspEdits.push_back(std::move(lspEdit));
        }
        return ::lsp::TextDocument_WillSaveWaitUntilResult{std::move(lspEdits)};
      });

  return utils::ScopedDisposable([&messageHandler]() {
    messageHandler.remove(::lsp::notifications::TextDocument_DidOpen::Method);
    messageHandler.remove(::lsp::notifications::TextDocument_DidChange::Method);
    messageHandler.remove(::lsp::notifications::TextDocument_DidSave::Method);
    messageHandler.remove(::lsp::notifications::TextDocument_DidClose::Method);
    messageHandler.remove(::lsp::notifications::TextDocument_WillSave::Method);
    messageHandler.remove(
        ::lsp::requests::TextDocument_WillSaveWaitUntil::Method);
  });
}

utils::ScopedDisposable DefaultTextDocuments::onDidOpen(
    std::function<void(const TextDocumentChangeEvent &)> listener) {
  return _onDidOpen.on(std::move(listener));
}

utils::ScopedDisposable DefaultTextDocuments::onDidChangeContent(
    std::function<void(const TextDocumentChangeEvent &)> listener) {
  return _onDidChangeContent.on(std::move(listener));
}

utils::ScopedDisposable DefaultTextDocuments::onDidSave(
    std::function<void(const TextDocumentChangeEvent &)> listener) {
  return _onDidSave.on(std::move(listener));
}

utils::ScopedDisposable DefaultTextDocuments::onDidClose(
    std::function<void(const TextDocumentChangeEvent &)> listener) {
  return _onDidClose.on(std::move(listener));
}

utils::ScopedDisposable DefaultTextDocuments::onWillSave(
    std::function<void(const TextDocumentWillSaveEvent &)> listener) {
  return _onWillSave.on(std::move(listener));
}

utils::ScopedDisposable DefaultTextDocuments::onWillSaveWaitUntil(
    std::function<std::vector<TextEdit>(const TextDocumentWillSaveEvent &)>
        listener) {
  const auto state = _onWillSaveWaitUntil;
  std::size_t id = 0;
  {
    std::scoped_lock lock(state->mutex);
    id = state->nextId++;
    state->activeId = id;
    state->listener = std::move(listener);
  }

  return utils::ScopedDisposable([state, id]() {
    std::scoped_lock lock(state->mutex);
    if (state->activeId == id) {
      state->listener = {};
    }
  });
}

void DefaultTextDocuments::emitDidOpen(
    const std::shared_ptr<TextDocument> &document) const {
  assert(document != nullptr);
  _onDidOpen.emit({.document = document});
}

void DefaultTextDocuments::emitDidChangeContent(
    const std::shared_ptr<TextDocument> &document) const {
  assert(document != nullptr);
  _onDidChangeContent.emit({.document = document});
}

void DefaultTextDocuments::emitDidSave(
    const std::shared_ptr<TextDocument> &document) const {
  assert(document != nullptr);
  _onDidSave.emit({.document = document});
}

void DefaultTextDocuments::emitDidClose(
    const std::shared_ptr<TextDocument> &document) const {
  assert(document != nullptr);
  _onDidClose.emit({.document = document});
}

void DefaultTextDocuments::emitWillSave(
    const std::shared_ptr<TextDocument> &document,
    TextDocumentSaveReason reason) const {
  assert(document != nullptr);
  _onWillSave.emit({.document = document, .reason = reason});
}

std::vector<TextEdit> DefaultTextDocuments::emitWillSaveWaitUntil(
    const std::shared_ptr<TextDocument> &document,
    TextDocumentSaveReason reason) const {
  assert(document != nullptr);

  std::function<std::vector<TextEdit>(const TextDocumentWillSaveEvent &)>
      listener;
  {
    std::scoped_lock lock(_onWillSaveWaitUntil->mutex);
    listener = _onWillSaveWaitUntil->listener;
  }
  if (!listener) {
    return {};
  }
  return listener({.document = document, .reason = reason});
}

} // namespace pegium
