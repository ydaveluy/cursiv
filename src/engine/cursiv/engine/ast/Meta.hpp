// What a language's AST tables read of each published row, typed: the
// generated source picks from these by action number.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "cursiv/engine/grammar/Row.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstMemberAccess.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

struct RowMeta {
  // The class the row builds, closes or writes into, and a new node of it
  // where the row builds one.
  const AstTypeTag* type = nullptr;
  detail::NewNodeFn make = nullptr;
  // The member it writes, where it writes one.
  std::string_view name;
  detail::FieldContent content = detail::FieldContent::value;
  detail::FieldStorage storage = detail::FieldStorage::direct;
  const AstTypeTag* owner = nullptr;
  const AstTypeTag* const* targets = nullptr;
  // A single reference's own type.
  const AstTypeTag* referenceTag = nullptr;
  detail::YieldFn yield = nullptr;
  // A reference member's operations, its narrowings left out.
  detail::RefOps ops{};
  // The class the row narrows a reference to, and the filter that says so.
  const AstTypeTag* narrow = nullptr;
  detail::RefBranchFn narrows = nullptr;
};

template <class R>
consteval RowMeta row_meta() {
  RowMeta m;
  if constexpr (R::kind == WriteKind::convert) return m;
  m.type = &kAstTypeTag<typename R::type>;
  if constexpr (R::kind == WriteKind::create || R::kind == WriteKind::climb ||
                R::kind == WriteKind::nest)
    m.make = &detail::new_ast_node<typename R::type>;
  using F = typename R::field;
  if constexpr (!std::is_void_v<F>) {
    using Targets = typename detail::held_targets<typename F::held>::type;
    m.name = F::name;
    m.content = F::content;
    m.storage = F::storage;
    m.owner = &kAstTypeTag<typename F::owner>;
    m.targets = TagsOf<Targets>::all;
    if constexpr (F::content == detail::FieldContent::node ||
                  F::content == detail::FieldContent::variant)
      m.yield = &detail::visit_member<F>;
    if constexpr (detail::is_ref_content(F::content)) {
      m.ops = detail::ref_ops<F>();
      if constexpr (F::content == detail::FieldContent::reference)
        m.referenceTag = &kAstTypeTag<typename F::held>;
      if constexpr (!std::is_void_v<typename R::narrow>) {
        m.narrow = &kAstTypeTag<typename R::narrow>;
        m.narrows = &detail::narrow_branch<typename R::narrow,
                                   detail::RefBranch<Targets>::template rank<typename R::narrow>>;
      }
    }
  }
  return m;
}

template <class Rows>
struct RowMetaOf;
template <class... Rs>
struct RowMetaOf<TypeList<Rs...>> {
  // One more than the rows: no array holds nothing.
  static constexpr RowMeta all[sizeof...(Rs) + 1] = {row_meta<Rs>()...};
};

}  // namespace cursiv::engine
