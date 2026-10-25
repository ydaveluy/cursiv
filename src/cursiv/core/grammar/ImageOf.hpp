#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/InGrammar.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/Tag.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/core/utils/typelist.hpp"

// Each expression type is a constant `ImageDesc`, each rule an `ImageRuleDesc`,
// and nothing here is code: `GrammarImage` interns them when something reads
// the grammar. `V` is the grammar whose pipeline resolves the rules, `void`
// for an expression read as written.

namespace cursiv::detail {

// A reading of the grammar: the grammar itself, `void` for an expression read
// as written, or a context deriving from `ViewBase` that names its grammar and
// what an engine adds to every directive and rule.
struct ViewBase {};

template <class V>
struct view_grammar {
  using type = V;
};
template <class V>
  requires std::is_base_of_v<ViewBase, V>
struct view_grammar<V> {
  using type = typename V::grammar;
};
template <class V>
using view_grammar_t = typename view_grammar<V>::type;

template <class V, class D>
inline constexpr const void* kRowOf = nullptr;
template <class V, class D>
inline constexpr const ImageStageDesc* kStageOf = nullptr;

template <auto P>
inline constexpr std::string_view kMemberNameView =
    last_component(raw_member_name<P>());
template <auto P>
constexpr std::string_view member_name() {
  return name_store<kMemberNameView<P>>::view;
}

// A value type prints at its fixed width: `long` is 64 bits on one ABI and 32
// on another, and a dump naming it would differ per target.
template <class T>
constexpr std::string_view dump_type_name() {
  if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, char>)
    return type_name<T>();
  else if constexpr (std::is_integral_v<T>) {
    constexpr bool s = std::is_signed_v<T>;
    if constexpr (sizeof(T) == 1)
      return s ? "int8" : "uint8";
    else if constexpr (sizeof(T) == 2)
      return s ? "int16" : "uint16";
    else if constexpr (sizeof(T) == 4)
      return s ? "int32" : "uint32";
    else
      return s ? "int64" : "uint64";
  } else if constexpr (std::is_floating_point_v<T>)
    return sizeof(T) == 4 ? "float32" : "float64";
  else
    return type_name<T>();
}

template <class E>
consteval ImageDesc no_image() {
  static_assert(dependent_false<E>,
                "this combinator has no image: give it a `kNode` before any "
                "reader can see it");
  return {};
}

template <class V, class E>
inline constexpr ImageDesc kNode = no_image<E>();

template <class V, class... Es>
inline constexpr const ImageDesc* kKids[] = {&kNode<V, Es>...};

// A rule refers to its body and the body to the rule: the rule is made by a
// class defined after both, and only its address is taken in between.
template <class V, class D>
struct RuleMaker;
template <class V, class D>
inline constexpr ImageRuleDesc kRule = RuleMaker<V, D>::make();

template <class V, class L>
struct RulesOf;
template <class V>
struct RulesOf<V, TypeList<>> {
  static constexpr const ImageRuleDesc* const* kAll = nullptr;
};
template <class V, class... Rs>
  requires(sizeof...(Rs) != 0)
struct RulesOf<V, TypeList<Rs...>> {
  static constexpr const ImageRuleDesc* kStore[] = {
      &kRule<V, declared_of_t<Rs>>...};
  static constexpr const ImageRuleDesc* const* kAll = kStore;
};

// A node is built by chaining, not by designated initializers: g++ fails on
// those in a variable template's initializer.
struct Draft {
  ImageDesc d{};

  consteval explicit Draft(ImageKind kind) { d.kind = kind; }
  consteval Draft flags(std::uint8_t f) const {
    Draft c = *this;
    c.d.flags = f;
    return c;
  }
  consteval Draft kids(std::uint32_t count, const ImageDesc* const* k) const {
    Draft c = *this;
    c.d.count = count;
    c.d.kids = k;
    return c;
  }
  consteval Draft rule(const ImageRuleDesc* r) const {
    Draft c = *this;
    c.d.rule = r;
    return c;
  }
  consteval Draft rules(const ImageRuleDesc* const* r) const {
    Draft c = *this;
    c.d.rules = r;
    return c;
  }
  consteval Draft tag(const void* t) const {
    Draft c = *this;
    c.d.tag = t;
    return c;
  }
  consteval Draft row(const void* r) const {
    Draft c = *this;
    c.d.row = r;
    return c;
  }
  consteval Draft counts(std::uint64_t m, std::uint64_t n) const {
    Draft c = *this;
    c.d.m = m;
    c.d.n = n;
    return c;
  }
  consteval Draft text(std::string_view a, std::string_view b = {}) const {
    Draft c = *this;
    c.d.text = a;
    c.d.text2 = b;
    return c;
  }
  consteval operator ImageDesc() const { return d; }
};

