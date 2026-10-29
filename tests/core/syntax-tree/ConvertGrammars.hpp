#pragma once

// The grammar behind the default converters and Parts, each body entered by
// its own first token, so a lane reads back the rule's own value. `inv` is a
// grammar of its own as well, which a guard lane lowers.

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string_view>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tconv {

// A repair may lose a value; it may not invent one. A data rule joins the
// visible leaves of its region, so where a repair resumes decides what the
// value is -- resuming one byte into `state` left `tate`, a name the
// document never contained, which the linker then failed to resolve.
namespace inv {
struct N : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct IDT : terminal<cr<"a-zA-Z_"> + many(w)> {};
  // A data rule, and its first element refuses the reserved word -- which is
  // what puts a resume point in the middle of `state`.
  struct Name : rule<!kw<"state"> + ref<IDT>> {};
  struct Root : rule<kw<"init"> + assign<&N::name>(ref<Name>) + kw<"end">> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace inv

// A leaf's converter composes. A data rule joins what its parts mean, so a
// terminal that strips its quotes contributes the stripped view, and the
// name above is one the document never spelled contiguously.
namespace comp {
struct N : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
constexpr std::string_view unquote(std::string_view t) {
  return t.size() >= 2 ? t.substr(1, t.size() - 2) : t;
}
}  // namespace comp

namespace materialized {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view copied;
  string_view written;
};
inline std::string_view copy_unquoted(std::string_view text,
                                      std::pmr::memory_resource* mem) {
  return cursiv::copy_into(mem, text.substr(1, text.size() - 2));
}
inline std::string_view write_unquoted(std::string_view text,
                                       std::pmr::memory_resource* mem) {
  const auto value = text.substr(1, text.size() - 2);
  const auto room = cursiv::text_room(mem, value.size());
  for (std::size_t i = 0; i < value.size(); ++i) room[i] = value[i];
  return {room.data(), room.size()};
}
}  // namespace materialized

// The value of a data rule is the language: `a . b` and `a.b` denote the
// same name, at every -O, however the rule is composed. Both ways to break
// that come from reading the tape's direct children instead of the span's
// atoms:
//
//   * one value per level -- `data<QN1> = ref<QN>` collapses at O2, so the
//     join would get QN's record rather than QN1's, and one level's trivia
//     would move out of the value: `'c /*x*/ . d'` at O0/O1, `'c.d'` at O2,
//     the same rule over the same document;
//   * a non-compositionality -- a join stripping trivia exactly one level
//     deep reads `Path = QN + "/" + QN` as `'e . f/g . h'` at every level:
//     stable, and wrong everywhere.
//
// The pin is therefore the value, not merely its stability -- cross-level
// identity alone would bless the second shape, which never diverges.
namespace dv {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  std::string_view a;  // ref<QN>    -- parts written tight
  std::string_view b;  // ref<QN1>   -- the collapsing wrapper, trivia inside
  std::string_view c;  // ref<Path>  -- data rules composed of data rules
  cursiv::reference<Doc> r;  // the same nested payload, as a link
  std::string_view quote;
};
}  // namespace dv

// Converters of a data rule's calls: the text a data rule joins is the one its
// calls convert to.
namespace calls {
inline constexpr auto none_if_empty = [](std::string_view text) {
  return text.empty() ? std::string_view("none") : text;
};
inline constexpr auto refuse_z = [](std::string_view text) {
  using Result = cursiv::ConversionResult<std::string_view>;
  return text.find('z') == std::string_view::npos ? Result::ok(text)
                                                  : Result::err("no z");
};
inline constexpr auto length = [](std::string_view text) {
  return static_cast<std::int32_t>(text.size());
};
inline constexpr auto fixed = [] { return std::string_view("fixed"); };
}  // namespace calls

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CM : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // Its converter is what the join must read; outside QN/Path, its presence
  // must not change the meaning or the direct source view of those
  // independent data rules.
  struct QUOTED
      : terminal<as<comp::unquote>(lit<"'"> + some(cr<"a-z">) + lit<"'">)> {};
  struct QN : rule<ref<ID> + some(lit<"."> + ref<ID>)> {};
  struct QN1 : rule<ref<QN>> {};
  struct Path : rule<ref<QN> + lit<"/"> + ref<QN>> {};
  struct DvRoot : rule<assign<&dv::Doc::a>(ref<QN>) + kw<";"> +
                       assign<&dv::Doc::b>(ref<QN1>) + kw<";"> +
                       assign<&dv::Doc::c>(ref<Path>) + kw<";"> +
                       assign<&dv::Doc::r>(ref<QN1>) +
                       option(lit<"!"> +
                              assign<&dv::Doc::quote>(ref<QUOTED>))> {};
  struct CompName : rule<ref<ID> + lit<"."> + ref<QUOTED>> {};
  struct CompRoot : rule<cursiv::create<comp::N> +
                         assign<&comp::N::name>(ref<CompName>)> {};
  struct COPIED : terminal<as<materialized::copy_unquoted>(
                      lit<"\""> + many(cr<"a-z">) + lit<"\"">)> {};
  struct WRITTEN : terminal<as<materialized::write_unquoted>(
                       lit<"`"> + many(cr<"a-z">) + lit<"`">)> {};
  struct MaterializedRoot
      : rule<kw<"materialized"> +
             assign<&materialized::Doc::copied>(ref<COPIED>) +
             assign<&materialized::Doc::written>(ref<WRITTEN>)> {};
  // Parts that are all present, each converting to empty text.
  struct BlankName : rule<ref<COPIED> + ref<COPIED>> {};
  struct BlankRoot
      : rule<kw<"blank"> + assign<&comp::N::name>(ref<BlankName>)> {};
  inline static constexpr auto operator_text = [](std::string_view text) {
    return text;
  };
  template <auto Expression>
  struct Operator : rule<as<operator_text>(Expression)> {};
  struct KeywordRoot
      : rule<kw<"keyword"> + assign<&comp::N::name>(ref<Operator<kw<"is">>>)> {
  };
  // A call that read nothing, in a grammar no converter takes parts in.
  struct Maybe : rule<as<calls::none_if_empty>(option(lit<"!">))> {};
  struct EmptyCallName : rule<ref<Maybe> + ref<ID>> {};
  struct EmptyCallRoot
      : rule<kw<"emptycall"> + assign<&comp::N::name>(ref<EmptyCallName>)> {};
  // A converter that reads nothing, over a record whose own converter makes no
  // text: its calls below are not its input.
  struct VETTED : terminal<as<calls::refuse_z>(lit<"!"> + some(cr<"a-z">))> {};
  struct Counted : rule<as<calls::length>(ref<VETTED> + lit<"."> + ref<ID>)> {};
  struct Fixed : rule<as<calls::fixed>(ref<Counted>)> {};
  struct FixedName : rule<ref<Fixed> + lit<"/"> + ref<ID>> {};
  struct FixedRoot
      : rule<kw<"fixed"> + assign<&comp::N::name>(ref<FixedName>)> {};
  struct Entry : rule<ref<DvRoot> | ref<CompRoot> | ref<inv::G::Root> |
                      ref<MaterializedRoot> | ref<KeywordRoot> |
                      ref<EmptyCallRoot> | ref<FixedRoot> | ref<BlankRoot>> {};
  using entry = Entry;
  using ignored = WS;
  using hidden = CM;
};


}  // namespace tconv
