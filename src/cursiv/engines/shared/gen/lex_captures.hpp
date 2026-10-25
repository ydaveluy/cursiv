// Whether an expression defines or reads a capture: a terminal that does keeps
// a call boundary of its own.
#pragma once

#include <type_traits>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

template <class E>
struct lex_has_captures;
template <class Kids>
struct lex_kids_have_captures;
template <class... Es>
struct lex_kids_have_captures<TypeList<Es...>>
    : std::bool_constant<(lex_has_captures<Es>::value || ...)> {};
template <class E>
struct lex_has_captures : lex_kids_have_captures<typename ExprShape<E>::kids> {
};
template <>
struct lex_has_captures<void> : std::false_type {};
template <>
struct lex_has_captures<WordBound> : std::false_type {};
template <class Tag, class E>
struct lex_has_captures<Mark<Tag, E>> : std::true_type {};
template <class Tag>
struct lex_has_captures<Same<Tag>> : std::true_type {};
template <str S, bool CI, class Guard>
struct lex_has_captures<Lit<S, CI, Guard>> : lex_has_captures<Guard> {};

}  // namespace cursiv::detail
