// The symbols an `AstMetaData` names: what a reflection reads off one
// member, and the ops a linker runs on a reference whose type it does not
// know -- each templated on the strict minimum, so only the arrays are
// per-grammar.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/runtime/ast_fields.hpp"

namespace cursiv::detail {

// The tag answers its own family: a tag carries the closure of its bases,
// so the question is about the class and no grammar is consulted.
template <class T>
bool tag_is_a(const AstTypeTag* t) {
  return t != nullptr && t->derivesFrom(&ast_type_tag<T>);
}

// One `RefOps` per field: a linker holding only a `RefHandle` reads and
// writes a reference whose type it does not know.

// The reference behind a handle. `index` selects the element only when the
// member is a vector of references; `optional` is dereferenced. Constness
// follows `owner`.
template <class F, class Node>
  requires std::is_base_of_v<AstNode, std::remove_const_t<Node>>
auto& ref_of(Node& owner, std::uint32_t index) noexcept {
  using Owner =
      cond_t<std::is_const_v<Node>, const typename F::owner, typename F::owner>;
  auto& m = static_cast<Owner&>(owner).*F::ptr;
  if constexpr (F::storage == FieldStorage::vector)
    return m[index];
  else if constexpr (F::storage == FieldStorage::optional)
    return *m;
  else
    return m;
}

// The index in `Ts...` a candidate satisfies, or `kNoBranch`.
template <class L>
struct ref_branch;
template <class... Ts>
struct ref_branch<TypeList<Ts...>> {
  static std::uint8_t run(const AstTypeTag* t) noexcept {
    return first_branch({tag_is_a<Ts>(t)...});
  }
};

// `Which` is the rank of the candidate the narrow type derives from -- exactly
// one, by the law -- so a tag this filter accepts lands at the rank the
// member's own row would have given it, and `get<T>()` keeps its meaning.
template <class N, std::uint8_t Which>
struct ref_narrow {
  static std::uint8_t run(const AstTypeTag* t) noexcept {
    return tag_is_a<N>(t) ? Which : kNoBranch;
  }
};

// Empty where the grammar does not narrow, which leaves `narrow_count` zero and
// the filter the member's own.
template <class Targets, class L>
struct narrow_table;
template <class Targets, class... Ns>
struct narrow_table<Targets, TypeList<Ns...>> {
  static constexpr std::array<RefBranchFn, sizeof...(Ns)> fns{
      &ref_narrow<Ns, narrow_bases<Ns, Targets>::rank>::run...};
  // The narrow types by name, for the diagnostic that says what was expected.
  static constexpr std::array<const AstTypeTag*, sizeof...(Ns)> tags{
      &ast_type_tag<Ns>...};
};

template <class F>
RefState ref_state_raw(const AstNode& o, std::uint32_t i) noexcept {
  return std::atomic_ref<RefState>(ref_of<F>(o, i)._state)
      .load(std::memory_order_acquire);
}

// `force` resolves through the reference's own protocol; `size`, `at` and
// `single` load the state with acquire before reading the targets.
template <class F, class Ns>
consteval RefOps ref_ops_of() {
  // One row shape for both natures: a linker holds a `RefHandle` and nothing
  // else, so how many targets a name reaches stays the grammar's business.
  if constexpr (!is_ref_nature(F::nature))
    return RefOps{};  // a non-reference field: a null entry
  else {
    using L = typename held_targets<typename F::held>::type;
    RefOps ops{
        .multi = is_multi_ref_nature(F::nature),
        .branch = &ref_branch<L>::run,
        .narrows = narrow_table<L, Ns>::fns.data(),
        .narrow_count = static_cast<std::uint8_t>(tl_size<Ns>::value),
        .narrow_tags = narrow_table<L, Ns>::tags.data(),
        .narrow_of =
            [](const AstNode& o, std::uint32_t i) noexcept {
              return ref_of<F>(o, i).narrow;
            },
        .force = [](const AstNode& o,
                    std::uint32_t i) { return ref_of<F>(o, i).resolve(); },
        .text =
            [](const AstNode& o, std::uint32_t i) noexcept {
              return ref_of<F>(o, i).refText;
            },
        .name_id =
            [](const AstNode& o, std::uint32_t i) noexcept {
              return ref_of<F>(o, i).refNodeId;
            },
        .state =
            [](const AstNode& o, std::uint32_t i) noexcept {
              return cursiv::detail::public_ref_state(ref_state_raw<F>(o, i));
            },
        .size = [](const AstNode& o, std::uint32_t i) noexcept -> std::size_t {
          if (ref_state_raw<F>(o, i) != RefState::Resolved) return 0;
          const auto& r = ref_of<F>(o, i);
          if constexpr (is_multi_ref_nature(F::nature))
            return r.items.size();
          else
            return r.target != nullptr ? 1u : 0u;
        },
        .at = [](const AstNode& o, std::uint32_t i,
                 std::size_t k) noexcept -> AstNode* {
          if (ref_state_raw<F>(o, i) != RefState::Resolved) return nullptr;
          const auto& r = ref_of<F>(o, i);
          if constexpr (is_multi_ref_nature(F::nature))
            return k < r.items.size() ? r.items[k].target : nullptr;
          else
            return k == 0 ? r.target : nullptr;
        },
        .reset =
            [](AstNode& o, std::uint32_t i) noexcept {
              auto& r = ref_of<F>(o, i);
              if constexpr (is_multi_ref_nature(F::nature))
                r.items.clear();
              else {
                r.target = nullptr;
                r.which = 0;
              }
              std::atomic_ref<RefState>(r._state).store(
                  RefState::Unresolved, std::memory_order_release);
            },
    };
    if constexpr (!is_multi_ref_nature(F::nature))
      ops.single = [](const AstNode& o, std::uint32_t i) noexcept {
        if (ref_state_raw<F>(o, i) != RefState::Resolved)
          return SingleRefRead{};
        const auto& r = ref_of<F>(o, i);
        return SingleRefRead{r.refNodeId, r.target};
      };
    return ops;
  }
}
// One tag per `reference<Ts...>` the grammar declares. Null anywhere else,
// `multi_reference` included: the tag is the single reference's own type, and
// a list of them has none to name.
template <class F>
consteval const AstTypeTag* reference_tag_of() {
  if constexpr (nature_of<typename F::held> == FieldNature::reference)
    return &ast_type_tag<typename F::held>;
  else
    return nullptr;
}

}  // namespace cursiv::detail