template <class V>
inline constexpr ImageDesc kNode<V, Epsilon> = Draft(ImageKind::epsilon);
template <class V>
inline constexpr ImageDesc kNode<V, Byte> = Draft(ImageKind::byte);
template <class V>
inline constexpr ImageDesc kNode<V, Eof> = Draft(ImageKind::eof);
template <class V>
inline constexpr ImageDesc kNode<V, Indent> = Draft(ImageKind::indent);
template <class V>
inline constexpr ImageDesc kNode<V, Dedent> = Draft(ImageKind::dedent);

template <class V, class D>
inline constexpr ImageDesc kNode<V, SuperT<D>> =
    (static_cast<void>(sizeof(super_escaped<SuperT<D>>)), ImageDesc{});

template <class Gd>
inline constexpr bool kExpressionGuard =
    !std::is_void_v<Gd> && !std::is_same_v<Gd, WordBound>;
template <class V, class Gd>
consteval const ImageDesc* const* guard_kids() {
  if constexpr (kExpressionGuard<Gd>)
    return kKids<V, Gd>;
  else
    return nullptr;
}
template <class V, str S, bool CI, class Gd>
inline constexpr ImageDesc kNode<V, Lit<S, CI, Gd>> =
    Draft(ImageKind::lit)
        .flags(static_cast<std::uint8_t>(
            (CI ? kLitFolded : 0) |
            (std::is_same_v<Gd, WordBound> ? kLitGuardWord : 0) |
            (kExpressionGuard<Gd> ? kLitGuardExpr : 0)))
        .kids(kExpressionGuard<Gd> ? 1u : 0u, guard_kids<V, Gd>())
        .text(S.view());

template <class V, std::uint64_t M, bool N, str S>
inline constexpr ImageDesc kNode<V, CpSet<M, N, S>> =
    Draft(ImageKind::cpset)
        .flags(static_cast<std::uint8_t>(N ? kCpNot : 0))
        .counts(M, 0)
        .text(S.view());

template <class V, class E, str T>
inline constexpr ImageDesc kNode<V, Doc<E, T>> =
    Draft(ImageKind::doc).kids(1, kKids<V, E>).text(T.view());

template <class V, class E>
inline constexpr ImageDesc kNode<V, GapHas<E>> =
    Draft(ImageKind::gapHas)
        .flags(static_cast<std::uint8_t>(
            std::is_same_v<E, std::remove_cvref_t<decltype(eol)>> ? kGapLine
                                                                  : 0))
        .kids(1, kKids<V, E>);
template <class V, class E>
inline constexpr ImageDesc kNode<V, GapIs<E>> =
    Draft(ImageKind::gapIs).kids(1, kKids<V, E>);
template <class V, class T, class C>
inline constexpr ImageDesc kNode<V, Implied<T, C>> =
    Draft(ImageKind::implied).kids(2, kKids<V, T, C>);
template <class V, class O, class Cl>
inline constexpr ImageDesc kNode<V, Through<O, Cl>> =
    Draft(ImageKind::through).kids(2, kKids<V, O, Cl>);

template <class V, class Tag, class E>
inline constexpr ImageDesc kNode<V, Mark<Tag, E>> =
    Draft(ImageKind::mark)
        .kids(1, kKids<V, E>)
        .text(type_name<Tag>())
        .tag(&type_tag<Tag>);
template <class V, class Tag>
inline constexpr ImageDesc kNode<V, Same<Tag>> =
    Draft(ImageKind::same).text(type_name<Tag>()).tag(&type_tag<Tag>);

template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Seq<Es...>> =
    Draft(ImageKind::seq).kids(sizeof...(Es), kKids<V, Es...>);
template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Choice<Es...>> =
    Draft(ImageKind::choice).kids(sizeof...(Es), kKids<V, Es...>);
template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Unordered<Es...>> =
    Draft(ImageKind::unordered).kids(sizeof...(Es), kKids<V, Es...>);

template <class V, std::size_t Mn, std::size_t Mx, class E>
inline constexpr ImageDesc kNode<V, Repeat<Mn, Mx, E>> =
    Draft(ImageKind::repeat).kids(1, kKids<V, E>).counts(Mn, Mx);

template <class V, class E>
inline constexpr ImageDesc kNode<V, Not<E>> =
    Draft(ImageKind::notOf).kids(1, kKids<V, E>);
template <class V, class E>
inline constexpr ImageDesc kNode<V, And<E>> =
    Draft(ImageKind::andOf).kids(1, kKids<V, E>);
