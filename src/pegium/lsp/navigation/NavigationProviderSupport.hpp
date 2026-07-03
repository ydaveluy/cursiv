#pragma once

/// Shared implementation for the navigation providers that resolve the
/// declarations at a position and then collect location links for each one. The
/// go-to-implementation and go-to-type-definition flows differ only in the
/// per-declaration collector, so the resolve-then-collect loop lives here once.

#include <optional>
#include <utility>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/references/References.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

namespace pegium::navigation_detail {

/// Resolves the declarations at `position` and collects their location links.
///
/// `collect` returns the links for one declaration. A position can resolve to
/// several declarations (e.g. a multi-reference), so the links of every
/// resolved declaration are flattened together; an empty result yields
/// `nullopt`.
template <typename Collect>
[[nodiscard]] std::optional<std::vector<::lsp::LocationLink>>
collect_links_per_declaration(const Services &services,
                              const workspace::Document &document,
                              const ::lsp::Position &position, Collect &&collect,
                              const utils::CancellationToken &cancelToken) {
  utils::throw_if_cancelled(cancelToken);
  const auto &references = *services.references.references;
  const auto declarations = provider_detail::find_declarations_at_offset(
      document, document.textDocument().offsetAt(position), references);
  std::vector<::lsp::LocationLink> links;
  for (const auto *declaration : declarations) {
    utils::throw_if_cancelled(cancelToken);
    if (auto sublinks = collect(*declaration, cancelToken);
        sublinks.has_value()) {
      for (auto &link : *sublinks) {
        links.push_back(std::move(link));
      }
    }
  }
  if (links.empty()) {
    return std::nullopt;
  }
  return links;
}

} // namespace pegium::navigation_detail
