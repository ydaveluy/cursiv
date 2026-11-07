#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/MultiRefItems.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"

// One compiled grammar's answer to a reference read, as one table per
// document: the site is a `RefHandle`, and what the member holds is the
// metadata's row for it.

namespace cursiv {

struct DefaultDocumentReferences : DocumentReferences {
  explicit DefaultDocumentReferences(const AstMetaData& meta) noexcept
      : _f(meta) {}

  [[nodiscard]] Answer resolve(std::uint32_t rank) const override;

  // Defined with the resolution frames, since forcing opens a phase on them.
  void force(std::span<const RefHandle> which,
             const CancellationToken& cancelToken) override;

  void forceAt(const Cst& cst, TextOffset offset,
               const CancellationToken& cancelToken) override;

  void visitResolved(const Cst& cst,
                     ResolvedReferenceVisitor visitor) const override;

  [[nodiscard]] bool anyUnresolved() const noexcept override;

  void visitUnresolved(const Cst& cst,
                       UnresolvedReferenceVisitor visitor) const override;

  void visitIn(const Cst& cst, TextOffset begin, TextOffset end,
               ResolvedReferenceVisitor resolved,
               UnresolvedReferenceVisitor unresolved) const override;

  void reset() noexcept override;

  RefState resolveAll(std::uint32_t rank, MultiRefItems& out) const override;

 private:
  // The site as plain data. `reference` is the field index, so nothing is
  // looked up.
  ReferenceInfo info(const RefHandle& h, std::string_view text,
                     std::uint8_t narrow) const;

  // One reference's share of `visitResolved` and `visitUnresolved`.
  void visit_resolved(const Cst& cst, const RefHandle& h,
                      ResolvedReferenceVisitor visitor) const;
  void visit_unresolved(const Cst& cst, const RefHandle& h,
                        UnresolvedReferenceVisitor visitor) const;

  // The ranks whose name touches `[begin, end]`, in rank order.
  [[nodiscard]] std::vector<std::uint32_t> ranks_in(const Cst& cst,
                                                    TextOffset begin,
                                                    TextOffset end) const;
  void index_names(const Cst& cst) const;

  // A named reference by where its name begins, and the furthest end of the
  // names up to it in this order: walking back from the end of a window, a
  // reach before its start says no earlier name can touch it.
  struct NameAt {
    TextOffset begin = 0;
    TextOffset reach = 0;
    std::uint32_t rank = 0;
  };

  const AstMetaData& _f;
  // Built on the first question about a caret or a range: names never move
  // once the table is built, and a table no one asks pays nothing.
  mutable std::vector<NameAt> _names;
  mutable std::mutex _indexing;
  mutable std::atomic<bool> _indexed{false};
};

}  // namespace cursiv
