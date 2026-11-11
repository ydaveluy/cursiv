// The directives of one compiled grammar, applied. It refuses what it does not
// model: a `Parts` converter, a payload with several sources, a member no
// `put` expresses.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cursiv/DeclaredModel.hpp"
#include "cursiv/GrammarActions.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/shared/gen/GrammarBindings.hpp"
#include "cursiv/shared/gen/Row.hpp"
#include "cursiv/shared/runtime/ast_conv.hpp"
#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/VariantNodeAlt.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::fuzzer {

namespace detail {

template <class T>
[[nodiscard]] inline bool node_is(const AstNode* n) {
  return n != nullptr && n->type != nullptr &&
         n->type->derivesFrom(&ast_type_tag<T>);
}

// Rendered as text, so both trees compare the same way and a difference is
// readable. `%.17g` round-trips a double, so equal doubles print equal.

template <class X>
struct Render {
  static bool run(const X& v, std::string& out) {
    if constexpr (std::is_same_v<X, std::string_view> ||
                  std::is_same_v<X, std::string>) {
      out.assign(v);
      return true;
    } else if constexpr (std::is_same_v<X, bool>) {
      out = v ? "true" : "false";
      return true;
    } else if constexpr (std::is_same_v<X, std::monostate>) {
      out = "-";
      return true;
    } else if constexpr (std::is_enum_v<X>) {
      out = std::to_string(static_cast<long long>(v));
      return true;
    } else if constexpr (std::is_floating_point_v<X>) {
      char buf[40];
      const int n =
          std::snprintf(buf, sizeof buf, "%.17g", static_cast<double>(v));
      out.assign(buf, n > 0 ? static_cast<std::size_t>(n) : 0u);
      return true;
    } else if constexpr (std::is_integral_v<X>) {
      out = std::to_string(v);
      return true;
    } else {
      return false;
    }
  }
};

template <class... As>
struct Render<std::variant<As...>> {
  static bool run(const std::variant<As...>& v, std::string& out) {
    out = "#" + std::to_string(v.index()) + ":";
    bool ok = true;
    std::visit(
        [&](const auto& alt) {
          using A = std::remove_cvref_t<decltype(alt)>;
          if constexpr (cursiv::detail::ast_pointer_alt<A>) {
            // A node in a variant: not read here, compared as a child.
            ok = false;
          } else {
            std::string s;
            ok = Render<A>::run(alt, s);
            out += s;
          }
        },
        v);
    return ok;
  }
};

template <class L>
struct only_one {
  using type = void;
};
template <class T>
struct only_one<TypeList<T>> {
  using type = T;
};

// The single alternative of a variant that holds a node.
template <class Held>
using node_alt_t = typename only_one<
    typename cursiv::detail::variant_node_alt<Held>::type>::type;

// The engine's own write, where the payload names an alternative or needs none.
template <class F, class A, class V>
Write store(typename F::owner& o, V&& v) {
  if constexpr (cursiv::detail::is_variant_v<typename F::held> &&
                std::is_void_v<A> &&
                !std::is_same_v<std::remove_cvref_t<V>, typename F::held> &&
                !std::is_same_v<std::remove_cvref_t<V>, typename F::member>) {
    return Write::not_modelled;
  } else {
    cursiv::detail::place<A>(o.*F::ptr, std::forward<V>(v));
    return Write::modelled;
  }
}

template <class E>
struct made_type {
  using type = void;
};
template <class T, class E>
struct made_type<Create<T, E>> {
  using type = T;
};
template <class T, auto P, class E>
struct made_type<Nest<T, P, E>> {
  using type = T;
};

// The member an assignment names, and the payload it writes.
template <class E>
struct directive {
  static constexpr bool is_one = false;
};
template <auto Sp, class E>
struct directive<Assign<Sp, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = cursiv::detail::spec_ptr<Sp>;
  using payload = E;
};
template <auto Sp, class E>
struct directive<Append<Sp, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = cursiv::detail::spec_ptr<Sp>;
  using payload = E;
};
template <auto P, class E>
struct directive<Flag<P, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = true;
  static constexpr auto ptr = P;
  using payload = E;
};
template <class T, auto P, class E>
struct directive<Nest<T, P, E>> {
  static constexpr bool is_one = true;
  static constexpr bool enable = false;
  static constexpr auto ptr = P;
  using payload = E;
};