template <class V, class E>
inline constexpr ImageDesc kNode<V, Trigger<E>> =
    Draft(ImageKind::trigger).kids(1, kKids<V, E>);
template <class V, class P, class B>
inline constexpr ImageDesc kNode<V, Commit<P, B>> =
    Draft(ImageKind::commit).kids(2, kKids<V, P, B>);
template <class V, class Gr, class E>
inline constexpr ImageDesc kNode<V, Embed<Gr, E>> =
    Draft(ImageKind::embed).kids(1, kKids<V, E>).rule(&kRule<V, Gr>);

template <class V, class R>
inline constexpr ImageDesc kNode<V, Ref<R>> =
    Draft(ImageKind::ref).rule(&kRule<V, declared_of_t<R>>);

template <class V, class T, class E>
inline constexpr ImageDesc kNode<V, Create<T, E>> =
    Draft(ImageKind::create)
        .kids(1, kKids<V, E>)
        .text(type_name<T>())
        .row(kRowOf<V, Create<T, E>>);

template <class V, class T, auto P, class E>
inline constexpr ImageDesc kNode<V, Nest<T, P, E>> =
    Draft(ImageKind::nest)
        .kids(1, kKids<V, E>)
        .text(type_name<T>(), member_name<P>())
        .row(kRowOf<V, Nest<T, P, E>>);

template <auto Sp>
consteval std::string_view narrow_name() {
  if constexpr (std::is_void_v<spec_narrow_t<Sp>>)
    return {};
  else
    return type_name<spec_narrow_t<Sp>>();
}
template <class V, auto Sp, class E>
inline constexpr ImageDesc kNode<V, Assign<Sp, E>> =
    Draft(ImageKind::assign)
        .kids(1, kKids<V, E>)
        .text(member_name<spec_ptr<Sp>>(), narrow_name<Sp>())
        .row(kRowOf<V, Assign<Sp, E>>);
template <class V, auto Sp, class E>
inline constexpr ImageDesc kNode<V, Append<Sp, E>> =
    Draft(ImageKind::append)
        .kids(1, kKids<V, E>)
        .text(member_name<spec_ptr<Sp>>(), narrow_name<Sp>())
        .row(kRowOf<V, Append<Sp, E>>);
template <class V, auto P, class E>
inline constexpr ImageDesc kNode<V, Flag<P, E>> =
    Draft(ImageKind::flag)
        .kids(1, kKids<V, E>)
        .text(member_name<P>())
        .row(kRowOf<V, Flag<P, E>>);

template <class V, class... Is, class... Hs, class E>
inline constexpr ImageDesc kNode<V, Skip<TypeList<Is...>, TypeList<Hs...>, E>> =
    Draft(ImageKind::skip)
        .kids(1, kKids<V, E>)
        .rules(RulesOf<V, TypeList<Is..., Hs...>>::kAll)
        .counts(sizeof...(Is), sizeof...(Hs));

template <class V, class Lv>
struct LevelsOf;
template <class V, class... Ls>
struct LevelsOf<V, Levels<Ls...>> {
  static constexpr ImageLevelDesc kAll[] = {
      {Ls::kGroup == Association::right ? ImageGroup::right
       : Ls::kGroup == Association::non ? ImageGroup::non
                                        : ImageGroup::left,
       &kNode<V, typename Ls::op_expr>}...};
};

template <class C>
struct class_of_bound;
template <std::uint64_t M, bool N, str S>
struct class_of_bound<CpSet<M, N, S>> {
  static constexpr ImageClassDesc value{M, N, S.view()};
};

template <class V, class K>
consteval ImageFaceDesc grammar_face() {
  using Ig = section_ignore_t<K>;
  using Hd = section_hidden_t<K>;
  ImageFaceDesc f;
  f.kind = ImageRuleKind::grammar;
  f.ignore = RulesOf<V, Ig>::kAll;
  f.ignoreCount = static_cast<std::uint32_t>(tl_size<Ig>::value);
  f.hidden = RulesOf<V, Hd>::kAll;
  f.hiddenCount = static_cast<std::uint32_t>(tl_size<Hd>::value);
  f.entry = &kRule<V, declared_of_t<section_entry_t<K>>>;
  f.bound = class_of_bound<
      std::remove_cvref_t<decltype(ResolveOverride<K>::word_boundary)>>::value;
  return f;
}

