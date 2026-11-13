#pragma once

// The grammar behind the default converters and Parts, each body entered by
// its own first token, so a test reads back the rule's own value. `inv` is a
// grammar of its own as well, which a guard test lowers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tconv {

// A repair may lose a value, never invent one: resuming one byte into
// `state` must not make the name `tate`.
namespace inv {
struct N : cursiv::Extends<cursiv::AstNode> {
  string name;
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
  string name;
};
constexpr std::string_view unquote(std::string_view t) {
  return t.size() >= 2 ? t.substr(1, t.size() - 2) : t;
}
}  // namespace comp

namespace materialized {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string copied;
  string written;
};
inline std::string copy_unquoted(std::string_view text) {
  return std::string(text.substr(1, text.size() - 2));
}
inline std::string write_unquoted(std::string_view text) {
  return std::string(text.substr(1, text.size() - 2));
}
}  // namespace materialized

// Converters answering classes of the language's own, plain or not, into every
// form a member takes.
namespace values {
struct Uuid {
  std::array<std::uint8_t, 4> bytes{};
};
// Counts its instances: what the tree's teardown runs, it runs for all of them.
struct Tracked {
  inline static int live = 0;
  std::string raw;
  std::vector<int> parts;
  Tracked() { ++live; }
  Tracked(const Tracked& o) : raw(o.raw), parts(o.parts) { ++live; }
  Tracked(Tracked&& o) noexcept
      : raw(std::move(o.raw)), parts(std::move(o.parts)) {
    ++live;
  }
  Tracked& operator=(const Tracked&) = default;
  Tracked& operator=(Tracked&&) noexcept = default;
  ~Tracked() { --live; }
};
class Money {
 public:
  Money() = default;
  explicit Money(std::string_view text) : _text(text) {}
  [[nodiscard]] const std::string& text() const { return _text; }

 private:
  std::string _text;
};
struct Blob {
  std::string bytes;
};
inline Tracked tracked(std::string_view text) {
  Tracked out;
  out.raw = std::string(text);
  out.parts.push_back(static_cast<int>(text.size()));
  return out;
}
inline Uuid uuid(std::string_view text) {
  Uuid out;
  for (std::size_t i = 0; i < out.bytes.size() && i < text.size(); ++i)
    out.bytes[i] = static_cast<std::uint8_t>(text[i]);
  return out;
}
inline Money money(std::string_view text) { return Money(text); }
inline std::unique_ptr<Blob> blob(std::string_view text) {
  return std::make_unique<Blob>(Blob{std::string(text)});
}
inline cursiv::ConversionResult<Tracked> checked(std::string_view text) {
  if (text == "^0") return cursiv::ConversionResult<Tracked>::err("no zero");
  return cursiv::ConversionResult<Tracked>::ok(tracked(text));
}
inline std::vector<std::string> joined(
    std::span<const std::string_view> parts) {
  return {parts.begin(), parts.end()};
}
inline std::set<std::string> tags(std::span<const std::string_view> parts) {
  return {parts.begin(), parts.end()};
}
inline std::map<std::string, int> counts(
    std::span<const std::string_view> parts) {
  std::map<std::string, int> out;
  for (const std::string_view part : parts) ++out[std::string(part)];
  return out;
}
inline cursiv::ConversionResult<std::vector<int>> digits(
    std::string_view text) {
  if (text == "<0>")
    return cursiv::ConversionResult<std::vector<int>>::err("no zero");
  std::vector<int> out;
  for (const char c : text.substr(1, text.size() - 2)) out.push_back(c - '0');
  return cursiv::ConversionResult<std::vector<int>>::ok(std::move(out));
}
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Tracked one;
  vector<Tracked> many;
  optional<Tracked> maybe;
  variant<std::monostate, Tracked, int64> either;
  Tracked guarded;
  Uuid id;
  Money price;
  vector<Money> prices;
  std::unique_ptr<Blob> payload;
  vector<string> pieces;
  vector<string> forwarded;
  vector<int> numbers;
  std::set<std::string> tagged;
  std::map<std::string, int> counted;
};
}  // namespace values

