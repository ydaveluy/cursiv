#pragma once

// The grammar behind the default converters and Parts, each body entered by
// its own first token, so a lane reads back the rule's own value. `inv` is a
// grammar of its own as well, which a guard lane lowers.

#include <cstddef>
#include <memory_resource>
#include <string_view>

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
  using ignore = WS;
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

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CM : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // Its converter is what the join must read; outside QN/Path, its presence
  // must not change the meaning or the direct source view of those
  // independent data rules.
  struct QUOTED : terminal<lit<"'"> + some(cr<"a-z">) + lit<"'">,
                           cursiv::text_conv<comp::unquote>> {};
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
  struct COPIED : terminal<lit<"\""> + many(cr<"a-z">) + lit<"\"">,
                           text_conv<materialized::copy_unquoted>> {};
  struct WRITTEN : terminal<lit<"`"> + many(cr<"a-z">) + lit<"`">,
                            text_conv<materialized::write_unquoted>> {};
  struct MaterializedRoot
      : rule<kw<"materialized"> +
             assign<&materialized::Doc::copied>(ref<COPIED>) +
             assign<&materialized::Doc::written>(ref<WRITTEN>)> {};
  inline static constexpr auto operator_text = [](std::string_view text) {
    return text;
  };
  template <auto Expression>
  struct Operator : rule<Expression, text_conv<operator_text>> {};
  struct KeywordRoot
      : rule<kw<"keyword"> + assign<&comp::N::name>(ref<Operator<kw<"is">>>)> {
  };
  struct Entry : rule<ref<DvRoot> | ref<CompRoot> | ref<inv::G::Root> |
                      ref<MaterializedRoot> | ref<KeywordRoot>> {};
  using entry = Entry;
  using ignore = WS;
  using hidden = CM;
};


}  // namespace tconv
