#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/engine/grammar/Facts.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/workspace/AstNodeLocator.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv::test {
namespace detail {

// Visit each grammar property once, including inherited properties.
template <class Grammar, class Visit>
void grammar_properties(const AstNode& node, Visit&& visit) {
  []<class... Rows>(TypeList<Rows...>, const AstNode& n, auto&& visitor) {
    using Fields = cursiv::detail::dedupe_t<
        TypeList<typename Rows::field...>>;
    [&]<class... FieldsT>(TypeList<FieldsT...>) {
      auto one = [&]<class F>() {
        if constexpr (!std::is_void_v<F>)
          if (auto* owner = dynamic_cast<const typename F::owner*>(&n))
            visitor.template operator()<F>(*owner);
      };
      (one.template operator()<FieldsT>(), ...);
    }(Fields{});
  }(typename engine::Facts<Grammar>::rows{}, node, visit);
}

using NodePair = std::pair<const AstNode*, const AstNode*>;

struct AstComparison {
  std::vector<NodePair> children, targets;

  template <class T>
  bool same(const T& a, const T& b) {
    namespace core = cursiv::detail;
    if constexpr (core::is_reference_v<T> || core::is_multi_reference_v<T>) {
      if (a.refText != b.refText || a.resolve() != b.resolve()) return false;
      if constexpr (core::is_reference_v<T>)
        targets.emplace_back(a.target(), b.target());
      else {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
          targets.emplace_back(core::RefAccess::items(a)[i].target,
                               core::RefAccess::items(b)[i].target);
      }
    } else if constexpr (core::strip_storage<T>::storage ==
                         core::FieldStorage::vector) {
      if (a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!same(a[i], b[i])) return false;
    } else if constexpr (core::strip_storage<T>::storage ==
                         core::FieldStorage::optional) {
      return a.has_value() == b.has_value() && (!a || same(*a, *b));
    } else if constexpr (core::is_variant_v<T>) {
      if (a.index() != b.index()) return false;
      if (a.valueless_by_exception()) return true;
      return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return ((a.index() == I && same(std::get<I>(a), std::get<I>(b))) ||
                ...);
      }(std::make_index_sequence<std::variant_size_v<T>>{});
    } else if constexpr (std::is_pointer_v<T> &&
                         std::is_base_of_v<AstNode, std::remove_pointer_t<T>>) {
      if (a && b)
        children.emplace_back(a, b);
      else
        return a == b;
    } else
      return a == b;
    return true;
  }
};

inline bool same_external_target(const AstNode* a, const AstNode* b,
                                 const Document* expectedDoc,
                                 const Document* actualDoc) {
  if (a == b) return true;
  if (!a || !b || a->type != b->type) return false;
  const auto* ad = a->getDocument();
  const auto* bd = b->getDocument();
  return ad && bd && ad->language && bd->language &&
         ((ad == expectedDoc && bd == actualDoc) || ad->uri == bd->uri) &&
         ad->language->astNodeLocator->getAstNodePath(*a) ==
             bd->language->astNodeLocator->getAstNodePath(*b);
}

}  // namespace detail

template <class T>
[[nodiscard]] const T& requireFound(const T* value,
                                     std::string_view description) {
  if (!value) throw ExpectationError("not found: " + std::string(description));
  return *value;
}

// Compare grammar-bound properties and reference targets across separately
// allocated trees.
template <class Grammar>
void expectEqualAst(
    const AstNode& expected, const AstNode& actual,
    FunctionRef<bool(const AstNode&, const AstNode&)> extra =
        [](const AstNode&, const AstNode&) { return true; }) {
  detail::AstComparison compare;
  compare.children.emplace_back(&expected, &actual);
  std::unordered_map<const AstNode*, const AstNode*> paired;
  while (!compare.children.empty()) {
    const auto [a, b] = compare.children.back();
    compare.children.pop_back();
    if (typeid(*a) != typeid(*b)) {
      expectedFunction("different type", "same type", "AST node");
      return;
    }
    if (auto [it, added] = paired.emplace(a, b); !added) {
      if (it->second != b)
        expectedFunction("different child", "same child", "AST containment");
      continue;
    }
    if (!extra(*a, *b))
      expectedFunction("different value", "same value",
                       "application AST properties");
    detail::grammar_properties<Grammar>(*a, [&]<class F>(const auto& owner) {
      const auto* other = dynamic_cast<const typename F::owner*>(b);
      if (!compare.same(owner.*F::ptr, other->*F::ptr))
        expectedFunction("different value", "same value", F::name);
    });
  }
  const auto* expectedDoc = expected.getDocument();
  const auto* actualDoc = actual.getDocument();
  for (const auto& [a, b] : compare.targets) {
    const auto it = paired.find(a);
    if (it == paired.end()
            ? !detail::same_external_target(a, b, expectedDoc, actualDoc)
            : it->second != b)
      expectedFunction("different target", "same target", "AST reference");
  }
}

// The first reference of this declared C++ type matching its spelling or exact
// source range.
template <class Grammar, class Ref>
[[nodiscard]] const Ref* findReference(const Document& doc,
                                        std::optional<std::string_view> text,
                                        std::optional<TextRange> range = {}) {
  static_assert(cursiv::detail::is_reference_v<Ref> ||
                cursiv::detail::is_multi_reference_v<Ref>);
  const Ref* result = nullptr;
  auto check = [&](const Ref& ref) {
    if (result || (text && ref.refText != *text)) return;
    if (range) {
      const auto& cst = doc.parseResult.cst();
      const auto id = cursiv::detail::RefAccess::node_id(ref);
      if (id >= cst.size() || cst[id].begin != range->begin ||
          cst[id].end != range->end)
        return;
    }
    result = &ref;
  };
  for (const AstNode* n : doc.parseResult.nodes()) {
    detail::grammar_properties<Grammar>(*n, [&]<class F>(const auto& owner) {
      if constexpr (std::is_same_v<typename F::held, Ref>) {
        const auto& value = owner.*F::ptr;
        if constexpr (F::storage == cursiv::detail::FieldStorage::vector) {
          for (const auto& ref : value) check(ref);
        } else if constexpr (F::storage ==
                             cursiv::detail::FieldStorage::optional) {
          if (value) check(*value);
        } else
          check(value);
      }
    });
    if (result) break;
  }
  return result;
}
}  // namespace cursiv::test
