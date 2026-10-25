#include "cursiv/core/validation/SpellingActions.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "cursiv/core/references/ScopeProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/utils/EditDistance.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {
namespace {

constexpr unsigned kSpelledSites = 64;
constexpr std::size_t kMaxSuggestions = 3;

JsonValue::Array near_names(const Document& document,
                            const CoreServices& language,
                            const ReferenceInfo& site, TextRange range) {
  JsonValue::Array rows;
  const std::string_view text = site.referenceText;
  // Under three bytes every name is a neighbour.
  if (text.size() < 3) return rows;
  const std::size_t limit = text.size() < 4 ? 1 : text.size() < 8 ? 2 : 3;
  struct Near {
    std::string_view name;
    std::size_t distance;
    bool elsewhere;
  };
  std::vector<Near> near;
  (void)language.scopeProvider->visitScopeEntries(
      site, [&](const AstNodeDescription& entry) {
        if (entry.name.size() + limit < text.size() ||
            text.size() + limit < entry.name.size())
          return true;
        // A case that differs is the likeliest slip, ahead of any edit.
        const std::size_t distance =
            folded_equal(entry.name, text)
                ? 0
                : edit_distance(entry.name, text, limit);
        if (distance > limit) return true;
        // An outer scope re-surfaces what an inner one already gave.
        for (const Near& held : near)
          if (held.name == entry.name) return true;
        near.push_back(Near{entry.name, distance,
                            entry.documentId != document.id});
        return true;
      });
  std::ranges::sort(near, [](const Near& a, const Near& b) {
    return std::tie(a.distance, a.elsewhere, a.name) <
           std::tie(b.distance, b.elsewhere, b.name);
  });
  if (near.size() > kMaxSuggestions) near.resize(kMaxSuggestions);
  for (const Near& n : near)
    rows.push_back(make_action_row(kReplaceEdit,
                                   std::format("Replace with '{}'", n.name),
                                   range, std::string(n.name),
                                   document.buildRevision));
  return rows;
}

}  // namespace

std::vector<JsonValue::Array> spelling_actions(
    const Document& document, std::span<const Diagnostic* const> diagnostics) {
  std::vector<JsonValue::Array> rows(diagnostics.size());
  const DocumentReferences* const references =
      document.parseResult.references.get();
  const auto asked = [](const Diagnostic& d) {
    return d.code.has_value() && *d.code == kLinkingError &&
           d.range.begin != d.range.end;
  };
  if (references == nullptr || document.language == nullptr ||
      std::ranges::none_of(diagnostics, [&](const Diagnostic* d) {
        return asked(*d);
      }))
    return rows;
  // The sites counted in the order the validator lists them, so the first
  // ones are the same whichever diagnostics ask.
  unsigned rank = 0;
  references->visitUnresolved(
      document.parseResult.cst(),
      [&](RefState why, TextOffset begin, TextOffset end,
          const ReferenceInfo& site) {
        if (why != RefState::ErrorNotFound || begin == end) return;
        if (rank++ >= kSpelledSites) return;
        for (std::size_t i = 0; i < diagnostics.size(); ++i) {
          const Diagnostic& d = *diagnostics[i];
          if (asked(d) && d.range.begin == begin && d.range.end == end &&
              rows[i].empty())
            rows[i] = near_names(document, *document.language, site, d.range);
        }
      });
  return rows;
}

}  // namespace cursiv
