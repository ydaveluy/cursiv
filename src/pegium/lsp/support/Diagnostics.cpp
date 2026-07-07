#include <pegium/lsp/support/Diagnostics.hpp>

#include <limits>
#include <string>
#include <utility>

#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/lsp/support/JsonValue.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/core/workspace/TextDocumentProvider.hpp>

namespace pegium {

namespace {

constexpr int clamp_to_lsp_integer(std::int64_t value) noexcept {
  if (value < std::numeric_limits<int>::min()) {
    return std::numeric_limits<int>::min();
  }
  if (value > std::numeric_limits<int>::max()) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(value);
}

::lsp::DiagnosticTag to_lsp_diagnostic_tag(pegium::DiagnosticTag tag) {
  using enum pegium::DiagnosticTag;
  switch (tag) {
  case Unnecessary:
    return ::lsp::DiagnosticTag::Unnecessary;
  case Deprecated:
    return ::lsp::DiagnosticTag::Deprecated;
  }
  return ::lsp::DiagnosticTag::Unnecessary;
}

} // namespace

::lsp::Diagnostic
to_lsp_diagnostic(const workspace::TextDocument &positionDocument,
                  const Diagnostic &diagnostic,
                  const workspace::TextDocumentProvider *crossFileProvider) {
  const auto &documentUri = positionDocument.uri();
  ::lsp::Diagnostic lspDiagnostic{};
  lspDiagnostic.severity = to_lsp_diagnostic_severity(diagnostic.severity);
  lspDiagnostic.source = diagnostic.source;
  lspDiagnostic.message = diagnostic.message;
  if (diagnostic.code.has_value()) {
    if (const auto *code = std::get_if<std::int64_t>(&*diagnostic.code)) {
      lspDiagnostic.code =
          ::lsp::OneOf<int, ::lsp::String>(clamp_to_lsp_integer(*code));
    } else if (const auto *stringCode =
                   std::get_if<std::string>(&*diagnostic.code)) {
      lspDiagnostic.code = ::lsp::OneOf<int, ::lsp::String>(*stringCode);
    }
  }
  if (diagnostic.codeDescription.has_value()) {
    const auto uri = ::lsp::Uri::parse(*diagnostic.codeDescription);
    if (uri.isValid()) {
      ::lsp::CodeDescription codeDescription{};
      codeDescription.href = uri;
      lspDiagnostic.codeDescription = std::move(codeDescription);
    }
  }
  if (!diagnostic.tags.empty()) {
    ::lsp::Array<::lsp::DiagnosticTagEnum> tags;
    tags.reserve(diagnostic.tags.size());
    for (const auto tag : diagnostic.tags) {
      tags.push_back(to_lsp_diagnostic_tag(tag));
    }
    lspDiagnostic.tags = std::move(tags);
  }
  if (!diagnostic.relatedInformation.empty()) {
    ::lsp::Array<::lsp::DiagnosticRelatedInformation> relatedInformation;
    relatedInformation.reserve(diagnostic.relatedInformation.size());
    for (const auto &entry : diagnostic.relatedInformation) {
      ::lsp::DiagnosticRelatedInformation related{};
      const auto entryUri = entry.uri.empty() ? ::lsp::Uri::parse(documentUri)
                                              : ::lsp::Uri::parse(entry.uri);
      related.location.uri =
          entryUri.isValid() ? entryUri : ::lsp::Uri::parse(documentUri);

      // Map the entry's offsets against the document it actually points at: a
      // cross-file entry resolves through `crossFileProvider`. Reusing
      // `positionDocument`'s offsets for an unresolved cross-file entry would
      // silently produce a plausible-looking but wrong line/column, so an
      // entry naming a different file that cannot be resolved reports {0,0}
      // instead.
      const workspace::TextDocument *rangeDocument = &positionDocument;
      std::shared_ptr<workspace::TextDocument> crossFileHolder;
      if (!entry.uri.empty() && entry.uri != documentUri) {
        auto resolved = crossFileProvider != nullptr
                            ? crossFileProvider->get(entry.uri)
                            : nullptr;
        if (resolved == nullptr) {
          rangeDocument = nullptr;
        } else if (resolved->uri() != documentUri) {
          crossFileHolder = std::move(resolved);
          rangeDocument = crossFileHolder.get();
        }
      }
      if (rangeDocument == nullptr) {
        related.location.range.start = {};
        related.location.range.end = {};
      } else {
        related.location.range.start = rangeDocument->positionAt(entry.begin);
        related.location.range.end = rangeDocument->positionAt(
            entry.end >= entry.begin ? entry.end : entry.begin);
      }
      related.message = entry.message;
      relatedInformation.push_back(std::move(related));
    }
    lspDiagnostic.relatedInformation = std::move(relatedInformation);
  }
  if (diagnostic.data.has_value()) {
    lspDiagnostic.data = to_lsp_any(*diagnostic.data);
  }

  lspDiagnostic.range.start = positionDocument.positionAt(diagnostic.begin);
  lspDiagnostic.range.end = positionDocument.positionAt(
      diagnostic.end >= diagnostic.begin ? diagnostic.end : diagnostic.begin);
  return lspDiagnostic;
}

void publish_diagnostics(
    ::lsp::MessageHandler *messageHandler,
    const workspace::DocumentDiagnosticsSnapshot &snapshot,
    const workspace::TextDocumentProvider *crossFileProvider) {
  if (messageHandler == nullptr) {
    return;
  }

  ::lsp::notifications::TextDocument_PublishDiagnostics::Params params{};
  params.uri = ::lsp::Uri::parse(snapshot.uri);
  params.diagnostics.reserve(snapshot.diagnostics.size());
  if (snapshot.version.has_value()) {
    params.version = clamp_to_lsp_integer(*snapshot.version);
  }
  auto positionDocument =
      workspace::TextDocument::create(snapshot.uri, "", 0, snapshot.text);

  for (const auto &diagnostic : snapshot.diagnostics) {
    params.diagnostics.push_back(
        to_lsp_diagnostic(positionDocument, diagnostic, crossFileProvider));
  }

  messageHandler
      ->sendNotification<::lsp::notifications::TextDocument_PublishDiagnostics>(
          std::move(params));
}

} // namespace pegium