using DenoteFn = std::optional<std::string> (*)(std::string_view);
using MakeFn = AstNode* (*)(WalkTree&);
using NodeFn = Write (*)(AstNode&, AstNode*);
using TextFn = Write (*)(AstNode&, ElementId, std::string_view);

template <class T>
AstNode* make_node(WalkTree& ar) {
  return ar.template make<T>();
}

// What a leaf's converter answers, asked as the engine asks: its parts, of
// which a token has one, then its text, or nothing.
template <class R>
auto convert_leaf([[maybe_unused]] std::string_view raw) {
  using Cv = cursiv::detail::conv_t<R>;
  if constexpr (cursiv::detail::ConvertsParts<Cv>)
    return Cv::convert(Parts{&raw, 1});
  else if constexpr (cursiv::detail::ConvertsText<Cv>)
    return Cv::convert(raw);
  else
    return Cv::convert();
}

template <class R>
std::optional<std::string> denote_of(std::string_view raw) {
  auto result = convert_leaf<R>(raw);
  if constexpr (cursiv::engine::is_conversion_result<decltype(result)>) {
    if (!result) return std::nullopt;
    return std::string(*result.value);
  } else {
    return std::string(result);
  }
}

// A converter that answers text is what a leaf denotes, as the engine reads it:
// a view or a string. Anything else leaves the raw text standing.
template <class E>
consteval DenoteFn denote_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; })
    if constexpr (is_terminal_rule<T> || cursiv::detail::is_data_rule<T>)
      if constexpr (cursiv::detail::HasConverter<cursiv::detail::conv_t<T>>)
        if constexpr (cursiv::engine::is_text_value<
                          cursiv::detail::unwrap_result_t<
                              decltype(convert_leaf<T>(std::string_view{}))>>)
          return &denote_of<T>;
  return nullptr;
}

template <class E>
Write write_node_of(AstNode& owner, AstNode* child) {
  using D = directive<E>;
  using F = cursiv::detail::field_tag<D::ptr>;
  if (!node_is<typename F::owner>(&owner)) return Write::not_modelled;
  auto& o = *cursiv::detail::ast_cast_unchecked<typename F::owner>(&owner);
  using Held = typename F::held;
  if constexpr (F::content == cursiv::detail::FieldContent::node) {
    using T = std::remove_pointer_t<Held>;
    if (!node_is<T>(child)) return Write::not_modelled;
    return store<F, Held>(o, cursiv::detail::ast_cast_unchecked<T>(child));
  } else if constexpr (F::content == cursiv::detail::FieldContent::variant) {
    using A = node_alt_t<Held>;
    if constexpr (std::is_void_v<A>) {
      return Write::not_modelled;
    } else {
      using T = std::remove_pointer_t<A>;
      if (!node_is<T>(child)) return Write::not_modelled;
      return store<F, A>(o, cursiv::detail::ast_cast_unchecked<T>(child));
    }
  } else {
    return Write::not_modelled;
  }
}

// The converter's answer. A failed positioned result is still modelled: the
// engine leaves the member at its default, as here.
template <class F, class A, class Res>
Write accept(typename F::owner& o, Res&& r) {
  using R = std::remove_cvref_t<Res>;
  if constexpr (cursiv::engine::is_conversion_result<R>) {
    if (!r) return Write::modelled;
    return store<F, A>(o, std::move(*r.value));
  } else {
    return store<F, A>(o, std::forward<Res>(r));
  }
}

// The row a payload rule is named by; `kNoElement` for a bare matcher, which
// is the fallback tried when no rule of the choice matched.
template <class C, class R>
consteval ElementId payload_row() {
  if constexpr (std::is_void_v<R>)
    return kNoElement;
  else
    return model::call_row<C, R>;
}

