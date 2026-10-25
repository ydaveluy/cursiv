// The row a directive names, with the payload it converts: resolved through
// the stage, so the published list and the walk over a view read an override
// the same way.
#pragma once

#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/Row.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv::detail {

// S is the stage or a view over it: the rows list reads the first, a
// definition the second.
template <class S>
struct stage_of {
  using type = S;
};
template <class S>
  requires requires { typename S::stage; }
struct stage_of<S> {
  using type = typename S::stage;
};

// The payload a write converts, already erased: a rule indistinguishable
// from a bare matcher is the same action, `void` where nothing converts.
template <class St, class R>
struct src_rule {
  using type =
      erased_src_t<pay_text<typename stage_of<St>::type::template rule<R>>>;
};
template <class St>
struct src_rule<St, void> {
  using type = pay_text<void>;
};
template <class St, class L>
struct text_src {
  using type = void;
};
template <class St, class R, class... Rest>
struct text_src<St, TypeList<pay_text<R>, Rest...>> : src_rule<St, R> {};
template <class St, class S0, class... Rest>
struct text_src<St, TypeList<S0, Rest...>> : text_src<St, TypeList<Rest...>> {};
template <class St, class E>
using pay_src_t = typename text_src<St, typename payload_of<E>::type>::type;

template <class St, auto S, WriteKind K, class E>
using spec_row_t = Row<K, spec_class_t<S>, Wrote<spec_ptr<S>>, spec_narrow_t<S>,
                       pay_src_t<St, E>>;

// The row a write directive names.
template <class St, class D>
struct directive_row;
template <class St, class T, class E>
struct directive_row<St, Create<T, E>> {
  using type = Row<WriteKind::create, T>;
};
template <class St, class T, auto P, class E>
struct directive_row<St, Nest<T, P, E>> {
  using type = Row<WriteKind::nest, T, Wrote<P>>;
};
template <class St, auto Sp, class E>
struct directive_row<St, Assign<Sp, E>> {
  using type = spec_row_t<St, Sp, WriteKind::assign, E>;
};
template <class St, auto Sp, class E>
struct directive_row<St, Append<Sp, E>> {
  using type = spec_row_t<St, Sp, WriteKind::append, E>;
};
template <class St, auto P, class E>
struct directive_row<St, Flag<P, E>> {
  using type = Row<WriteKind::flag,
                   typename member_traits<decltype(P)>::class_type, Wrote<P>>;
};

}  // namespace cursiv::detail
