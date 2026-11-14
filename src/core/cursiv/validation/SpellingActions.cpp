#include "cursiv/validation/SpellingActions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {
namespace {

// The optimal string alignment distance over bytes, a transposition counting
// as one, bit-parallel (Hyyro 2003). Names past 64 bytes are not compared; a
// distance past `limit` returns `limit + 1`, saturated.
std::size_t edit_distance(std::string_view a, std::string_view b,
                          std::size_t limit) {
  constexpr std::size_t kWidest = 64;
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  const std::size_t exceeded =
      limit == std::numeric_limits<std::size_t>::max() ? limit : limit + 1;
  if (n > kWidest || m > kWidest) return exceeded;
  if ((n > m ? n - m : m - n) > limit) return exceeded;
  if (n == 0) return std::min(m, exceeded);
  std::array<std::uint64_t, 256> match{};
  for (std::size_t i = 0; i < n; ++i)
    match[static_cast<unsigned char>(a[i])] |= std::uint64_t{1} << i;
  const std::uint64_t last = std::uint64_t{1} << (n - 1);
  std::uint64_t vp = ~std::uint64_t{0};
  std::uint64_t vn = 0;
  std::uint64_t d0 = 0;
  std::uint64_t before = 0;
  std::size_t distance = n;
  for (const char c : b) {
    const std::uint64_t eq = match[static_cast<unsigned char>(c)];
    const std::uint64_t swap = ((~d0 & eq) << 1) & before;
    d0 = (((eq & vp) + vp) ^ vp) | eq | vn | swap;
    std::uint64_t hp = vn | ~(d0 | vp);
    std::uint64_t hn = d0 & vp;
    distance += (hp & last) != 0;
    distance -= (hn & last) != 0;
    hp = (hp << 1) | 1;
    hn <<= 1;
    vp = hn | ~(d0 | hp);
    vn = hp & d0;
    before = eq;
  }
  return std::min(distance, exceeded);
}

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
  const auto before = [](const Near& a, const Near& b) {
    return std::tie(a.distance, a.elsewhere, a.name) <
           std::tie(b.distance, b.elsewhere, b.name);
  };
  // Only the best are kept, nearest first: a scope may hold thousands of
  // near names.
  std::vector<Near> near;
  near.reserve(kMaxSuggestions + 1);
  std::unordered_set<std::string_view> seen;
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
        if (!seen.insert(entry.name).second) return true;
        const Near candidate{entry.name, distance,
                             entry.documentId != document.id};
        near.insert(std::ranges::upper_bound(near, candidate, before),
                    candidate);
        if (near.size() > kMaxSuggestions) near.pop_back();
        return true;
      });
  for (const Near& n : near)
    rows.push_back(detail::make_action_row(
        detail::kReplaceEdit, std::format("Replace with '{}'", n.name), range,
        std::string(n.name), document.buildRevision));
  return rows;
}

}  // namespace

std::vector<JsonValue::Array> detail::spelling_actions(
    const Document& document, std::span<const Diagnostic* const> diagnostics) {
  std::vector<JsonValue::Array> rows(diagnostics.size());
  const DocumentReferences* const references =
      document.parseResult.references.get();
  const auto asked = [](const Diagnostic& d) {
    return data_code(d) == kLinkingError && d.range.begin != d.range.end;
  };
  if (references == nullptr || document.language == nullptr ||
      std::ranges::none_of(diagnostics, [&](const Diagnostic* d) {
        return asked(*d);
      }))
    return rows;
  // The sites one request is served, in the order the validator lists them.
  unsigned spelt = 0;
  references->visitUnresolved(
      document.parseResult.cst(),
      [&](RefState why, TextOffset begin, TextOffset end,
          const ReferenceInfo& site) {
        if (why != RefState::ErrorNotFound || begin == end ||
            spelt == kSpelledSites)
          return;
        bool here = false;
        for (std::size_t i = 0; i < diagnostics.size(); ++i) {
          const Diagnostic& d = *diagnostics[i];
          if (asked(d) && d.range.begin == begin && d.range.end == end &&
              rows[i].empty()) {
            rows[i] = near_names(document, *document.language, site, d.range);
            here = true;
          }
        }
        if (here) ++spelt;
      });
  return rows;
}

}  // namespace cursiv
