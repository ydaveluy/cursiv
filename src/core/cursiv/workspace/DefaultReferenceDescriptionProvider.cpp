#include "cursiv/workspace/DefaultReferenceDescriptionProvider.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv {

namespace {

struct TargetHash {
  std::size_t operator()(const ReferenceDescription& row) const noexcept {
    const auto d = static_cast<std::size_t>(row.targetDocumentId);
    const auto s = static_cast<std::size_t>(row.targetSymbolId);
    return d ^ (s + 0x9e3779b9u + (d << 6u) + (d >> 2u));
  }
};

struct LargeGroup {
  std::size_t first = 0;
  std::optional<std::unordered_set<ReferenceDescription, TargetHash>> seen;

  // A value parameter lets the common path keep its row in registers.
  CURSIV_NOINLINE bool drawn(const std::vector<ReferenceDescription>& rows,
                             ReferenceDescription row) {
    // Small groups never touch this state. Any rows they append invalidate
    // the last large group, rebuilt only when another large one needs it.
    if (!seen || rows.size() != first + seen->size()) {
      first = rows.size();
      while (first != 0 && rows[first - 1].sourceBegin == row.sourceBegin &&
             rows[first - 1].sourceEnd == row.sourceEnd)
        --first;
      seen.emplace(rows.begin() + first, rows.end());
    }
    return !seen->insert(row).second;
  }
};

// The document of each target. A short walk remembers nothing; a long one is
// remembered, and the next one stops where it reaches it, so a deep chain is
// walked once.
class TargetDocuments {
 public:
  [[nodiscard]] const Document* of(const AstNode& node) {
    const AstNode* at = &node;
    std::size_t steps = 0;
    const Document* targetDocument = nullptr;
    for (;;) {
      if (!_known.empty())
        if (const auto hit = _known.find(at); hit != _known.end()) {
          targetDocument = hit->second;
          break;
        }
      if (at->parent == nullptr) {
        targetDocument = ast_cast<Document>(at);
        break;
      }
      at = at->parent;
      ++steps;
    }
    if (steps > kShort)
      for (const AstNode* p = &node; p != at; p = p->parent)
        _known.emplace(p, targetDocument);
    return targetDocument;
  }

 private:
  static constexpr std::size_t kShort = 64;
  std::unordered_map<const AstNode*, const Document*> _known;
};

}  // namespace

std::vector<ReferenceDescription>
DefaultReferenceDescriptionProvider::createDescriptions(
    const Document& document, const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> rows;
  const DocumentReferences* const references =
      document.parseResult.references.get();
  if (references == nullptr) return rows;
  const Cst& cst = document.parseResult.cst();
  rows.reserve(references->handles.size());

  // One row per resolved target, as the inverse table groups rows by target:
  // every member of a multi reference's set finds it. Two targets built on one
  // CST record share one row.
  LargeGroup large;
  TargetDocuments targetDocuments;
  references->visitResolved(cst, [&](TextOffset begin, TextOffset end,
                                     const AstNode& target, bool multi) {
    throw_if_cancelled(cancelToken);
    const Document* const targetDocument = targetDocuments.of(target);
    const ReferenceDescription row{.sourceDocumentId = document.id,
                                   .sourceBegin = begin,
                                   .sourceEnd = end,
                                   .targetDocumentId = targetDocument == nullptr
                                                           ? kNoDocument
                                                           : targetDocument->id,
                                   .targetSymbolId = target.cstNodeId,
                                   .local = targetDocument == &document,
                                   .multiReference = multi};
    // The usual single target sees only the first span check. Large groups
    // switch to hashing after a bounded scan of their most recent targets.
    unsigned compared = 0;
    for (auto it = rows.rbegin();
         it != rows.rend() && it->sourceBegin == row.sourceBegin &&
         it->sourceEnd == row.sourceEnd;
         ++it) {
      if (*it == row) return;
      if (++compared == 16) {
        if (large.drawn(rows, row)) return;
        break;
      }
    }
    rows.push_back(row);
  });

  // Only what resolved is described: a reference pointing nowhere is no edge.
  return rows;
}

}  // namespace cursiv
