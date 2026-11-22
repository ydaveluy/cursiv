#include "cursiv/TestWorkspace.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/TextDocumentParams.hpp"
#include "cursiv/completion/CompletionProvider.hpp"
#include "cursiv/hover/HoverProvider.hpp"
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/navigation/RenameProvider.hpp"
#include "cursiv/ranges/FoldingRangeProvider.hpp"
#include "cursiv/semantic/InlayHintProvider.hpp"
#include "cursiv/semantic/SignatureHelpProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/Documents.hpp"

namespace cursiv::test {
namespace {
const NamedMarker& unique_marker(const MarkedDocument& file,
                                 std::string_view label, bool caretOnly) {
  const NamedMarker* found = nullptr;
  for (const auto& m : file.markers) {
    if (m.label != label || (caretOnly && !m.caret)) continue;
    if (found)
      throw ExpectationError("ambiguous marker '" + std::string(label) +
                             "' in " + file.document->uri);
    found = &m;
  }
  if (!found)
    throw ExpectationError("missing marker '" + std::string(label) + "' in " +
                           file.document->uri);
  return *found;
}

void append_targets(const MarkedDocument& file, std::string_view label,
                    std::vector<lsp::Location>& targets) {
  const auto uri = lsp::Uri::parse(file.document->uri);
  for (const auto& m : file.markers)
    if (!m.caret && m.label == label)
      targets.push_back({uri, toLsp(file.document->textDocument().rangeAt(
                                  m.range.begin, m.range.end))});
}

const LspServices& editor(const Document& doc) {
  const auto* services = dynamic_cast<const LspServices*>(doc.language);
  if (!services) throw ExpectationError("no editor services for " + doc.uri);
  return *services;
}
}  // namespace

TextRange MarkedDocument::range(std::string_view label) const {
  return unique_marker(*this, label, false).range;
}
lsp::Range MarkedDocument::lspRange(std::string_view label) const {
  const auto [begin, end] = range(label);
  return toLsp(document->textDocument().rangeAt(begin, end));
}

lsp::CompletionList MarkedDocument::completion(
    std::string_view label, const CancellationToken& cancel) const {
  const auto& provider = editor(*document).completionProvider;
  if (!provider) throw ExpectationError("missing completion provider");
  lsp::CompletionParams params{
      textDocumentPositionParams(*document, range(label).begin)};
  return provider->getCompletion(*document, params, cancel);
}
std::optional<lsp::Hover> MarkedDocument::hover(
    std::string_view label, const CancellationToken& cancel) const {
  const auto& provider = editor(*document).hoverProvider;
  if (!provider) throw ExpectationError("missing hover provider");
  return provider->getHoverContent(*document, lspRange(label).start, cancel);
}
std::optional<lsp::SignatureHelp> MarkedDocument::signatureHelp(
    std::string_view label, std::optional<lsp::SignatureHelpContext> context,
    const CancellationToken& cancel) const {
  const auto& provider = editor(*document).signatureHelpProvider;
  if (!provider) throw ExpectationError("missing signature help provider");
  lsp::SignatureHelpParams params{
      textDocumentPositionParams(*document, range(label).begin)};
  params.context = std::move(context);
  return provider->provideSignatureHelp(*document, params, cancel);
}
std::vector<lsp::InlayHint> MarkedDocument::inlayHints(
    std::string_view label, const CancellationToken& cancel) const {
  const auto& provider = editor(*document).inlayHintProvider;
  if (!provider) throw ExpectationError("missing inlay hint provider");
  return provider->getInlayHints(*document, lspRange(label), cancel);
}
std::optional<lsp::DocumentSymbol> MarkedDocument::symbol(
    std::string_view label, const CancellationToken& cancel) const {
  const auto& provider = editor(*document).documentSymbolProvider;
  if (!provider) throw ExpectationError("missing symbol provider");
  const auto range = lspRange(label);
  auto symbols = provider->getSymbols(*document, cancel);
  std::vector<lsp::DocumentSymbol*> pending;
  for (auto& root : symbols) pending.push_back(&root);
  while (!pending.empty()) {
    auto* s = pending.back();
    pending.pop_back();
    if (isRangeEqual(s->range, range)) return std::move(*s);
    if (s->children)
      for (auto& child : *s->children) pending.push_back(&child);
  }
  return {};
}

void MarkedDocument::expectHighlights(std::string_view label,
                                      const CancellationToken& cancel) const {
  std::vector<lsp::Location> expected;
  append_targets(*this, label, expected);
  detail::sort_locations(expected);
  detail::check_navigation(editor(*document), *document,
                           unique_marker(*this, label, true).range.begin,
                           detail::NavigationQuery::Highlights, expected,
                           cancel);
}

void MarkedDocument::expectFolding(std::string_view label,
                                   std::optional<lsp::FoldingRangeKind> kind,
                                   const CancellationToken& cancel) const {
  const auto& provider = editor(*document).foldingRangeProvider;
  if (!provider) throw ExpectationError("missing folding provider");
  const auto r = lspRange(label);
  const auto folds = provider->getFoldingRanges(*document, cancel);
  const bool found = std::ranges::any_of(folds, [&](const auto& f) {
    return f.startLine == r.start.line && f.endLine == r.end.line &&
           (!kind || (f.kind && *f.kind == *kind));
  });
  expectedFunction(found ? "present" : "missing", "present", label);
}

DecodedSemanticTokensWithRanges MarkedDocument::semanticTokens(
    std::string_view label, const CancellationToken& cancel) const {
  std::vector<std::pair<TextOffset, TextOffset>> ranges;
  for (const auto& m : markers)
    if (!m.caret && m.label == label)
      ranges.emplace_back(m.range.begin, m.range.end);
  if (ranges.empty())
    throw ExpectationError("missing token range: " + std::string(label));
  return decodeTokens(editor(*document), *document, std::move(ranges), cancel);
}

std::vector<std::shared_ptr<Document>> TestWorkspace::parse(
    std::span<const TestFile> files, const BuildOptions& options,
    const MarkerSyntax& syntax, const CancellationToken& cancel) {
  decltype(_documents) staged;
  std::vector<std::shared_ptr<Document>> batch;
  batch.reserve(files.size());
  for (const auto& input : files) {
    throwIfCancelled(cancel);
    auto uri = normalizeUri(input.uri);
    if (uri.empty()) throw ExpectationError("a fixture file needs a URI");
    auto [at, inserted] = staged.try_emplace(uri);
    if (!inserted || _documents.contains(uri))
      throw ExpectationError("duplicate fixture URI: " + uri);
    auto text = extractMarkers(input.text, at->second.markers, syntax);
    auto doc = _shared.documentFactory->create(TextDocument::create(
        std::move(uri), std::string(input.languageId), 0, std::move(text)));
    doc->isOpen = true;
    at->second.document = doc;
    batch.push_back(std::move(doc));
  }
  detail::build_documents(_shared, batch, options, cancel);
  _documents.merge(staged);
  return batch;
}

void TestWorkspace::attach(MarkedDocument file) {
  if (!file.document ||
      _shared.documents->getDocument(file.document->id) != file.document)
    throw ExpectationError("attach requires a registered document");
  const auto& uri = file.document->uri;
  auto size = file.document->textDocument().getText().size();
  for (const auto& m : file.markers)
    if (m.range.begin > m.range.end || m.range.end > size ||
        (m.caret && m.range.begin != m.range.end))
      throw ExpectationError("invalid marker in " + uri);
  const auto [it, added] = _documents.try_emplace(uri, std::move(file));
  if (!added) throw ExpectationError("duplicate fixture URI: " + uri);
  it->second.document->isOpen = true;
}

const MarkedDocument& TestWorkspace::at(std::string_view uri) const {
  const auto it = _documents.find(normalizeUri(uri));
  if (it == _documents.end() ||
      _shared.documents->getDocument(it->second.document->id) !=
          it->second.document)
    throw ExpectationError("unknown fixture document: " + std::string(uri));
  return it->second;
}

void TestWorkspace::erase(std::string_view uri) {
  auto it = _documents.find(normalizeUri(uri));
  if (it == _documents.end()) return;
  clearDocuments(_shared, {&it->second.document, 1});
  _documents.erase(it);
}
void TestWorkspace::clear() {
  if (_documents.empty()) return;
  std::vector<std::shared_ptr<Document>> held;
  for (const auto& [uri, file] : _documents) held.push_back(file.document);
  clearDocuments(_shared, held);
  _documents.clear();
}

void TestWorkspace::expectTargets(std::string_view uri, std::string_view label,
                                  detail::NavigationQuery query, bool fullRange,
                                  const CancellationToken& cancel) const {
  const auto& source = at(uri);
  const auto caret = unique_marker(source, label, true).range.begin;
  std::vector<lsp::Location> expected;
  for (const auto& [targetUri, file] : _documents) {
    if (_shared.documents->getDocument(file.document->id) != file.document)
      throw ExpectationError("stale fixture document: " + targetUri);
    append_targets(file, label, expected);
  }
  detail::sort_locations(expected);
  detail::check_navigation(editor(*source.document), *source.document, caret,
                           query, expected, cancel, false, fullRange);
}

void TestWorkspace::expectDefinition(std::string_view uri,
                                     std::string_view label, bool fullRange,
                                     const CancellationToken& cancel) const {
  expectTargets(uri, label, detail::NavigationQuery::Definition, fullRange,
                cancel);
}
void TestWorkspace::expectDeclaration(std::string_view uri,
                                      std::string_view label, bool fullRange,
                                      const CancellationToken& cancel) const {
  expectTargets(uri, label, detail::NavigationQuery::Declaration, fullRange,
                cancel);
}
void TestWorkspace::expectImplementation(
    std::string_view uri, std::string_view label, bool fullRange,
    const CancellationToken& cancel) const {
  expectTargets(uri, label, detail::NavigationQuery::Implementation, fullRange,
                cancel);
}
void TestWorkspace::expectTypeDefinition(
    std::string_view uri, std::string_view label, bool fullRange,
    const CancellationToken& cancel) const {
  expectTargets(uri, label, detail::NavigationQuery::TypeDefinition, fullRange,
                cancel);
}
void TestWorkspace::expectReferences(std::string_view uri,
                                     std::string_view label,
                                     bool includeDeclaration,
                                     const CancellationToken& cancel) const {
  expectTargets(uri, label,
                includeDeclaration
                    ? detail::NavigationQuery::ReferencesWithDeclaration
                    : detail::NavigationQuery::References,
                false, cancel);
}

void TestWorkspace::rename(std::string_view uri, std::string_view label,
                           std::string_view newName,
                           const CancellationToken& cancel) {
  const auto& source = at(uri);
  const auto& provider = editor(*source.document).renameProvider;
  if (!provider) throw ExpectationError("missing rename provider");
  const auto pos =
      source.document->textDocument().positionAt(source.range(label).begin);
  auto edit = provider->rename(*source.document, toLsp(pos), newName, cancel);
  if (!edit)
    throw ExpectationError("rename refused in " + source.document->uri);

  struct Change {
    TextDocument text;
    std::vector<NamedMarker> markers;
  };
  std::map<std::string, Change, std::less<>> staged;
  const auto prepare = [&](const lsp::Uri& target, const auto& edits,
                           std::optional<int> version) {
    const auto& file = at(target.toString());
    auto [it, first] = staged.try_emplace(file.document->uri);
    auto& change = it->second;
    const TextDocument& before =
        first ? file.document->textDocument() : change.text;
    if (version && *version != before.version())
      throw ExpectationError("stale rename edit for " + file.document->uri);
    if (before.version() == std::numeric_limits<std::int64_t>::max())
      throw ExpectationError("document version exhausted in " +
                             file.document->uri);
    if (first) change.markers = file.markers;
    if (edits.empty())
      throw ExpectationError("rename returned an empty edit for " +
                             file.document->uri);
    std::vector<TextEdit> converted;
    struct Span {
      TextOffset begin, end;
      std::int64_t length, delta;
    };
    std::vector<Span> spans;
    for (const auto& item : edits) {
      const auto* plain = detail::plain_edit(item);
      if (!plain)
        throw ExpectationError("expected a plain text edit in rename");
      const auto range = fromLsp(plain->range);
      const auto begin = before.offsetAt(range.start),
                 end = before.offsetAt(range.end);
      if (begin > end || before.positionAt(begin) != range.start ||
          before.positionAt(end) != range.end)
        throw ExpectationError("invalid rename range in " + file.document->uri);
      converted.push_back({range, plain->newText});
      spans.push_back(
          {begin, end, static_cast<std::int64_t>(plain->newText.size()), 0});
    }
    auto after = TextDocument::create(
        file.document->uri, std::string(before.languageId()),
        before.version() + 1, TextDocument::applyEdits(before, converted));
    std::ranges::stable_sort(spans, {}, &Span::begin);
    std::int64_t delta = 0;
    for (auto& span : spans) {
      span.delta = delta;
      delta += span.length - (span.end - span.begin);
    }
    const auto shift = [&](TextOffset offset) -> TextOffset {
      auto it = std::ranges::lower_bound(spans, offset, {}, &Span::begin);
      if (it == spans.begin()) return offset;
      const auto& prev = *std::prev(it);
      const auto distance = static_cast<std::int64_t>(offset) - prev.begin;
      const auto inside = offset < prev.end ? std::min(distance, prev.length)
                                            : prev.length + offset - prev.end;
      return static_cast<TextOffset>(prev.begin + prev.delta + inside);
    };
    for (auto& m : change.markers)
      m.range = {shift(m.range.begin), shift(m.range.end)};
    change.text = std::move(after);
  };
  if (edit->documentChanges) {
    for (const auto& entry : *edit->documentChanges) {
      const auto* text = std::get_if<lsp::TextDocumentEdit>(&entry);
      if (!text) throw ExpectationError("rename returned a file operation");
      const auto& version = text->textDocument.version;
      prepare(text->textDocument.uri, text->edits,
              version.isNull() ? std::nullopt : std::optional<int>(*version));
    }
  } else if (edit->changes) {
    for (const auto& [target, edits] : *edit->changes)
      prepare(target, edits, std::nullopt);
  }
  if (staged.empty())
    throw ExpectationError("rename returned no document edits");
  throwIfCancelled(cancel);
  std::vector<std::string_view> changed;
  changed.reserve(staged.size());
  for (auto& [key, change] : staged) {
    auto& file = _documents.at(key);
    _shared.documentFactory->attachTextDocument(*file.document,
                                                std::move(change.text));
    file.markers = std::move(change.markers);
    changed.push_back(key);
  }
  _shared.documentBuilder->update(changed, {}, cancel);
}

}  // namespace cursiv::test