// The rule a payload resolves to: an override's derived rule, or a bare
// matcher itself.
template <class R>
concept declares_kind = requires { typename R::kind; };
template <class C, class R, bool = declares_kind<R>>
struct payload_rule {
  using type = R;
};
template <class C, class R>
struct payload_rule<C, R, true> {
  using resolved = typename cursiv::Normalize<C>::template rule<R>;
  using type = std::conditional_t<std::is_void_v<resolved>, R, resolved>;
};

// One source of one directive: the value is what the rule produces, and
// the member takes it through its own alternative when it is a variant.
template <class C, class F, class S>
Write one_text([[maybe_unused]] typename F::owner& o,
               [[maybe_unused]] ElementId payload,
               [[maybe_unused]] bool fallback,
               [[maybe_unused]] std::string_view text) {
  if constexpr (!cursiv::detail::is_text_payload<S>) {
    return Write::not_modelled;
  } else {
    using R = typename S::rule;
    constexpr ElementId kRow = payload_row<C, R>();
    if (fallback ? kRow != kNoElement : kRow != payload)
      return Write::not_modelled;
    // The name picks the source; the resolved rule converts it.
    using RR = typename payload_rule<C, R>::type;
    using A = typename cursiv::engine::conv_alt<
        typename F::held, cursiv::detail::text_payload<RR>>::type;
    using CR = cursiv::detail::converter_rule_t<RR>;
    if constexpr (!std::is_void_v<CR>) {
      if constexpr (cursiv::detail::ConvertsText<CR>) {
        return accept<F, A>(o, CR::convert(text));
      } else {
        // A converter reading `Parts`, or a value a data rule passes on, is
        // not modelled.
        return Write::not_modelled;
      }
    } else if constexpr (std::is_void_v<A> ||
                         !cursiv::detail::has_default_convert<A>) {
      return Write::not_modelled;
    } else {
      return accept<F, A>(o, default_convert<A>(text));
    }
  }
}

template <class C, class F, class L>
struct text_sources;
template <class C, class F, class... Ss>
struct text_sources<C, F, TypeList<Ss...>> {
  static Write run([[maybe_unused]] typename F::owner& o,
                   [[maybe_unused]] ElementId payload,
                   [[maybe_unused]] std::string_view text) {
    bool done = false;
    // The rule that spelled it first, the bare matcher only if none did.
    ((done = done ||
             one_text<C, F, Ss>(o, payload, false, text) == Write::modelled),
     ...);
    ((done = done ||
             one_text<C, F, Ss>(o, payload, true, text) == Write::modelled),
     ...);
    return done ? Write::modelled : Write::not_modelled;
  }
};

template <class C, class E>
Write write_text_of(AstNode& owner, [[maybe_unused]] ElementId payload,
                    [[maybe_unused]] std::string_view text) {
  using D = directive<E>;
  using F = cursiv::detail::field_tag<D::ptr>;
  if (!node_is<typename F::owner>(&owner)) return Write::not_modelled;
  auto& o = *cursiv::detail::ast_cast_unchecked<typename F::owner>(&owner);

  if constexpr (D::enable) {
    return store<F, typename F::held>(o, true);
  } else if constexpr (cursiv::detail::is_ref_content(F::content)) {
    // A reference is judged on its text: the name is what the document
    // spells, and resolving it is the linker's verdict, not the walk's.
    typename F::held slot{};
    slot.refText = text;
    return store<F, typename F::held>(o, slot);
  } else {
    return text_sources<C, F,
                        typename cursiv::detail::payload_of<
                            typename D::payload>::type>::run(o, payload, text);
  }
}

// Whether the rule builds its own node: true when a root `create` wraps
// its body, false when its alternatives each build their own head and the
// rule just adopts what they built.
template <class E>
inline constexpr bool root_creates = false;
template <class T, class E>
inline constexpr bool root_creates<Create<T, E>> = true;
template <class C, class R>
inline constexpr bool rule_creates =
    root_creates<cursiv::detail::declared_body_t<
        typename cursiv::Normalize<C>::template rule<R>>>;

