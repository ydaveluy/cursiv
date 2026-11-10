#include "cursiv/references/References.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <tuple>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv::detail {

std::vector<ReferenceDescription> collect_references_per_declaration(
    const References& references, std::span<const AstNode* const> targets,
    function_ref<FindReferencesOptions(const AstNode&)> options,
    const CancellationToken& cancelToken) {
  std::vector<ReferenceDescription> rows;
  for (const AstNode* const target : targets) {
    throw_if_cancelled(cancelToken);
    std::ranges::move(
        references.findReferences(*target, options(*target), cancelToken),
        std::back_inserter(rows));
  }
  const auto key = [](const ReferenceDescription& row) {
    return std::tuple{row.sourceDocumentId, row.sourceBegin, row.sourceEnd};
  };
  // Stable, so the row a site keeps is the first target's that reaches it.
  std::ranges::stable_sort(rows, {}, key);
  const auto repeats = std::ranges::unique(rows, {}, key);
  rows.erase(repeats.begin(), repeats.end());
  return rows;
}

}  // namespace cursiv::detail
