#pragma once

#include <algorithm>
#include <string_view>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlldatatext {
struct Value : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Value*> values;
};
inline thread_local unsigned untickCalls = 0;
inline constexpr auto untick = [](std::string_view text) {
  ++untickCalls;
  return text.substr(1, text.size() - 2);
};
inline constexpr auto prefix = [](std::string_view text,
                                  std::pmr::memory_resource* memory) {
  const auto room = cursiv::text_room(memory, text.size() + 1);
  room[0] = '#';
  std::copy(text.begin(), text.end(), room.begin() + 1);
  return std::string_view(room.data(), room.size());
};
inline constexpr auto refuseZ = [](std::string_view text) {
  using Result = cursiv::ConversionResult<std::string_view>;
  return text.find('z') == std::string_view::npos ? Result::ok(text)
                                                  : Result::err("no z");
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(w)> {};
  struct QUOTED : terminal<as<untick>(through(lit<"`">, lit<"`">))> {};
  struct CHECKED : terminal<as<refuseZ>(lit<"!"> + some(w))> {};
  struct Symbol : rule<ref<QUOTED> | ref<ID>> {};
  struct Forward : rule<ref<Symbol>> {};
  struct Path : rule<ref<Symbol> + lit<"."> + ref<Symbol>> {};
  struct Converted : rule<as<tlldatatext::prefix>(ref<Symbol>)> {};
  struct Literal : rule<as<tlldatatext::prefix>(lit<"literal">)> {};
  struct ForwardConverted : rule<ref<Converted>> {};
  struct ConvertedPath : rule<ref<Converted> + lit<"."> + ref<Converted>> {};
  struct CheckedPath : rule<ref<CHECKED> + lit<"."> + ref<Symbol>> {};
  struct Keyed : rule<kw<"key"> + lit<"."> + ref<Symbol>> {};
  struct Item
      : rule<(kw<"symbol"> + assign<&Value::text>(ref<Symbol>)) |
             (kw<"forward"> + assign<&Value::text>(ref<Forward>)) |
             (kw<"path"> + assign<&Value::text>(ref<Path>)) |
             (kw<"converted"> + assign<&Value::text>(ref<Converted>)) |
             (kw<"literal"> + assign<&Value::text>(ref<Literal>)) |
             (kw<"convertedforward"> +
              assign<&Value::text>(ref<ForwardConverted>)) |
             (kw<"convertedpath"> + assign<&Value::text>(ref<ConvertedPath>)) |
             (kw<"checked"> + assign<&Value::text>(ref<CheckedPath>)) |
             (kw<"keyed"> + assign<&Value::text>(ref<Keyed>))> {};
  struct Root
      : rule<many(append<&Document::values>(ref<Item>) + lit<";">) + eof> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tlldatatext