template <class C, class E>
consteval MakeFn make_entry() {
  using T = std::remove_cvref_t<E>;
  using Made = typename made_type<T>::type;
  if constexpr (!std::is_void_v<Made>) {
    return &make_node<Made>;
  } else if constexpr (requires { typename T::kind; }) {
    // A climb builds one node per fold; a rule whose alternatives build no
    // head node carries the creation, its own element making it.
    if constexpr (cursiv::detail::is_infix_rule<T>)
      return &make_node<typename T::node_type>;
    else if constexpr (cursiv::detail::is_node_rule<T> && rule_creates<C, T>)
      return &make_node<cursiv::detail::rule_type_t<T>>;
    else
      return nullptr;
  } else {
    return nullptr;
  }
}

// The operand rule's row. `Ref<R>` resolves to R's own row, which is where an
// effective rule and the name it was declared under coincide.
template <class C, class R>
consteval ElementId primary_row() {
  return model::call_row<C, typename R::primary>;
}

// Levels are declared tightest-first; the step a resolution compares is
// `N - I` for level `I` of `N`.
template <class C, class L, std::uint8_t Prec, bool Right>
struct ops_of_level;
template <class C, class... As, std::uint8_t Prec, bool Right>
struct ops_of_level<C, TypeList<As...>, Prec, Right> {
  static constexpr std::size_t size = sizeof...(As);
  static constexpr std::array<InfixOp, sizeof...(As)> value = {
      InfixOp{model::op_row<C, As>, Prec, Right}...};
};

// The operators as declared: substituting the word boundary into a `kw<>`
// guard is the lowering's reading of them, not the author's. The chains
// written are binary: a unary level offers none.
template <class Lvls, std::size_t I, class L = typename Lvls::template at<I>>
using level_alts_t = cursiv::detail::cond_t<
    cursiv::detail::is_unary_level<L>, TypeList<>,
    typename cursiv::detail::alts_list<typename L::op_expr>::type>;

template <class C, class R, class Is>
struct infix_ops;
template <class C, class R, std::size_t... Is>
struct infix_ops<C, R, std::index_sequence<Is...>> {
  using Lvls = typename R::levels;
  template <std::size_t I>
  using Level =
      ops_of_level<C, level_alts_t<Lvls, I>,
                   static_cast<std::uint8_t>(sizeof...(Is) - I),
                   Lvls::template at<I>::kGroup == Association::right>;
  static constexpr std::size_t kN = (std::size_t{0} + ... + Level<Is>::size);
  static constexpr std::array<InfixOp, kN> kOps = []() consteval {
    std::array<InfixOp, kN> out{};
    std::size_t at = 0;
    (([&] {
       for (const InfixOp& o : Level<Is>::value) out[at++] = o;
     }()),
     ...);
    return out;
  }();
};

template <class C, class R>
using infix_ops_t = infix_ops<C, R, std::make_index_sequence<R::levels::size>>;

// What an operator hands the member it writes: the constant its own
// converter names, or the text it matched, its parts joined, where it names
// none.
template <class Op>
constexpr auto op_value(std::string_view text) {
  using Rule = typename model::op_position<Op>::type;
  if constexpr (requires { typename Rule::slot; }) {
    using Conv = cursiv::detail::conv_t<Rule>;
    if constexpr (cursiv::detail::ConvertsNothing<Conv>)
      return Conv::convert();
    else if constexpr (cursiv::detail::ConvertsText<Conv>)
      return Conv::convert(text);
    else
      return text;
  } else {
    return text;
  }
}

// The operators of every level, flattened: the fold below asks each in turn
// which one the generator chose.
template <class Lvls, class Is>
struct level_alts_all;
template <class Lvls, std::size_t... Is>
struct level_alts_all<Lvls, std::index_sequence<Is...>>
    : cursiv::detail::tl_cat<level_alts_t<Lvls, Is>...> {};

// The three writes a fold commits, spelled from the rule's own pointers. The
// operator is named by its row, not by its text: two spellings may denote one
// constant, and a constant is what the member holds.
template <class C, class R, class N, class... Ops>
Write write_op(ElementId chosen, N& n, std::string_view text,
               TypeList<Ops...>) {
  bool done = false;
  (((!done && model::op_row<C, Ops> == chosen)
        ? (n.*R::op_pointer = op_value<Ops>(text), done = true)
        : false),
   ...);
  return done ? Write::modelled : Write::not_modelled;
}

