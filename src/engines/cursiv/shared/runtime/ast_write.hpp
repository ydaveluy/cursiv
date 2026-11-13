// One member's write, from a pending value to the node that owns it.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/Pending.hpp"
#include "cursiv/shared/runtime/ast_conv.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// What a site writes, value or call. A refused conversion leaves the member as
// it was.
template <class M>
CURSIV_ALWAYS_INLINE inline bool run_conv(AstBuildCtx& ctx, NodeId id,
                                          const ConvSlot& c, M& member) {
  using H = typename strip_storage<M>::type;
  if constexpr (!is_variant_v<H> && std::is_trivially_copyable_v<H>)
    if (c.value != nullptr) {
      // Text that was never written denotes nothing, value or not: the same
      // rule `invoke_conv` states for a repair's zero-width record.
      if (ctx.repaired) [[unlikely]]
        if (const CstNode& n = (*ctx.cst)[id]; n.recovered && n.begin == n.end)
          return false;
      place<H>(member, *static_cast<const H*>(c.value));
      return true;
    }
  if constexpr (std::is_same_v<H, std::string_view>)
    if (c.fn == nullptr) {
      // This slot denotes raw text, including a genuinely empty match. A
      // zero-width record invented by a repair still writes nothing.
      if (ctx.repaired)
        if (const CstNode& n = (*ctx.cst)[id]; n.recovered && n.begin == n.end)
          return false;
      place<H>(member, ctx.cst->text(id));
      return true;
    }
  return c.fn(ctx, id, &member);
}

// One body per written member. The caller checked the owner's class, so the
// cast is safe.
template <class F>
CURSIV_ALWAYS_INLINE inline void write_into(AstBuildCtx& ctx, const Pending& p,
                                            AstNode* owner,
                                            const ConvSlot* conv,
                                            std::uint16_t field,
                                            std::uint16_t alt) {
  using X = typename F::held;
  (void)field, (void)alt;
  auto& member = ast_cast_unchecked<typename F::owner>(owner)->*F::ptr;
  if constexpr (F::content == FieldContent::node) {
    using T = std::remove_pointer_t<X>;
    static_assert(
        std::is_base_of_v<AstNode, T>,
        "a node member points at a class deriving from cursiv::AstNode "
        "-- the builder wires `parent` and the type tag through that "
        "base");
    if (p.value == nullptr) broken_tape(p.root);
    let_go(node_held(member), owner, ctx.detached);
    place<X>(member, ast_cast_unchecked<T>(p.value));
    p.value->parent = owner;
    ++ctx.attached;
  } else if constexpr (is_ref_content(F::content)) {
    // `check_write` refuses a reference source that is not `is_text_payload`.
    assert(conv != nullptr &&
           "a reference member is written from a text payload, and a text "
           "payload always carries a conversion");
    // Borrowed: the input and the tree's kept text both outlive its reader.
    AstNode::string_view text;
    if (!run_conv(ctx, p.text, *conv, text)) return;
    X& slot = [&]() CURSIV_ALWAYS_INLINE -> X& {
      if constexpr (F::storage == FieldStorage::direct)
        return member;
      else if constexpr (F::storage == FieldStorage::optional)
        return member.has_value() ? *member : member.emplace();
      else
        return member.emplace_back();
    }();
    // Bound to the targets this site accepts, which may be fewer than the
    // member's.
    RefAccess::bind(slot, p.text, text, p.narrow);
    std::uint32_t index = 0;
    if constexpr (F::storage == FieldStorage::vector) {
      index = static_cast<std::uint32_t>(member.size() - 1);
    } else {
      // Reassigning a scalar keeps its location, once verified: a copied
      // reference may carry another member's handle.
      const auto*& resolver = RefAccess::resolver(slot);
      const std::uint32_t handle = RefAccess::handle(slot);
      if (resolver == ctx.resolver && resolver != nullptr &&
          handle < ctx.refs->size()) {
        const RefHandle& previous = (*ctx.refs)[handle];
        if (previous.owner == owner && previous.field == field &&
            previous.index == 0)
          return;
      }
    }
    // A handle, never a pointer: a vector of references relocates as it grows,
    // the owner does not.
    RefAccess::resolver(slot) = ctx.resolver;
    RefAccess::handle(slot) = static_cast<std::uint32_t>(ctx.refs->size());
    ctx.refs->emplace_back(owner, field, index);
  } else if constexpr (F::content == FieldContent::variant) {
    AstNode* const held = node_held(member);
    if (p.value != nullptr) {
      using L = typename variant_node_alt<X>::type;
      if constexpr (tl_size<L>::value != 0) {
        let_go(held, owner, ctx.detached);
        if (emplace_node_alt<L>(alt, member, p.value)) {
          p.value->parent = owner;
          ++ctx.attached;
          return;
        }
      }
      broken_tape(p.root);
    }
    if (conv == nullptr) broken_tape(p.root);
    if (run_conv(ctx, p.text, *conv, member)) let_go(held, owner, ctx.detached);
  } else {
    // `?=` writes the fact of a match, into a `bool` member.
    if constexpr (std::is_same_v<X, bool>) {
      if (p.kind == ActKind::flag) {
        if (const CstNode& n = (*ctx.cst)[p.root];
            !n.recovered || n.begin != n.end)
          place<bool>(member, true);
        return;
      }
    }
    assert(conv != nullptr &&
           "a value member is written by a conversion -- `admits` refuses the "
           "payload that has none");
    (void)run_conv(ctx, p.text, *conv, member);
  }
}

}  // namespace cursiv::engine