// One terminal feeding both kinds of member: its converter answers a slice, and
// a data rule over it decodes that slice.
namespace chain {
// A class of its own, built from the string a data rule passes on.
struct Boxed {
  std::string text;
  Boxed() = default;
  explicit Boxed(std::string value) : text(std::move(value)) {}
};
// With no empty state: a list or an optional needs none.
struct Ident {
  std::string text;
  explicit Ident(std::string value) : text(std::move(value)) {}
};
// Takes a string by assignment, and nothing of its own type.
struct Latch {
  std::string text;
  Latch() = default;
  explicit Latch(std::string value) : text(std::move(value)) {}
  Latch& operator=(std::string value) {
    text = std::move(value);
    return *this;
  }
  Latch& operator=(Latch&&) = delete;
};
// A class nothing can derive from still converts.
struct Trimmed final {
  std::string operator()(std::string_view text) const {
    return std::string(text.substr(1));
  }
};
// Built once and never assigned: a list or an optional asks no more.
struct ConstText {
  const std::string text;
  explicit ConstText(std::string value) : text(std::move(value)) {}
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  // Reads its view when destroyed, so a tree that frees its text before its
  // nodes fails under a sanitizer.
  inline static std::size_t read = 0;
  string_view raw;
  string decoded;
  Boxed boxed;
  optional<Ident> first;
  vector<Ident> rest;
  vector<variant<int64, string>> mixed;
  // Held directly, with its first value; the member after it shows the write
  // stays inside.
  Ident only{"none"};
  unsigned guard = 0;
  int64 wide = -1;
  optional<ConstText> constText;
  vector<ConstText> constTexts;
  optional<Ident> passed;
  // No empty state, held directly and in a list.
  variant<Ident, int64> pick{int64{0}};
  vector<variant<Ident, int64>> picks;
  // What a converter answers whole.
  variant<int64, string> answer;
  vector<variant<int64, string>> answers;
  optional<string> maybe;
  Latch latch;
  string trimmed;
  float64 ratio = 0;
  ~Doc() override {
    for (const char c : raw) read += static_cast<unsigned char>(c);
  }
};
inline std::string tag(std::string_view text) {
  return std::string(text.substr(1));
}
inline Ident ident(std::string_view text) {
  return Ident(std::string(text.substr(1)));
}
inline std::vector<std::variant<std::int64_t, std::string>> mixed(
    std::string_view text) {
  return {std::int64_t{1}, std::string(text.substr(1))};
}
inline ConstText const_text(std::string_view text) {
  return ConstText(std::string(text.substr(1)));
}
inline std::variant<std::int64_t, std::string> answer(std::string_view text) {
  if (text == "?one") return std::int64_t{1};
  return std::string(text.substr(1));
}
inline std::optional<std::string> maybe(std::string_view text) {
  if (text == "*none") return std::nullopt;
  return std::string(text.substr(1));
}
inline std::string_view slice(std::string_view text) {
  return text.substr(1, text.size() - 2);
}
inline std::string decode(std::string_view text) {
  std::string out;
  for (const char c : text)
    if (c != '\\') out += c;
  return out;
}
}  // namespace chain

// A chain of unnamed boxes, each with a named item and a use of it.
namespace deep {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Use : cursiv::Extends<cursiv::AstNode> {
  reference<Item> target;
};
struct Box : cursiv::Extends<cursiv::AstNode> {
  Item* item = nullptr;
  Use* use = nullptr;
  Box* inner = nullptr;
};
}  // namespace deep

// A class reached by two paths is one node, and a class that shares nothing in
// the same grammar stays plain.
namespace diamond {
struct Named : cursiv::VirtualExtends<cursiv::AstNode> {
  string name;
};
struct Typed : cursiv::VirtualExtends<Named> {
  string_view type;
};
struct Documented : cursiv::VirtualExtends<Named> {
  string doc;
};
struct Field : cursiv::Extends<Typed, Documented> {};
// No parse ever builds one, so its class has stated no offsets.
struct Hand : cursiv::Extends<Typed, Documented> {};
struct Use : cursiv::Extends<cursiv::AstNode> {
  reference<Named> target;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Field*> fields;
  vector<Use*> uses;
  vector<boolean> bits;
};
inline bool is_plus(std::string_view text) { return text == "+"; }
inline std::string upper(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}
}  // namespace diamond