template <class C, class R>
Write fold_of(ElementId op, AstNode& node, AstNode* lhs, std::string_view text,
              AstNode* rhs) {
  using N = typename R::node_type;
  using LT = std::remove_pointer_t<typename member_traits<
      std::remove_cv_t<decltype(R::left_pointer)>>::type>;
  using RT = std::remove_pointer_t<typename member_traits<
      std::remove_cv_t<decltype(R::right_pointer)>>::type>;
  if (!node_is<N>(&node) || !node_is<LT>(lhs) || !node_is<RT>(rhs))
    return Write::not_modelled;
  N& n = *cursiv::detail::ast_cast_unchecked<N>(&node);
  n.*R::left_pointer = cursiv::detail::ast_cast_unchecked<LT>(lhs);
  using Alts = typename level_alts_all<
      typename R::levels,
      std::make_index_sequence<R::levels::size>>::type;
  if (write_op<C, R>(op, n, text, Alts{}) == Write::not_modelled)
    return Write::not_modelled;
  n.*R::right_pointer = cursiv::detail::ast_cast_unchecked<RT>(rhs);
  return Write::modelled;
}

using FoldFn = Write (*)(ElementId, AstNode&, AstNode*, std::string_view,
                         AstNode*);

template <class C, class E>
consteval ElementId primary_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (cursiv::detail::is_infix_rule<T>) return primary_row<C, T>();
  }
  return kNoElement;
}

template <class C, class E>
consteval std::span<const InfixOp> ops_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (cursiv::detail::is_infix_rule<T>)
      return std::span<const InfixOp>(infix_ops_t<C, T>::kOps);
  }
  return {};
}

template <class C, class E>
consteval FoldFn fold_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (requires { typename T::kind; }) {
    if constexpr (cursiv::detail::is_infix_rule<T>) return &fold_of<C, T>;
  }
  return nullptr;
}

template <class E>
consteval NodeFn node_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (directive<T>::is_one) return &write_node_of<T>;
  return nullptr;
}

template <class C, class E>
consteval TextFn text_entry() {
  using T = std::remove_cvref_t<E>;
  if constexpr (directive<T>::is_one) return &write_text_of<C, T>;
  return nullptr;
}

// One row per position; an element past them has none.
template <class C, class L>
struct act_rows;
template <class C, class... Es>
struct act_rows<C, TypeList<Es...>> {
  static constexpr std::size_t kN = sizeof...(Es);
  static constexpr std::array<MakeFn, kN> kMake = {make_entry<C, Es>()...};
  static constexpr std::array<NodeFn, kN> kNode = {node_entry<Es>()...};
  static constexpr std::array<TextFn, kN> kText = {text_entry<C, Es>()...};
  static constexpr std::array<ElementId, kN> kPrimary = {
      primary_entry<C, Es>()...};
  static constexpr std::array<std::span<const InfixOp>, kN> kOps = {
      ops_entry<C, Es>()...};
  static constexpr std::array<FoldFn, kN> kFold = {fold_entry<C, Es>()...};
  static constexpr std::array<DenoteFn, kN> kDenote = {denote_entry<Es>()...};
};

// The members a grammar writes, read off the engine's rows.
template <class R>
struct written_field {
  using type = TypeList<>;
};
template <engine::WriteKind K, class T, auto P, class N, class Src>
struct written_field<engine::Row<K, T, engine::Wrote<P>, N, Src>> {
  using type = TypeList<cursiv::detail::field_tag<P>>;
};

template <class L>
struct written_fields;
template <class... Rs>
struct written_fields<TypeList<Rs...>>
    : cursiv::detail::dedupe<
          cursiv::detail::tl_cat_t<typename written_field<Rs>::type...>> {};

using ValueYield = cursiv::function_ref<void(std::string_view, std::uint32_t,
                                             ValueKind, std::string_view)>;

