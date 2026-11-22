#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::detail {

// The generated dispatch proves the owner. A nest reads only the first
// element of its property, while reflection visits the whole list.
template <class F>
void visit_member(
    const AstNode& node,
    cursiv::FunctionRef<void(std::string_view, std::uint32_t, AstNode&)> yield,
    std::size_t limit) {
  const auto& member = detail::ast_cast_unchecked<const typename F::owner>(&node)->*F::ptr;
  const auto one = [&](const auto& value, std::uint32_t at) {
    const auto child = [&](const auto& v) {
      if constexpr (ast_pointer_alt<std::remove_cvref_t<decltype(v)>>)
        if (v != nullptr) yield(F::name, at, *v);
    };
    if constexpr (F::content == FieldContent::variant) std::visit(child, value);
    else child(value);
  };
  if constexpr (F::storage == FieldStorage::vector) {
    std::uint32_t at = 0;
    for (const auto& value : std::span(member).first(std::min(limit, member.size())))
      one(value, at++);
  } else if constexpr (F::storage == FieldStorage::optional) {
    if (member) one(*member, AstReflection::kNoIndex);
  } else {
    one(member, AstReflection::kNoIndex);
  }
}

// The AST classes a member points at.
template <class X>
struct held_targets {
  using type = TypeList<>;
};
template <class... Ts>
struct held_targets<cursiv::reference<Ts...>> {
  using type = TypeList<Ts...>;
};
template <class... Ts>
struct held_targets<cursiv::multi_reference<Ts...>> {
  using type = TypeList<Ts...>;
};
template <class X>
  requires(std::is_pointer_v<X> && std::is_class_v<std::remove_pointer_t<X>>)
struct held_targets<X> {
  using type = TypeList<std::remove_pointer_t<X>>;
};
template <class... As>
struct held_targets<std::variant<As...>>
    : tl_cat<typename held_targets<As>::type...> {};

// The first hit's index, `kNoBranch` for none.
constexpr std::uint8_t first_branch(std::initializer_list<bool> hits) noexcept {
  std::uint8_t at = 0;
  for (const bool hit : hits) {
    if (hit) return at;
    ++at;
  }
  return kNoBranch;
}

// The reference behind a handle: `index` selects the element of a list.
template <class F, class Node>
auto& ref_of(Node& owner, std::uint32_t index) noexcept {
  using Owner =
      std::conditional_t<std::is_const_v<Node>, const typename F::owner, typename F::owner>;
  auto& member = ast_cast_unchecked<Owner>(&owner)->*F::ptr;
  if constexpr (F::storage == FieldStorage::vector)
    return member[index];
  else if constexpr (F::storage == FieldStorage::optional)
    return *member;
  else
    return member;
}

template <class F>
RefState ref_state(const AstNode& owner, std::uint32_t index) noexcept {
  return std::atomic_ref<RefState>(RefAccess::state(ref_of<F>(owner, index)))
      .load(std::memory_order_acquire);
}

// The candidate of `Ts...` a class satisfies, the first that accepts it.
template <class L>
struct RefBranch;
template <class... Ts>
struct RefBranch<TypeList<Ts...>> {
  static std::uint8_t run(const AstTypeTag* tag) noexcept {
    return tag == nullptr ? kNoBranch
                          : first_branch({tag->derivesFrom(&kAstTypeTag<Ts>)...});
  }
  // The candidate the class `N` narrows: the one it derives from.
  template <class N>
  static constexpr std::uint8_t rank = first_branch({std::is_base_of_v<Ts, N>...});
};

// A class of `N` answers the candidate `N` narrows.
template <class N, std::uint8_t Which>
std::uint8_t narrow_branch(const AstTypeTag* tag) noexcept {
  return tag != nullptr && tag->derivesFrom(&kAstTypeTag<N>) ? Which : kNoBranch;
}

template <class F>
consteval RefOps ref_ops() {
  using Targets = typename held_targets<typename F::held>::type;
  constexpr bool multi = is_multi_ref_content(F::content);
  RefOps ops;
  ops.multi = multi;
  ops.branch = &RefBranch<Targets>::run;
  ops.narrow_of = [](const AstNode& o, std::uint32_t i) noexcept {
    return RefAccess::narrow(ref_of<F>(o, i));
  };
  ops.force = [](const AstNode& o, std::uint32_t i) { return ref_of<F>(o, i).resolve(); };
  ops.text = [](const AstNode& o, std::uint32_t i) noexcept -> std::string_view {
    return ref_of<F>(o, i).refText;
  };
  ops.name_id = [](const AstNode& o, std::uint32_t i) noexcept {
    return RefAccess::node_id(ref_of<F>(o, i));
  };
  ops.state = [](const AstNode& o, std::uint32_t i) noexcept {
    return public_ref_state(ref_state<F>(o, i));
  };
  ops.visit = [](const AstNode& owner, std::uint32_t i, const Cst& cst,
                 const ResolvedReferenceVisitor& visitor) {
    if (ref_state<F>(owner, i) != RefState::Resolved) return;
    const auto& ref = ref_of<F>(owner, i);
    const NodeId name = RefAccess::node_id(ref);
    if (name == kNoNode) return;
    if constexpr (multi) {
      const auto count = RefAccess::items(ref).size();
      for (std::size_t k = 0; k < count; ++k) {
        // A visitor can unlink or move the reference between targets.
        if (k != 0 && ref_state<F>(owner, i) != RefState::Resolved) break;
        const auto& items = RefAccess::items(ref_of<F>(owner, i));
        if (k >= items.size()) break;
        if (const AstNode* target = items[k].target)
          visitor(cst[name].begin, cst[name].end, *target, true);
      }
    } else if (const AstNode* target = RefAccess::target(ref)) {
      visitor(cst[name].begin, cst[name].end, *target, false);
    }
  };
  ops.reset = [](AstNode& o, std::uint32_t i) noexcept {
    auto& ref = ref_of<F>(o, i);
    if constexpr (multi) {
      RefAccess::items(ref).clear();
    } else {
      RefAccess::target(ref) = nullptr;
      RefAccess::which(ref) = 0;
    }
    std::atomic_ref<RefState>(RefAccess::state(ref))
        .store(RefState::Unresolved, std::memory_order_release);
  };
  return ops;
}

// `ops` with the narrowings its member's sites declare.
constexpr RefOps narrowed(RefOps ops, const RefBranchFn* narrows,
                                  const AstTypeTag* const* tags, std::uint8_t count) {
  ops.narrows = narrows;
  ops.narrow_tags = tags;
  ops.narrow_count = count;
  return ops;
}

}  // namespace cursiv::detail
