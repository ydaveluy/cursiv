// The published row: an action is a type (what it does, to which class,
// through which member, reading what), and its id is its position in the list
// a grammar publishes.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"

#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstMemberAccess.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"

namespace cursiv::engine {

// `N` is the candidates a reference narrows to and `Src` what the write reads:
// two directives that differ in either are two actions.
template <WriteKind K, class T, class M = void, class N = void,
          class Src = void>
struct Row {
  static constexpr WriteKind kind = K;
  using type = T;
  using field = M;
  using narrow = N;
  using source = Src;
  static constexpr bool node = [] {
    if constexpr (std::is_void_v<M> || K == WriteKind::flag) return false;
    else
      return M::content == detail::FieldContent::node ||
             (M::content == detail::FieldContent::variant && std::is_void_v<Src>);
  }();
};

// A static member's `decltype` carries its `const`, which no pointer-to-member
// trait matches.
template <auto P, class Src = void>
using owned_row =
    Row<WriteKind::assign,
        typename detail::member_traits<std::remove_cv_t<decltype(P)>>::class_type,
        detail::field_tag<P>, void, Src>;

// The payload a write converts, already erased: a rule indistinguishable
// from a bare matcher is the same action, `void` where nothing converts.
template <class St, class R>
struct src_rule {
  using type = detail::erased_src_t<detail::text_payload<typename St::template rule<R>>>;
};
template <class St>
struct src_rule<St, void> {
  using type = detail::text_payload<void>;
};
template <class St, class L>
struct text_src {
  using type = void;
};
template <class St, class R, class... Rest>
struct text_src<St, TypeList<detail::text_payload<R>, Rest...>> : src_rule<St, R> {};
template <class St, class S0, class... Rest>
struct text_src<St, TypeList<S0, Rest...>> : text_src<St, TypeList<Rest...>> {};
template <class St, class E>
using payload_src_t = typename text_src<St, typename detail::payload_of<E>::type>::type;

template <class St, auto S, WriteKind K, class E>
using spec_row_t = Row<K, detail::spec_class_t<S>, detail::field_tag<detail::spec_ptr<S>>, detail::spec_narrow_t<S>,
                       payload_src_t<St, E>>;

template <class St, class D>
struct directive_row;
template <class St, class T, class E>
struct directive_row<St, detail::Create<T, E>> {
  using type = Row<WriteKind::create, T>;
};
template <class St, class T, auto P, class E>
struct directive_row<St, detail::Nest<T, P, E>> {
  using type = Row<WriteKind::nest, T, detail::field_tag<P>>;
};
template <class St, auto Sp, class E>
struct directive_row<St, detail::Assign<Sp, E>> {
  using type = spec_row_t<St, Sp, WriteKind::assign, E>;
};
template <class St, auto Sp, class E>
struct directive_row<St, detail::Append<Sp, E>> {
  using type = spec_row_t<St, Sp, WriteKind::append, E>;
};
template <class St, auto P, class E>
struct directive_row<St, detail::Flag<P, E>> {
  using type = Row<WriteKind::flag,
                   typename detail::member_traits<decltype(P)>::class_type, detail::field_tag<P>>;
};

template <class L>
struct TagsOf;
template <class... Ts>
struct TagsOf<TypeList<Ts...>> {
  // One more than the classes: no array holds nothing.
  static constexpr const AstTypeTag* all[sizeof...(Ts) + 1] = {&kAstTypeTag<Ts>...};
  static constexpr std::size_t count = sizeof...(Ts);
};

// A data member pointer holds the member's offset in its class.
template <auto P>
std::ptrdiff_t member_offset() {
  constexpr auto member = P;
  static_assert(sizeof member == sizeof(std::ptrdiff_t),
                "this ABI's data member pointer is not an offset");
  std::ptrdiff_t offset = 0;
  std::memcpy(&offset, &member, sizeof offset);
  return offset;
}

template <auto P>
inline constexpr MemberDesc kMember{
    detail::field_tag<P>::name,
    detail::field_tag<P>::content,
    detail::field_tag<P>::storage,
    &kAstTypeTag<typename detail::field_tag<P>::owner>,
    {TagsOf<typename detail::held_targets<typename detail::field_tag<P>::held>::type>::all,
     TagsOf<typename detail::held_targets<typename detail::field_tag<P>::held>::type>::count},
    &member_offset<P>};

// What the converter of `Rule` gives: nothing where the rule has none, text,
// a value of another type, or the value of what the rule reads.
template <class Rule>
consteval Converts converts() {
  using Cv = detail::converter_rule_t<Rule>;
  if constexpr (std::is_void_v<Cv> ||
                (std::is_base_of_v<detail::BuiltInConverter, Cv> &&
                 (std::is_same_v<Cv, default_converter<std::string_view>> ||
                  std::is_same_v<Cv, default_converter<std::string>>))) {
    return Converts::nothing;
  } else if constexpr (detail::is_pass_conv<Cv>) {
    return Converts::pass;
  } else {
    using V = detail::rule_type_t<Rule>;
    return std::is_same_v<V, std::string_view> || std::is_same_v<V, std::string>
               ? Converts::text
               : Converts::value;
  }
}

// A row as data: what the generator reads of it.
template <class R>
consteval RowDesc make_row() {
  RowDesc d;
  d.kind = R::kind;
  if constexpr (R::kind != WriteKind::convert) {
    d.name = detail::type_name<typename R::type>();
    d.type = &kAstTypeTag<typename R::type>;
  } else {
    // The rule an `as<X>(e)` stands for has no name of its own.
    if constexpr (!detail::is_anonymous_rule<typename R::type> ||
                  detail::slot_empty<typename R::type>)
      d.name = detail::type_name<typename R::type>();
    d.terminal = cursiv::kIsTerminalRule<typename R::type>;
    d.dataRule = detail::is_data_rule<typename R::type>;
    d.converts = converts<typename R::source>();
  }
  using F = typename R::field;
  if constexpr (!std::is_void_v<F>) {
    d.member = &kMember<F::ptr>;
    d.node = R::node;
    if constexpr (!std::is_void_v<typename R::narrow>)
      d.narrow = &kAstTypeTag<typename R::narrow>;
  }
  return d;
}
template <class R>
inline constexpr RowDesc kRow = make_row<R>();

}  // namespace cursiv::engine