template <class F, class X>
void yield_one(std::string_view name, std::uint32_t index, const X& v,
               ValueYield yield) {
  if constexpr (cursiv::detail::is_ref_content(F::content)) {
    if constexpr (F::content == cursiv::detail::FieldContent::reference)
      yield(name, index, ValueKind::text, v.refText);
    else
      yield(name, index, ValueKind::unreadable, {});
  } else {
    std::string out;
    if (Render<X>::run(v, out))
      yield(name, index, ValueKind::text, out);
    else
      yield(name, index, ValueKind::unreadable, out);
  }
}

template <class F>
void yield_field(const AstNode& n, ValueYield yield) {
  if constexpr (F::content == cursiv::detail::FieldContent::node) {
    return;  // `AstReflection` walks the nodes
  } else {
    if (!node_is<typename F::owner>(&n)) return;
    const auto& o = *ast_cast<const typename F::owner>(&n);
    constexpr std::uint32_t kNone = ~std::uint32_t{0};
    if constexpr (F::storage == cursiv::detail::FieldStorage::vector) {
      std::uint32_t at = 0;
      for (const auto& v : o.*F::ptr) yield_one<F>(F::name, at++, v, yield);
    } else if constexpr (F::storage == cursiv::detail::FieldStorage::optional) {
      if ((o.*F::ptr).has_value())
        yield_one<F>(F::name, kNone, *(o.*F::ptr), yield);
      else
        yield(F::name, kNone, ValueKind::unset, {});
    } else {
      yield_one<F>(F::name, kNone, o.*F::ptr, yield);
    }
  }
}

template <class L>
struct value_walk;
template <class... Fs>
struct value_walk<TypeList<Fs...>> {
  static void run([[maybe_unused]] const AstNode& n,
                  [[maybe_unused]] ValueYield yield) {
    (yield_field<Fs>(n, yield), ...);
  }
};

}  // namespace detail

template <class C>
struct CursivActions final : GrammarActions {
  using Rows =
      detail::act_rows<C, typename detail::model::positions_for<C>::type>;
  using Fields = typename detail::written_fields<
      typename engine::GrammarBindings<C>::rows>::type;

  [[nodiscard]] AstNode* make(ElementId e, WalkTree& ar) const override {
    if (e >= Rows::kMake.size() || Rows::kMake[e] == nullptr) return nullptr;
    return Rows::kMake[e](ar);
  }

  [[nodiscard]] ElementId primary(ElementId e) const override {
    return e < Rows::kPrimary.size() ? Rows::kPrimary[e] : kNoElement;
  }

  [[nodiscard]] std::span<const InfixOp> operators(ElementId e) const override {
    return e < Rows::kOps.size() ? Rows::kOps[e] : std::span<const InfixOp>{};
  }

  Write fold(ElementId e, ElementId op, AstNode& node, AstNode* left,
             std::string_view text, AstNode* right) const override {
    if (e >= Rows::kFold.size() || Rows::kFold[e] == nullptr)
      return Write::not_modelled;
    return Rows::kFold[e](op, node, left, text, right);
  }

  Write write_node(ElementId e, AstNode& owner, AstNode* child) const override {
    if (e >= Rows::kNode.size() || Rows::kNode[e] == nullptr)
      return Write::not_modelled;
    return Rows::kNode[e](owner, child);
  }

  Write write_text(ElementId e, ElementId payload, AstNode& owner,
                   std::string_view text) const override {
    if (e >= Rows::kText.size() || Rows::kText[e] == nullptr)
      return Write::not_modelled;
    return Rows::kText[e](owner, payload, text);
  }

  [[nodiscard]] std::optional<std::string> denote(
      ElementId e, std::string_view text) const override {
    if (e >= Rows::kDenote.size() || Rows::kDenote[e] == nullptr)
      return std::string(text);
    return Rows::kDenote[e](text);
  }

  void visitValues(const AstNode& n, detail::ValueYield yield) const override {
    detail::value_walk<Fields>::run(n, yield);
  }
};

}  // namespace cursiv::fuzzer