// The value of a data rule is the same however the rule is composed and at
// every -O: `a . b` and `a.b` denote one name. The value itself is checked,
// not only its stability across levels.
namespace dv {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view a;     // ref<QN>    -- parts written tight
  string_view b;     // ref<QN1>   -- the collapsing wrapper, trivia inside
  string_view c;     // ref<Path>  -- data rules composed of data rules
  reference<Doc> r;  // the same nested payload, as a link
  string_view quote;
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
  struct CompRoot : rule<cursiv::GrammarSpec::create<comp::N> +
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
  struct TRACKED : terminal<as<&values::tracked>(lit<"%"> + some(d))> {};
  struct CHECKED : terminal<as<&values::checked>(lit<"^"> + some(d))> {};
  struct UUID : terminal<as<&values::uuid>(lit<"~"> + some(cr<"a-z">))> {};
  struct MONEY : terminal<as<&values::money>(lit<"$"> + some(d))> {};
  struct BLOB : terminal<as<&values::blob>(lit<"#"> + some(cr<"a-z">))> {};
  struct PIECE : terminal<lit<"@"> + some(d)> {};
  struct Pieces : rule<as<&values::joined>(some(ref<PIECE>, lit<",">))> {};
  struct ForwardedPieces : rule<ref<Pieces>> {};
  struct Tags : rule<as<&values::tags>(some(ref<PIECE>))> {};
  struct Counts : rule<as<&values::counts>(some(ref<PIECE>))> {};
  struct NUMBERS
      : terminal<as<&values::digits>(lit<"<"> + some(d) + lit<">">)> {};
  struct ValuesRoot
      : rule<kw<"values"> + assign<&values::Doc::one>(ref<TRACKED>) +
             many(append<&values::Doc::many>(ref<TRACKED>)) + kw<";"> +
             option(kw<"maybe"> + assign<&values::Doc::maybe>(ref<TRACKED>)) +
             kw<"either"> + assign<&values::Doc::either>(ref<TRACKED>) +
             assign<&values::Doc::guarded>(ref<CHECKED>) +
             assign<&values::Doc::id>(ref<UUID>) +
             assign<&values::Doc::price>(ref<MONEY>) +
             many(append<&values::Doc::prices>(ref<MONEY>)) +
             assign<&values::Doc::payload>(ref<BLOB>) +
             assign<&values::Doc::pieces>(ref<Pieces>) + kw<"then"> +
             assign<&values::Doc::forwarded>(ref<ForwardedPieces>) +
             assign<&values::Doc::numbers>(ref<NUMBERS>) +
             option(kw<"sets"> + assign<&values::Doc::tagged>(ref<Tags>) +
                    kw<"maps"> + assign<&values::Doc::counted>(ref<Counts>))> {
  };
  struct DOC : terminal<as<&diamond::upper>(lit<"&"> + some(cr<"a-z">))> {};
  struct DiamondField : rule<create<diamond::Field>(
                            assign<&diamond::Field::name>(ref<ID>) + kw<":"> +
                            assign<&diamond::Field::type>(ref<ID>) +
                            assign<&diamond::Field::doc>(ref<DOC>))> {};
  struct QUOTED_RAW : terminal<as<&chain::slice>(
                          lit<"\""> + many(cr<"a-z\\\\">) + lit<"\"">)> {};
  struct Decoded : rule<as<&chain::decode>(ref<QUOTED_RAW>)> {};
  struct TAG : terminal<as<&chain::tag>(lit<"@"> + some(cr<"a-z">))> {};
  struct PassedTag : rule<ref<TAG>> {};
  struct IDENT : terminal<as<&chain::ident>(lit<"$"> + some(cr<"a-z">))> {};
  struct MIXED : terminal<as<&chain::mixed>(lit<"#"> + some(cr<"a-z">))> {};
  struct ONLY : terminal<as<&chain::ident>(lit<"!"> + some(cr<"a-z">))> {};
  struct SMALL : terminal<as<std::int8_t>(some(d))> {};
  struct CONST_TEXT
      : terminal<as<&chain::const_text>(lit<"="> + some(cr<"a-z">))> {};
  struct PassedIdent : rule<ref<IDENT>> {};
  struct ANSWER : terminal<as<&chain::answer>(lit<"?"> + some(cr<"a-z">))> {};
  struct MAYBE : terminal<as<&chain::maybe>(lit<"*"> + some(cr<"a-z">))> {};
  struct TRIMMED : terminal<as<chain::Trimmed{}>(lit<"^"> + some(cr<"a-z">))> {
  };
  struct WHOLE : terminal<as<std::int64_t>(some(d))> {};
  struct ChainRoot
      : rule<
            kw<"chain"> + assign<&chain::Doc::raw>(ref<QUOTED_RAW>) +
            assign<&chain::Doc::decoded>(ref<Decoded>) +
            option(assign<&chain::Doc::boxed>(ref<PassedTag>)) +
            option(assign<&chain::Doc::first>(ref<IDENT>) +
                   many(append<&chain::Doc::rest>(ref<IDENT>))) +
            option(assign<&chain::Doc::mixed>(ref<MIXED>)) +
            option(assign<&chain::Doc::only>(ref<ONLY>)) +
            option(kw<"small"> + assign<&chain::Doc::wide>(ref<SMALL>)) +
            option(kw<"constant"> +
                   assign<&chain::Doc::constText>(ref<CONST_TEXT>) +
                   many(append<&chain::Doc::constTexts>(ref<CONST_TEXT>))) +
            option(kw<"pass"> + assign<&chain::Doc::passed>(ref<PassedIdent>)) +
            option(kw<"pick"> + assign<&chain::Doc::pick>(ref<ONLY>) +
                   append<&chain::Doc::picks>(ref<ONLY>) +
                   append<&chain::Doc::picks>(ref<SMALL>)) +
            option(kw<"answer"> + assign<&chain::Doc::answer>(ref<ANSWER>) +
                   many(append<&chain::Doc::answers>(ref<ANSWER>))) +
            option(kw<"maybe"> + assign<&chain::Doc::maybe>(ref<MAYBE>)) +
            option(kw<"latch"> + assign<&chain::Doc::latch>(ref<TAG>)) +
            option(kw<"trimmed"> + assign<&chain::Doc::trimmed>(ref<TRIMMED>)) +
            option(kw<"ratio"> + assign<&chain::Doc::ratio>(ref<WHOLE>))> {};
  struct DeepItem : rule<create<deep::Item>(
                        kw<"item"> + assign<&deep::Item::name>(ref<ID>))> {};
  struct DeepUse : rule<create<deep::Use>(
                       kw<"see"> + assign<&deep::Use::target>(ref<ID>))> {};
  struct DeepBox
      : rule<create<deep::Box>(
            lit<"["> + assign<&deep::Box::item>(ref<DeepItem>) +
            assign<&deep::Box::use>(ref<DeepUse>) +
            option(assign<&deep::Box::inner>(ref<DeepBox>)) + lit<"]">)> {};
  struct DiamondUse : rule<create<diamond::Use>(
                          kw<"use"> + assign<&diamond::Use::target>(ref<ID>))> {
  };
  struct BIT : terminal<as<&diamond::is_plus>(lit<"+"> | lit<"-">)> {};
  struct DiamondRoot
      : rule<kw<"diamond"> +
             many(append<&diamond::Doc::fields>(ref<DiamondField>)) +
             many(append<&diamond::Doc::uses>(ref<DiamondUse>)) +
             option(kw<"bits"> + many(append<&diamond::Doc::bits>(ref<BIT>)))> {
  };
  struct Entry
      : rule<ref<DvRoot> | ref<CompRoot> | ref<inv::G::Root> |
             ref<MaterializedRoot> | ref<KeywordRoot> | ref<EmptyCallRoot> |
             ref<FixedRoot> | ref<BlankRoot> | ref<ValuesRoot> |
             ref<DiamondRoot> | ref<ChainRoot> | ref<DeepBox>> {};
  using entry = Entry;
  using ignored = WS;
  using hidden = CM;
};


}  // namespace tconv
