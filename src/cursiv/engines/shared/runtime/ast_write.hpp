// One member's write, from a pending to the node that owns it: the storage
// peeled, the nature dispatched, the conversion run or the node wired.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/runtime/AstActions.hpp"
#include "cursiv/engines/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/engines/shared/runtime/Pending.hpp"
#include "cursiv/engines/shared/runtime/ast_conv.hpp"

namespace cursiv {

// What a site writes, value or call.
template <class X>
CURSIV_ALWAYS_INLINE inline bool run_conv(AstBuildCtx& ctx, NodeId id,
                                          const detail::ConvSlot& c, X& slot) {
  if constexpr (!detail::is_variant_v<X> && std::is_trivially_copyable_v<X>)
    if (c.value != nullptr) {
      // Text that was never written denotes nothing, value or not: the same
      // rule `invoke_conv` states for a repair's zero-width record.
      if (ctx.repaired) [[unlikely]]
        if (const CstNode& n = (*ctx.cst)[id]; n.recovered && n.begin == n.end)
          return false;
      slot = *static_cast<const X*>(c.value);
      return true;
    }
  return c.fn(ctx, id, &slot);
}

// Whether the slot was written: a refused conversion leaves an `optional`
// disengaged and a `vector` unchanged.
template <class F, class X>
CURSIV_ALWAYS_INLINE inline bool write_slot(AstBuildCtx& ctx, const Pending& p,
                                            AstNode* owner,
                                            const detail::ConvSlot* conv,
                                            std::uint16_t field, ActKind kind,
                                            X& slot, std::size_t index,
                                            std::uint16_t alt) {
  using detail::FieldNature;
  using detail::FieldStorage;
  (void)owner, (void)conv, (void)field, (void)kind, (void)index, (void)alt;
  if constexpr (F::nature == FieldNature::node) {
    using T = std::remove_pointer_t<X>;
    static_assert(
        std::is_base_of_v<AstNode, T>,
        "a node member points at a class deriving from cursiv::AstNode "
        "-- the builder wires `parent` and the type tag through that "
        "base");
    if (p.value == nullptr) {
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return false;
    }
    if constexpr (F::storage != FieldStorage::vector)
      detail::detach_held(slot, owner, ctx.detached);
    slot = static_cast<T*>(p.value);
    p.value->parent = owner;
    ++ctx.attached;
    return true;
  } else if constexpr (detail::is_ref_nature(F::nature)) {
    // `check_write` refuses a reference source that is not `is_text_pay`.
    assert(conv != nullptr &&
           "a reference member is written from a text payload, and a text "
           "payload always carries a conversion");
    // Borrowed: the input, the CST pool or the converter's resource all
    // outlive the tree's reader.
    AstNode::string_view text;
    if (!conv->fn(ctx, p.text, &text)) return false;
    // Stamped with what this site accepts, which may be less than the member.
    slot.bind(p.text, text, p.narrow);
    if constexpr (F::storage != FieldStorage::vector) {
      // Reassigning a scalar keeps its location, once verified: a copied
      // reference may carry another member's handle.
      if (slot.home == ctx.home && slot.home != nullptr &&
          slot.handle < ctx.refs->size()) {
        const RefHandle& previous = (*ctx.refs)[slot.handle];
        if (previous.owner == owner && previous.field == field &&
            previous.index == index)
          return true;
      }
    }
    // A handle, never a pointer: a vector of references relocates as it grows,
    // the owner does not.
    slot.home = ctx.home;
    slot.handle = static_cast<std::uint32_t>(ctx.refs->size());
    ctx.refs->push_back(
        RefHandle{owner, field, static_cast<std::uint32_t>(index)});
    return true;
  } else if constexpr (F::nature == FieldNature::variant) {
    if (p.value != nullptr) {
      using L = typename detail::variant_node_alt<X>::type;
      if constexpr (detail::tl_size<L>::value != 0) {
        if constexpr (F::storage != FieldStorage::vector)
          detail::detach_held(slot, owner, ctx.detached);
        if (detail::emplace_node_alt<L>(alt, slot, p.value)) {
          p.value->parent = owner;
          ++ctx.attached;
          return true;
        }
      }
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return false;
    }
    if (conv == nullptr) {
      ctx.fail(AstBuildError::malformed_cst, p.root);
      return false;
    }
    if constexpr (F::storage == FieldStorage::direct) {
      const X held = slot;
      if (!run_conv(ctx, p.text, *conv, slot)) return false;
      detail::detach_held(held, owner, ctx.detached);
      return true;
    } else {
      return run_conv(ctx, p.text, *conv, slot);
    }
  } else {
    // `?=` writes the fact of a match, into a `bool` member.
    if constexpr (std::is_same_v<X, bool>) {
      if (kind == ActKind::flag) {
        const CstNode& n = (*ctx.cst)[p.root];
        if (n.recovered && n.begin == n.end) return false;
        slot = true;
        return true;
      }
    }
    assert(conv != nullptr &&
           "a value member is written by a conversion -- `admits` refuses the "
           "payload that has none");
    return run_conv(ctx, p.text, *conv, slot);
  }
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
  using O = typename F::owner;
  using X = typename F::held;
  auto& member = static_cast<O*>(owner)->*F::ptr;
  if constexpr (F::storage == FieldStorage::direct) {
    (void)write_slot<F>(ctx, p, owner, conv, field, kind, member, 0, alt);
  } else if constexpr (F::storage == FieldStorage::optional) {
    X value{};
    if constexpr (detail::is_ref_nature(F::nature)) {
      // A temporary, so failure preserves the old value; only its registered
      // location is inherited.
      if (member.has_value()) {
        value.home = member->home;
        value.handle = member->handle;
      }
    }
    if (write_slot<F>(ctx, p, owner, conv, field, kind, value, 0, alt)) {
      if constexpr (F::nature == FieldNature::node ||
                    F::nature == FieldNature::variant)
        if (member.has_value())
          detail::detach_held(*member, owner, ctx.detached);
      member = std::move(value);
    }
  } else {
    // The handle's index is the position the element is about to take.
    X value{};
    if (write_slot<F>(ctx, p, owner, conv, field, kind, value, member.size(),
                      alt))
      member.push_back(*ctx.arena, value);
  }
}

}  // namespace cursiv
