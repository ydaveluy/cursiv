// One member's write, from a pending to the node that owns it: the storage
// peeled, the nature dispatched, the conversion run or the node wired.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/Pending.hpp"
#include "cursiv/shared/runtime/ast_conv.hpp"

namespace cursiv {

// What a site writes, value or call. A refused conversion leaves the member as
// it was.
template <class M>
CURSIV_ALWAYS_INLINE inline bool run_conv(AstBuildCtx& ctx, NodeId id,
                                          const detail::ConvSlot& c,
                                          M& member) {
  using H = typename detail::strip_storage<M>::type;
  if constexpr (!detail::is_variant_v<H> && std::is_trivially_copyable_v<H>)
    if (c.value != nullptr) {
      // Text that was never written denotes nothing, value or not: the same
      // rule `invoke_conv` states for a repair's zero-width record.
      if (ctx.repaired) [[unlikely]]
        if (const CstNode& n = (*ctx.cst)[id]; n.recovered && n.begin == n.end)
          return false;
      detail::place<H>(member, *static_cast<const H*>(c.value));
      return true;
    }
  return c.fn(ctx, id, &member);
}

// One body per written member. The caller proved the owner, so the cast is a
// fact.
template <class F>
CURSIV_ALWAYS_INLINE inline void write_into(AstBuildCtx& ctx, const Pending& p,
                                            AstNode* owner,
                                            const detail::ConvSlot* conv,
                                            std::uint16_t field, ActKind kind,
                                            std::uint16_t alt) {
  using detail::FieldNature;
  using detail::FieldStorage;
  using X = typename F::held;
  (void)field, (void)kind, (void)alt;
  auto& member = detail::ast_cast_unchecked<typename F::owner>(owner)->*F::ptr;
  if constexpr (F::nature == FieldNature::node) {
    using T = std::remove_pointer_t<X>;
    static_assert(
        std::is_base_of_v<AstNode, T>,
        "a node member points at a class deriving from cursiv::AstNode "
        "-- the builder wires `parent` and the type tag through that "
        "base");
    if (p.value == nullptr) {
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return;
    }
    detail::let_go(detail::node_held(member), owner, ctx.detached);
    detail::place<X>(member, detail::ast_cast_unchecked<T>(p.value));
    p.value->parent = owner;
    ++ctx.attached;
  } else if constexpr (detail::is_ref_nature(F::nature)) {
    // `check_write` refuses a reference source that is not `is_text_pay`.
    assert(conv != nullptr &&
           "a reference member is written from a text payload, and a text "
           "payload always carries a conversion");
    // Borrowed: the input and the tree's kept text both outlive its reader.
    AstNode::string_view text;
    if (!conv->fn(ctx, p.text, &text)) return;
    X& slot = [&]() CURSIV_ALWAYS_INLINE -> X& {
      if constexpr (F::storage == FieldStorage::direct)
        return member;
      else if constexpr (F::storage == FieldStorage::optional)
        return member.has_value() ? *member : member.emplace();
      else
        return member.emplace_back();
    }();
    // Stamped with what this site accepts, which may be less than the member.
    detail::RefAccess::bind(slot, p.text, text, p.narrow);
    std::uint32_t index = 0;
    if constexpr (F::storage == FieldStorage::vector) {
      index = static_cast<std::uint32_t>(member.size() - 1);
    } else {
      // Reassigning a scalar keeps its location, once verified: a copied
      // reference may carry another member's handle.
      const auto*& home = detail::RefAccess::home(slot);
      const std::uint32_t handle = detail::RefAccess::handle(slot);
      if (home == ctx.home && home != nullptr && handle < ctx.refs->size()) {
        const RefHandle& previous = (*ctx.refs)[handle];
        if (previous.owner == owner && previous.field == field &&
            previous.index == 0)
          return;
      }
    }
    // A handle, never a pointer: a vector of references relocates as it grows,
    // the owner does not.
    detail::RefAccess::home(slot) = ctx.home;
    detail::RefAccess::handle(slot) =
        static_cast<std::uint32_t>(ctx.refs->size());
    ctx.refs->push_back(RefHandle{owner, field, index});
  } else if constexpr (F::nature == FieldNature::variant) {
    AstNode* const held = detail::node_held(member);
    if (p.value != nullptr) {
      using L = typename detail::variant_node_alt<X>::type;
      if constexpr (detail::tl_size<L>::value != 0) {
        detail::let_go(held, owner, ctx.detached);
        if (detail::emplace_node_alt<L>(alt, member, p.value)) {
          p.value->parent = owner;
          ++ctx.attached;
          return;
        }
      }
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return;
    }
    if (conv == nullptr) {
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return;
    }
    if (run_conv(ctx, p.text, *conv, member))
      detail::let_go(held, owner, ctx.detached);
  } else {
    // `?=` writes the fact of a match, into a `bool` member.
    if constexpr (std::is_same_v<X, bool>) {
      if (kind == ActKind::flag) {
        const CstNode& n = (*ctx.cst)[p.root];
        if (!n.recovered || n.begin != n.end) detail::place<bool>(member, true);
        return;
      }
    }
    assert(conv != nullptr &&
           "a value member is written by a conversion -- `admits` refuses the "
           "payload that has none");
    (void)run_conv(ctx, p.text, *conv, member);
  }
}

}  // namespace cursiv
