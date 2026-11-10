#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::detail {

// The generated type dispatch proves each owner before this accessor runs.
template <class F>
void visit_member(
    const AstNode& node,
    cursiv::function_ref<void(std::string_view, std::uint32_t, AstNode&)>
        yield) {
  if constexpr (F::content == FieldContent::variant) {
    const auto& owner =
        *detail::ast_cast_unchecked<const typename F::owner>(&node);
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
  } else if constexpr (F::content != FieldContent::node) {
    (void)node;
    (void)yield;
  } else {
    const auto& owner =
        *detail::ast_cast_unchecked<const typename F::owner>(&node);
    if constexpr (F::storage == FieldStorage::vector) {
      std::uint32_t at = 0;
      for (const auto& child : owner.*F::ptr) {
        if (child != nullptr) yield(F::name, at, *child);
        ++at;
      }
    } else if constexpr (F::storage == FieldStorage::optional) {
      // Absent and present-but-null are one answer here: neither is a child.
      const auto& held = owner.*F::ptr;
      if (held.has_value() && *held != nullptr)
        yield(F::name, AstReflection::kNoIndex, **held);
    } else if (const auto& child = owner.*F::ptr; child != nullptr) {
      yield(F::name, AstReflection::kNoIndex, *child);
    }
  }
}

// The generator selects applicable fields in the classes' declaration order. A
// known type therefore needs one dispatch, without testing each member's owner
// again.
template <class T, class Fields>
struct AstMemberAccess;

template <class T, class... Fs>
struct AstMemberAccess<T, TypeList<Fs...>> {
  static_assert(std::is_void_v<T> ||
                (std::is_base_of_v<typename Fs::owner, T> && ...));

  static void visit(
      const AstNode& node,
      function_ref<void(std::string_view, std::uint32_t, AstNode&)> yield) {
    (void)node;
    (void)yield;
    (visit_member<Fs>(node, yield), ...);
  }
  // A manually constructed subtype may carry a tag absent from the grammar.
  // Keep its inherited children visible, once each, in the same order.
  static void visitUnknown(
      const AstNode& node,
      function_ref<void(std::string_view, std::uint32_t, AstNode&)> yield) {
    const auto one = [&]<class F>() {
      if constexpr (F::content == FieldContent::node ||
                    F::content == FieldContent::variant)
        if (node.type != nullptr &&
            node.type->derivesFrom(&ast_type_tag<typename F::owner>))
          visit_member<F>(node, yield);
    };
    (void)one;
    (one.template operator()<Fs>(), ...);
  }
};

}  // namespace cursiv::detail
