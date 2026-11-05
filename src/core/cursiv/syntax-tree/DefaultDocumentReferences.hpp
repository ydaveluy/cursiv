#pragma once

#include <cstdint>
#include <mutex>
#include <string_view>

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

  void forceAll(const CancellationToken& cancelToken) override;

  void forceAt(const Cst& cst, TextOffset offset,
               const CancellationToken& cancelToken) override;

  void visitResolved(const Cst& cst,
                     ResolvedReferenceVisitor visitor) const override;

  [[nodiscard]] bool anyUnresolved() const noexcept override;

  void visitUnresolved(const Cst& cst,
                       UnresolvedReferenceVisitor visitor) const override;

  void reset() noexcept override;

  RefState resolveAll(std::uint32_t rank, MultiRefItems& out) const override;

 private:
  // The site as plain data. `reference` is the field index, so nothing is
  // looked up.
  ReferenceInfo info(const RefHandle& h, std::string_view text,
                     std::uint8_t narrow) const;

  const AstMetaData& _f;
};

}  // namespace cursiv
