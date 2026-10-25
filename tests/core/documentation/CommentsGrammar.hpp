#pragma once

// The two comment rules a provider binds to by name, and enough shape to hang
// one on: a declaration, and a member inside it. The entry accepts the empty
// document, which is the one root that owns no trivia.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tcomment {

namespace ast {
struct Element : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
struct DataType : cursiv::Extends<Element> {};
struct Feature : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
struct Entity : cursiv::Extends<Element> {
  cursiv::vector<Feature*> features;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Element*> elements;
};
}  // namespace ast

struct Comments : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  struct FeatureRule
      : rule<create<ast::Feature>(assign<&ast::Feature::name>(ref<ID>) +
                                  lit<":"> + ref<ID>)> {};
  struct DataTypeRule
      : rule<create<ast::DataType>(ikw<"datatype"> +
                                   assign<&ast::DataType::name>(ref<ID>))> {};
  struct EntityRule
      : rule<create<ast::Entity>(
            ikw<"entity"> + assign<&ast::Entity::name>(ref<ID>) + lit<"{"> +
            many(append<&ast::Entity::features>(ref<FeatureRule>)) +
            lit<"}">)> {};
  struct ElementRule : rule<ref<DataTypeRule> | ref<EntityRule>> {};
  struct DocRule : rule<create<ast::Doc>(
                       many(append<&ast::Doc::elements>(ref<ElementRule>)))> {};

  using entry = DocRule;
  using ignore = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace tcomment