template <class V, class K>
consteval ImageFaceDesc infix_face() {
  ImageFaceDesc f;
  f.kind = ImageRuleKind::infix;
  f.returns = dump_type_name<rule_type_t<K>>();
  f.members[0] = member_name<K::left_pointer>();
  f.members[1] = member_name<K::op_pointer>();
  f.members[2] = member_name<K::right_pointer>();
  f.primary = &kRule<V, declared_of_t<typename K::primary>>;
  f.levels = LevelsOf<V, typename K::levels>::kAll;
  f.levelCount = static_cast<std::uint32_t>(K::levels::size);
  return f;
}

template <class V, class K, class Body>
consteval ImageFaceDesc plain_face() {
  ImageFaceDesc f;
  if constexpr (is_terminal_rule<K>) {
    f.kind = ImageRuleKind::terminal;
  } else if constexpr (is_data_rule<K>) {
    f.kind = ImageRuleKind::data;
  } else {
    f.kind = ImageRuleKind::node;
  }
  if constexpr (is_node_rule<K> ||
                !std::is_same_v<rule_type_t<K>, std::string_view>)
    f.returns = dump_type_name<rule_type_t<K>>();
  f.body = &kNode<V, Body>;
  return f;
}

template <class V, class D>
consteval ImageFaceDesc written_face() {
  if constexpr (is_grammar<D>)
    return grammar_face<V, D>();
  else if constexpr (is_infix_rule<D>)
    return infix_face<V, D>();
  else
    return plain_face<V, D, rule_body_t<D>>();
}

template <class V, class D>
inline constexpr ImageFaceDesc kWritten = written_face<V, D>();

template <class V, class D>
consteval ImageFaceDesc resolved_face() {
  using St = ResolveOverride<view_grammar_t<V>>;
  using K = typename St::template rule<D>;
  if constexpr (is_infix_rule<K>)
    return infix_face<V, K>();
  else
    return plain_face<V, K, typename St::template body<D>>();
}

template <class V, class D>
inline constexpr ImageFaceDesc kResolved = resolved_face<V, D>();

// The face of a rule as the pipeline resolved it; the written one where the
// two agree.
template <class V, class D>
consteval const ImageFaceDesc* resolved_of() {
  if constexpr (std::is_void_v<V> || is_grammar<D>) {
    return &kWritten<V, D>;
  } else {
    using St = ResolveOverride<view_grammar_t<V>>;
    using K = typename St::template rule<D>;
    if constexpr (is_infix_rule<K>) {
      if constexpr (std::is_same_v<K, D>)
        return &kWritten<V, D>;
      else
        return &kResolved<V, D>;
    } else if constexpr (std::is_same_v<K, D> &&
                         std::is_same_v<typename St::template body<D>,
                                        rule_body_t<D>>) {
      return &kWritten<V, D>;
    } else {
      return &kResolved<V, D>;
    }
  }
}

template <class V, class D>
struct RuleMaker {
  static consteval ImageRuleDesc make() {
    ImageRuleDesc r;
    r.name = type_name<D>();
    r.qualified = raw_type_name<D>();
    r.anonymous = is_anonymous_rule<D>;
    r.foreign = !std::is_void_v<typename home_of<D>::type>;
    r.inlineTerminal = is_inline_terminal<D>;
    r.grammar = is_grammar<D>;
    if constexpr (!std::is_void_v<typename home_of<D>::type>)
      r.home = &kRule<V, typename home_of<D>::type>;
    r.stage = kStageOf<V, D>;
    r.written = &kWritten<V, D>;
    r.resolved = resolved_of<V, D>();
    return r;
  }
};

template <class V>
consteval ImageGrammarDesc grammar_of() {
  using G = view_grammar_t<V>;
  using Ig = typename ov::as_rules<G, typename G::ignore>::type;
  using Hd = typename ov::as_rules<G, typename G::hidden>::type;
  ImageGrammarDesc g;
  g.name = type_name<G>();
  g.entry = &kRule<V, declared_of_t<typename G::entry>>;
  g.ignore = RulesOf<V, Ig>::kAll;
  g.ignoreCount = static_cast<std::uint32_t>(tl_size<Ig>::value);
  g.hidden = RulesOf<V, Hd>::kAll;
  g.hiddenCount = static_cast<std::uint32_t>(tl_size<Hd>::value);
  g.bound = class_of_bound<
      std::remove_cvref_t<decltype(ResolveOverride<G>::word_boundary)>>::value;
  g.plain = class_of_bound<std::remove_cvref_t<decltype(w)>>::value;
  return g;
}

template <class V>
inline constexpr ImageGrammarDesc kGrammar = grammar_of<V>();

}  // namespace cursiv::detail

namespace cursiv {

template <class V>
GrammarImage& grammar_image() {
  static GrammarImage image = [] {
    GrammarImage im;
    im.open(detail::kGrammar<V>);
    return im;
  }();
  return image;
}

}  // namespace cursiv
