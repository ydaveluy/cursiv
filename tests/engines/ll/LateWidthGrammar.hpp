#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlllatewidth {
struct Word : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Value : cursiv::Extends<cursiv::AstNode> {
  vector<Word*> prefix;
};
struct NumberValue : cursiv::Extends<Value> {
  string_view number;
};
struct DimensionValue : cursiv::Extends<Value> {
  string_view dimension;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Value* value = nullptr;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  struct DIMENSION : terminal<some(d) + lit<"px">> {};
  struct WordRule : rule<assign<&Word::name>(ref<ID>)> {};
  // WordRule has callers in both arms: its exact return contexts differ.
  struct NumberRule
      : rule<create<NumberValue>(
            some(append<&Value::prefix>(ref<WordRule>) + lit<";">) +
            assign<&NumberValue::number>(ref<NUMBER>) +
            (lit<"px!"> | lit<"px?"> | lit<"?">))> {};
  struct DimensionRule
      : rule<create<DimensionValue>(
            some(append<&Value::prefix>(ref<WordRule>) + lit<";">) +
            assign<&DimensionValue::dimension>(ref<DIMENSION>) + lit<"!">)> {};
  struct Root : rule<create<Document>(assign<&Document::value>(
                         ref<NumberRule> | ref<DimensionRule>)) +
                     eof> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tlllatewidth
