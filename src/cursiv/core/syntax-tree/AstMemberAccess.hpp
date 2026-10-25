#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

// The generated type dispatch proves each owner before these accessors run.
// A typed walk, because reading a member needs the member's type: the
// dispatch resolves at compile time to one test per member that can be
// absent. "Missing" is only asked where absence is representable -- a
// null view, a null node pointer; a `bool` or an `int32` has none, so
// this does not pretend to check one. A variant is judged on its active arm:
// left at a null first arm, it was never written.
template <class A>
bool arm_written(const A& arm) {
  if constexpr (std::is_pointer_v<A>)
    return arm != nullptr;
  else if constexpr (std::is_same_v<A, AstNode::string_view>)
    return arm.data() != nullptr;
  else
    return !std::is_same_v<A, std::monostate>;
}

template <class F>
bool member_written(const AstNode& node) {
  if constexpr (F::storage != FieldStorage::direct) {
    (void)node;
    return true;  // `optional<X>` and the list form ARE the optional ones
  } else {
    const auto& owner = static_cast<const typename F::owner&>(node);
    if constexpr (F::nature == FieldNature::node) {
      return (owner.*F::ptr) != nullptr;
    } else if constexpr (is_ref_nature(F::nature)) {
      // On its text and never on its target: an unresolved name is a
      // linking verdict about a member that was written.
      return (owner.*F::ptr).refText.data() != nullptr;
    } else if constexpr (F::nature == FieldNature::value &&
                         std::is_same_v<typename F::held,
                                        AstNode::string_view>) {
      return (owner.*F::ptr).data() != nullptr;
    } else if constexpr (F::nature == FieldNature::variant) {
      return std::visit([](const auto& arm) { return arm_written(arm); },
                        owner.*F::ptr);
    } else {
      return true;  // a scalar has no absent state
    }
  }
}

// The same dispatch, reading instead of asking whether it was written.
template <class F>
void visit_member(
    const AstNode& node,
    cursiv::function_ref<void(std::string_view, std::uint32_t, AstNode&)>
        yield) {
  if constexpr (F::nature == FieldNature::variant) {
    const auto& owner = static_cast<const typename F::owner&>(node);
    const auto one = [&](const auto& v, std::uint32_t at) {
      std::visit(
          [&](const auto& alt) {
            if constexpr (ast_pointer_alt<std::remove_cvref_t<decltype(alt)>>)
              if (alt != nullptr) yield(F::name, at, *alt);
          },
          v);
    };
    if constexpr (F::storage == FieldStorage::vector) {
      std::uint32_t at = 0;
      for (const auto& v : owner.*F::ptr) one(v, at++);
    } else if constexpr (F::storage == FieldStorage::optional) {
      const auto& held = owner.*F::ptr;
      if (held.has_value()) one(*held, AstReflection::kNoIndex);
    } else {
      one(owner.*F::ptr, AstReflection::kNoIndex);
    }
  } else if constexpr (F::nature != FieldNature::node) {
    (void)node;
    (void)yield;
  } else {
    const auto& owner = static_cast<const typename F::owner&>(node);
    if constexpr (F::storage == FieldStorage::vector) {
      std::uint32_t at = 0;
      for (auto* const child : owner.*F::ptr) {
        if (child != nullptr) yield(F::name, at, *child);
        ++at;
      }
    } else if constexpr (F::storage == FieldStorage::optional) {
      // Absent and present-but-null are one answer here: neither is a child.
      const auto& held = owner.*F::ptr;
      if (held.has_value() && *held != nullptr)
        yield(F::name, AstReflection::kNoIndex, **held);
    } else if (auto* const child = owner.*F::ptr; child != nullptr) {
      yield(F::name, AstReflection::kNoIndex, *child);
    }
  }
}

}  // namespace cursiv::detail

namespace cursiv {

// The generator selects applicable fields in the classes' declaration order. A
// known type therefore needs one dispatch, without testing each member's owner
// again.
template <class T, class Fields>
struct AstMemberAccess;

template <class T, class... Fs>
struct AstMemberAccess<T, TypeList<Fs...>> {
  static_assert(std::is_void_v<T> ||
                (std::is_base_of_v<typename Fs::owner, T> && ...));

  static bool complete(const AstNode& node) {
    (void)node;
    return (detail::member_written<Fs>(node) && ...);
  }
  static void visit(
      const AstNode& node,
      function_ref<void(std::string_view, std::uint32_t, AstNode&)> yield) {
    (void)node;
    (void)yield;
    (detail::visit_member<Fs>(node, yield), ...);
  }
  // A manually constructed subtype may carry a tag absent from the grammar.
  // Keep its inherited children visible, once each, in the same order.
  static void visitUnknown(
      const AstNode& node,
      function_ref<void(std::string_view, std::uint32_t, AstNode&)> yield) {
    const auto one = [&]<class F>() {
      if constexpr (F::nature == detail::FieldNature::node ||
                    F::nature == detail::FieldNature::variant)
        if (node.type != nullptr &&
            node.type->derivesFrom(&ast_type_tag<typename F::owner>))
          detail::visit_member<F>(node, yield);
    };
    (void)one;
    (one.template operator()<Fs>(), ...);
  }
};

}  // namespace cursiv
