#pragma once

#include <lsp/types.h>

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/DecodedSemanticTokens.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv::test {

namespace detail {
enum class NavigationQuery;
}

struct TestFile {
  std::string_view uri, text, languageId{};
};

struct MarkedDocument {
  std::shared_ptr<Document> document;
  std::vector<NamedMarker> markers;
  // Exactly one marker must have the label. A caret gives an empty range.
  [[nodiscard]] TextRange range(std::string_view label) const;
  [[nodiscard]] lsp::Range lspRange(std::string_view label) const;

  [[nodiscard]] lsp::CompletionList completion(
      std::string_view label, const CancellationToken& cancel = {}) const;
  [[nodiscard]] std::optional<lsp::Hover> hover(
      std::string_view label, const CancellationToken& cancel = {}) const;
  [[nodiscard]] std::optional<lsp::SignatureHelp> signatureHelp(
      std::string_view label,
      std::optional<lsp::SignatureHelpContext> context = {},
      const CancellationToken& cancel = {}) const;
  [[nodiscard]] std::vector<lsp::InlayHint> inlayHints(
      std::string_view label, const CancellationToken& cancel = {}) const;
  [[nodiscard]] std::optional<lsp::DocumentSymbol> symbol(
      std::string_view label, const CancellationToken& cancel = {}) const;
  void expectHighlights(std::string_view label,
                        const CancellationToken& cancel = {}) const;
  void expectFolding(std::string_view label,
                     std::optional<lsp::FoldingRangeKind> kind = {},
                     const CancellationToken& cancel = {}) const;
  [[nodiscard]] DecodedSemanticTokensWithRanges semanticTokens(
      std::string_view label, const CancellationToken& cancel = {}) const;

  template <class T>
  [[nodiscard]] auto nodes() const {
    return document->parseResult.nodes() |
           std::views::transform(
               [](const AstNode* n) { return ast_cast<T>(n); }) |
           std::views::filter([](const T* n) { return n != nullptr; });
  }
  template <class T>
  [[nodiscard]] const T* findNode(std::optional<std::string_view> name = {},
                                  std::optional<TextOffset> offset = {}) const {
    for (const T* node : nodes<T>()) {
      if (name && document->language->nameProvider->getName(*node) != name)
        continue;
      if (offset) {
        const Cst& cst = document->parseResult.cst();
        const auto id = static_cast<const AstNode&>(*node).cstNodeId;
        if (id >= cst.size() || *offset < cst[id].begin ||
            *offset >= cst[id].end)
          continue;
      }
      return node;
    }
    return nullptr;
  }
};

class TestWorkspace {
 public:
  explicit TestWorkspace(const SharedCoreServices& shared) : _shared(shared) {}
  TestWorkspace(const TestWorkspace&) = delete;
  TestWorkspace& operator=(const TestWorkspace&) = delete;

  // All files join the same build. The result follows the input order.
  std::vector<std::shared_ptr<Document>> parse(
      std::span<const TestFile> files, const BuildOptions& options = {},
      const MarkerSyntax& syntax = {}, const CancellationToken& cancel = {});
  std::vector<std::shared_ptr<Document>> parse(
      std::initializer_list<TestFile> files, const BuildOptions& options = {},
      const MarkerSyntax& syntax = {}, const CancellationToken& cancel = {}) {
    return parse(std::span(files), options, syntax, cancel);
  }

  void attach(MarkedDocument file);
  [[nodiscard]] const MarkedDocument& at(std::string_view uri) const;
  [[nodiscard]] const auto& documents() const { return _documents; }
  void erase(std::string_view uri);
  void clear();
  void expectDefinition(std::string_view uri, std::string_view label,
                        bool fullRange = false,
                        const CancellationToken& cancel = {}) const;
  void expectDeclaration(std::string_view uri, std::string_view label,
                         bool fullRange = false,
                         const CancellationToken& cancel = {}) const;
  void expectImplementation(std::string_view uri, std::string_view label,
                            bool fullRange = false,
                            const CancellationToken& cancel = {}) const;
  void expectTypeDefinition(std::string_view uri, std::string_view label,
                            bool fullRange = false,
                            const CancellationToken& cancel = {}) const;
  void expectReferences(std::string_view uri, std::string_view label,
                        bool includeDeclaration = false,
                        const CancellationToken& cancel = {}) const;
  void rename(std::string_view uri, std::string_view label,
              std::string_view newName, const CancellationToken& cancel = {});

 private:
  void expectTargets(std::string_view uri, std::string_view label,
                     detail::NavigationQuery query, bool fullRange,
                     const CancellationToken& cancel) const;
  const SharedCoreServices& _shared;
  std::map<std::string, MarkedDocument, std::less<>> _documents;
};

}  // namespace cursiv::test
