#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "cursiv/engine/grammar/GrammarImage.hpp"
#include "cursiv/engine/grammar/reached.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/grammar/InGrammar.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"

// Each expression type is a constant `ImageDesc`, each rule an
// `ImageRuleDesc`, interned by `GrammarImage`. `V` is the grammar whose
// pipeline resolves the rules, `void` for an expression read as written.

namespace cursiv::engine::gen {

// The image is read off the types of the grammar DSL, named as core names
// them.
using namespace cursiv::detail;

template <class V, class D>
inline constexpr const void* kRowOf = nullptr;
template <class V, class D>
inline constexpr const ImageStageDesc* kStageOf = nullptr;

// A value type is named at its fixed width: `long` is 64 bits on one ABI and 32
// on another.
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
  } else if constexpr (std::is_same_v<T, float>)
    return "float32";
  else if constexpr (std::is_same_v<T, double>)
    return "float64";
  else
    return type_name<T>();
}

template <auto Sp>
consteval std::string_view narrow_name() {
  if constexpr (std::is_void_v<spec_narrow_t<Sp>>)
    return {};
  else
    return type_name<spec_narrow_t<Sp>>();
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
inline constexpr const ImageDesc* kChildren[] = {&kNode<V, Es>...};

// A rule refers to its body and the body to the rule: the rule is made by a
// class defined after both, and only its address is taken in between.
template <class V, class D>
struct RuleMaker;
template <class V, class D>
inline constexpr ImageRuleDesc kRule = RuleMaker<V, D>::make();

template <class V, class L>
struct RulesOf;
template <class V, class... Rs>
struct RulesOf<V, TypeList<Rs...>> {
  static constexpr std::array<const ImageRuleDesc*, sizeof...(Rs)> rules{
      &kRule<V, declared_of_t<Rs>>...};
};
template <class V, class I, class H>
inline constexpr ImageTrivia kTrivia{RulesOf<V, I>::rules, RulesOf<V, H>::rules};

template <class V>
inline constexpr ImageDesc kNode<V, Epsilon> =
    ImageDesc{.kind = ImageKind::epsilon};
template <class V, auto F>
inline constexpr ImageDesc kNode<V, Scan<F>> = [] {
  ImageDesc d{.kind = ImageKind::scan,
              .text = std::is_pointer_v<decltype(F)> ? last_component(raw_member_name<F>()) : "callable"};
  if constexpr (!std::is_void_v<V>) {
    using Dependencies = reached_all_t<typename V::stage>;
    constexpr auto index = detail::index_of<Scan<F>, Dependencies>::value;
    static_assert(index < tl_size<Dependencies>::value, "unreachable lexical scanner");
    d.m = index;
  }
  return d;
}();
template <class V>
inline constexpr ImageDesc kNode<V, Byte> = ImageDesc{.kind = ImageKind::byte};
template <class V>
inline constexpr ImageDesc kNode<V, Eof> = ImageDesc{.kind = ImageKind::eof};
template <class V>
inline constexpr ImageDesc kNode<V, Indent> =
    ImageDesc{.kind = ImageKind::indent};
template <class V>
inline constexpr ImageDesc kNode<V, Dedent> =
    ImageDesc{.kind = ImageKind::dedent};

template <class V, class D>
inline constexpr ImageDesc kNode<V, SuperT<D>> =
    (static_cast<void>(sizeof(super_escaped<SuperT<D>>)), ImageDesc{});

template <class Gd>
inline constexpr bool kExpressionGuard =
    !std::is_void_v<Gd> && !std::is_same_v<Gd, WordBound>;
template <class V, class Gd>
consteval const ImageDesc* const* guard_children() {
  if constexpr (kExpressionGuard<Gd>)
    return kChildren<V, Gd>;
  else
    return nullptr;
}
template <class V, str S, bool CI, class Gd, bool Keyword>
inline constexpr ImageDesc kNode<V, Lit<S, CI, Gd, Keyword>> =
    ImageDesc{.kind = ImageKind::lit,
              .flags = static_cast<std::uint8_t>(
                  (CI ? kLitFolded : 0) | (Keyword ? kLitKeyword : 0) |
                  (std::is_same_v<Gd, WordBound> ? kLitGuardWord : 0) |
                  (kExpressionGuard<Gd> ? kLitGuardExpr : 0)),
              .count = kExpressionGuard<Gd> ? 1u : 0u,
              .children = guard_children<V, Gd>(),
              .text = S.view()};

template <class V, std::uint64_t M, bool N, str S>
inline constexpr ImageDesc kNode<V, CpSet<M, N, S>> =
    ImageDesc{.kind = ImageKind::cpset,
              .flags = static_cast<std::uint8_t>(N ? kCpNot : 0),
              .m = M,
              .n = 0,
              .text = S.view()};

template <class V, class E, str T>
inline constexpr ImageDesc kNode<V, Doc<E, T>> =
    ImageDesc{.kind = ImageKind::doc,
              .count = 1,
              .children = kChildren<V, E>,
              .text = T.view()};

template <class V, class E>
inline constexpr ImageDesc kNode<V, GapHas<E>> = ImageDesc{
    .kind = ImageKind::gapHas,
    .flags = static_cast<std::uint8_t>(std::is_same_v<E, Eol> ? kGapLine : 0),
    .count = 1,
    .children = kChildren<V, E>};
template <class V, class E>
inline constexpr ImageDesc kNode<V, GapIs<E>> = ImageDesc{
    .kind = ImageKind::gapIs, .count = 1, .children = kChildren<V, E>};
template <class V, class T, class C>
inline constexpr ImageDesc kNode<V, Implied<T, C>> = ImageDesc{
    .kind = ImageKind::implied, .count = 2, .children = kChildren<V, T, C>};
template <class V, class O, class Cl>
inline constexpr ImageDesc kNode<V, Through<O, Cl>> = ImageDesc{
    .kind = ImageKind::through, .count = 2, .children = kChildren<V, O, Cl>};

template <class V, class Tag, class E>
inline constexpr ImageDesc kNode<V, Mark<Tag, E>> =
    ImageDesc{.kind = ImageKind::mark,
              .count = 1,
              .children = kChildren<V, E>,
              .text = kTypeNameView<Tag>};
template <class V, class Tag>
inline constexpr ImageDesc kNode<V, Same<Tag>> =
    ImageDesc{.kind = ImageKind::same, .text = kTypeNameView<Tag>};

template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Seq<Es...>> =
    ImageDesc{.kind = ImageKind::seq,
              .count = sizeof...(Es),
              .children = kChildren<V, Es...>};
template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Choice<Es...>> =
    ImageDesc{.kind = ImageKind::choice,
              .count = sizeof...(Es),
              .children = kChildren<V, Es...>};
template <class V, class... Es>
inline constexpr ImageDesc kNode<V, Unordered<Es...>> =
    ImageDesc{.kind = ImageKind::unordered,
              .count = sizeof...(Es),
              .children = kChildren<V, Es...>};

template <class V, std::size_t Mn, std::size_t Mx, class E>
inline constexpr ImageDesc kNode<V, Repeat<Mn, Mx, E>> =
    ImageDesc{.kind = ImageKind::repeat,
              .count = 1,
              .children = kChildren<V, E>,
              .m = Mn,
              .n = Mx};

template <class V, class E>
inline constexpr ImageDesc kNode<V, Not<E>> = ImageDesc{
    .kind = ImageKind::notOf, .count = 1, .children = kChildren<V, E>};
template <class V, class E>
inline constexpr ImageDesc kNode<V, And<E>> = ImageDesc{
    .kind = ImageKind::andOf, .count = 1, .children = kChildren<V, E>};
template <class V, class E>
inline constexpr ImageDesc kNode<V, Trigger<E>> = ImageDesc{
    .kind = ImageKind::trigger, .count = 1, .children = kChildren<V, E>};
template <class V, class P, class B>
inline constexpr ImageDesc kNode<V, Commit<P, B>> = ImageDesc{
    .kind = ImageKind::commit, .count = 2, .children = kChildren<V, P, B>};
template <class V, class Gr, class E>
inline constexpr ImageDesc kNode<V, Embed<Gr, E>> =
    ImageDesc{.kind = ImageKind::embed,
              .count = 1,
              .children = kChildren<V, E>,
              .rule = &kRule<V, Gr>};

template <class V, class R>
inline constexpr ImageDesc kNode<V, Ref<R>> =
    ImageDesc{.kind = ImageKind::ref, .rule = &kRule<V, declared_of_t<R>>};

template <class V, class T, class E>
inline constexpr ImageDesc kNode<V, Create<T, E>> =
    ImageDesc{.kind = ImageKind::create,
              .count = 1,
              .children = kChildren<V, E>,
              .text = type_name<T>(),
              .row = kRowOf<V, Create<T, E>>};

template <class V, class T, auto P, class E>
inline constexpr ImageDesc kNode<V, Nest<T, P, E>> =
    ImageDesc{.kind = ImageKind::nest,
              .count = 1,
              .children = kChildren<V, E>,
              .text = type_name<T>(),
              .text2 = member_identifier<P>(),
              .row = kRowOf<V, Nest<T, P, E>>};

template <class V, auto Sp, class E>
inline constexpr ImageDesc kNode<V, Assign<Sp, E>> =
    ImageDesc{.kind = ImageKind::assign,
              .count = 1,
              .children = kChildren<V, E>,
              .text = member_identifier<spec_ptr<Sp>>(),
              .text2 = narrow_name<Sp>(),
              .row = kRowOf<V, Assign<Sp, E>>};
template <class V, auto Sp, class E>
inline constexpr ImageDesc kNode<V, Append<Sp, E>> =
    ImageDesc{.kind = ImageKind::append,
              .count = 1,
              .children = kChildren<V, E>,
              .text = member_identifier<spec_ptr<Sp>>(),
              .text2 = narrow_name<Sp>(),
              .row = kRowOf<V, Append<Sp, E>>};
template <class V, auto P, class E>
inline constexpr ImageDesc kNode<V, Flag<P, E>> =
    ImageDesc{.kind = ImageKind::flag,
              .count = 1,
              .children = kChildren<V, E>,
              .text = member_identifier<P>(),
              .row = kRowOf<V, Flag<P, E>>};

template <class V, class... Is, class... Hs, class E>
inline constexpr ImageDesc kNode<V, Skip<TypeList<Is...>, TypeList<Hs...>, E>> =
    ImageDesc{.kind = ImageKind::skip,
              .count = 1,
              .children = kChildren<V, E>,
              .trivia = &kTrivia<V, TypeList<Is...>, TypeList<Hs...>>};

template <class C>
struct class_of_bound;
template <std::uint64_t M, bool N, str S>
struct class_of_bound<CpSet<M, N, S>> {
  static constexpr ImageClassDesc value{M, N, S.view()};
};

// What a section of grammar `G` reads: its entry, and its trivia.
template <class V, class G>
consteval ImageGrammarDesc section_of() {
  using Ig = section_ignored_t<G>;
  using Hd = section_hidden_t<G>;
  ImageGrammarDesc g;
  g.entry = &kRule<V, declared_of_t<section_entry_t<G>>>;
  g.trivia = kTrivia<V, Ig, Hd>;
  return g;
}
template <class V, class G>
inline constexpr ImageGrammarDesc kSection = section_of<V, G>();

template <class V, class D>
struct RuleMaker {
  static consteval ImageRuleDesc make() {
    ImageRuleDesc r;
    r.name = type_name<D>();
    r.qualified = raw_type_name<D>();
    r.anonymous = is_anonymous_rule<D>;
    if constexpr (!std::is_void_v<typename owner_grammar_of<D>::type>)
      r.ownerGrammar = &kRule<V, typename owner_grammar_of<D>::type>;
    r.stage = kStageOf<V, D>;
    if constexpr (is_grammar<D>) {
      r.caseInsensitive = ResolveOverride<D>::case_insensitive;
      r.section = &kSection<V, D>;
    }
    return r;
  }
};

template <class V>
consteval ImageGrammarDesc grammar_of() {
  using G = typename V::grammar;
  using Ig = typename ResolveOverride<G>::ignored;
  using Hd = typename ResolveOverride<G>::hidden;
  ImageGrammarDesc g;
  g.name = type_name<G>();
  g.entry = &kRule<V, declared_of_t<typename G::entry>>;
  g.trivia = kTrivia<V, Ig, Hd>;
  g.bound = class_of_bound<
      std::remove_cvref_t<decltype(ResolveOverride<G>::word_boundary)>>::value;
  g.plain =
      class_of_bound<std::remove_cvref_t<decltype(GrammarSpec::w)>>::value;
  g.caseInsensitive = ResolveOverride<G>::case_insensitive;
  using Open = std::remove_cvref_t<decltype(G::indent_open)>;
  using Block = std::remove_cvref_t<decltype(G::indent_block)>;
  using Close = std::remove_cvref_t<decltype(G::indent_close)>;
  if constexpr (!std::is_same_v<Open, Epsilon>) g.indentOpen = &kNode<V, Open>;
  if constexpr (!std::is_same_v<Block, Epsilon>) g.indentBlock = &kNode<V, Block>;
  if constexpr (!std::is_same_v<Close, Epsilon>)
    g.indentClose = &kNode<V, Close>;
  if constexpr (!std::is_void_v<typename G::indent_newline>)
    g.indentNewline = &kRule<V, declared_of_t<typename G::indent_newline>>;
  using NewlineIn = std::remove_cvref_t<decltype(G::indent_newline_in)>;
  if constexpr (!std::is_same_v<NewlineIn, Epsilon>) g.indentNewlineIn = &kNode<V, NewlineIn>;
  return g;
}

template <class V>
inline constexpr ImageGrammarDesc kGrammar = grammar_of<V>();

// A new image of the grammar, owned by the caller: an image fills itself as it
// is read, so no two callers share one.
template <class V>
GrammarImage grammar_image() {
  GrammarImage image;
  image.open(kGrammar<V>);
  return image;
}

}  // namespace cursiv::engine::gen
