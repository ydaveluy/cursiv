#pragma once

/// Shared, item-type-templated implementation for the call- and type-hierarchy
/// providers. Their logic is identical apart from the LSP item type and the
/// SymbolKind, so the symbol-id decoding, target resolution, item construction
/// and the prepare/edge flows live here once.

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/references/References.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/core/workspace/Symbol.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/lsp/support/JsonValue.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

namespace pegium::hierarchy_detail {

/// Decodes the stable symbol id carried in a hierarchy item's `data`.
template <typename Item>
[[nodiscard]] std::optional<workspace::SymbolId>
symbol_id_from_item(const Item &item) {
  if (!item.data.has_value()) {
    return std::nullopt;
  }
  const auto data = from_lsp_any(*item.data);
  if (!data.isInteger()) {
    return std::nullopt;
  }
  const auto value = data.integer();
  if (value < 0 ||
      value > static_cast<std::int64_t>(
                  std::numeric_limits<workspace::SymbolId>::max())) {
    return std::nullopt;
  }
  return static_cast<workspace::SymbolId>(value);
}

/// Resolves a hierarchy item back to its AST node, by stable symbol id when
/// available and otherwise by the declaration at the item's selection range.
template <typename Item>
[[nodiscard]] const AstNode *
resolve_target(const workspace::Document &document, const Item &item,
               const references::References &references) {
  if (const auto symbolId = symbol_id_from_item(item); symbolId.has_value()) {
    if (const auto *node = document.findAstNode(*symbolId); node != nullptr) {
      return node;
    }
  }
  const auto &textDocument = document.textDocument();
  const auto declarations = provider_detail::find_declarations_at_offset(
      document, textDocument.offsetAt(item.selectionRange.start), references);
  return declarations.empty() ? nullptr : declarations.front();
}

/// Builds the hierarchy item for `node`, or `nullopt` when it has no name.
/// `customize` receives the built item for final, provider-specific tweaks.
template <typename Item, typename Customize>
[[nodiscard]] std::optional<Item>
create_item(const Services &services, const AstNode &node,
            ::lsp::SymbolKind kind, Customize &&customize) {
  const auto *nameProvider = services.references.nameProvider.get();
  const auto &document = getDocument(node);

  auto namedNode = references::named_node_info(node, *nameProvider);
  if (!namedNode.has_value()) {
    return std::nullopt;
  }

  Item item{};
  item.kind = kind;
  item.name = std::move(namedNode->name);
  item.uri = ::lsp::Uri::parse(document.uri);
  const auto &textDocument = document.textDocument();
  item.range = provider_detail::offset_to_range(
      textDocument, namedNode->nodeCst.getBegin(), namedNode->nodeCst.getEnd());
  item.selectionRange = provider_detail::offset_to_range(
      textDocument, namedNode->selectionNode.getBegin(),
      namedNode->selectionNode.getEnd());
  item.data = to_lsp_any(pegium::JsonValue(
      static_cast<std::int64_t>(document.makeSymbolId(node))));
  std::forward<Customize>(customize)(node, item);
  return item;
}

/// prepare flow: build the item for the declaration at `params.position`.
template <typename Item, typename PrepareParams, typename CreateItem>
[[nodiscard]] std::vector<Item>
prepare(const Services &services, const workspace::Document &document,
        const PrepareParams &params, CreateItem &&createItem,
        const utils::CancellationToken &cancelToken) {
  utils::throw_if_cancelled(cancelToken);
  const auto &references = *services.references.references;
  const auto &textDocument = document.textDocument();
  const auto declarations = provider_detail::find_declarations_at_offset(
      document, textDocument.offsetAt(params.position), references);
  // One item per resolved declaration: a single position can resolve to several
  // declarations (e.g. a multi-reference), so build an item for each rather than
  // only the first.
  std::vector<Item> items;
  for (const auto *declaration : declarations) {
    if (auto item = createItem(*declaration); item.has_value()) {
      items.push_back(std::move(*item));
    }
  }
  return items;
}

/// edge flow: resolve `params.item` to its node and collect its edges.
template <typename Params, typename Edges>
[[nodiscard]] auto collect_edges(const Services &services, const Params &params,
                                 Edges &&edges,
                                 const utils::CancellationToken &cancelToken)
    -> decltype(edges(std::declval<const AstNode &>(), cancelToken)) {
  utils::throw_if_cancelled(cancelToken);
  auto &documents = *services.shared.workspace.documents;
  const auto &references = *services.references.references;
  // The item's document may not be loaded (e.g. a dependency not opened in the
  // editor), so materialize it on demand rather than assuming it is present.
  std::shared_ptr<workspace::Document> document;
  try {
    document =
        documents.getOrCreateDocument(params.item.uri.toString(), cancelToken);
  } catch (const utils::OperationCancelled &) {
    throw;
  } catch (const std::exception &) {
    return {};
  }
  if (document == nullptr) {
    return {};
  }
  if (const auto *node = resolve_target(*document, params.item, references);
      node != nullptr) {
    return edges(*node, cancelToken);
  }
  return {};
}

} // namespace pegium::hierarchy_detail
