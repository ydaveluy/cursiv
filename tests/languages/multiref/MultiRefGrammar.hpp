#pragma once

// One name binding to a group: `SELECT t FROM t, t` writes one column and the
// scope exposes two items under `t`. The smallest grammar that holds a
// `multi_reference`.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace fmulti {

namespace ast {
struct Item : cursiv::AstNode {
  std::string_view name;
};
struct Pick : cursiv::AstNode {
  multi_reference<Item> name;
};
struct Query : cursiv::AstNode {
  Pick* pick = nullptr;
  vector<Item*> items;
};
// A root above the query: what a document exports is its top level, and a
// FROM item is a query-local binding, not a workspace symbol.
struct Model : cursiv::AstNode {
  Query* query = nullptr;
};
}  // namespace ast

struct MultiG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"_a-zA-Z"> + many(w)> {};

  struct ItemRule : rule<assign<&ast::Item::name>(ref<ID>)> {};
  struct PickRule : rule<assign<&ast::Pick::name>(ref<ID>)> {};
  struct QueryRule
      : rule<ikw<"SELECT"> + assign<&ast::Query::pick>(ref<PickRule>) +
             ikw<"FROM"> +
             some(append<&ast::Query::items>(ref<ItemRule>), lit<",">)> {};
  struct ModelRule : rule<assign<&ast::Model::query>(ref<QueryRule>)> {};
  using entry = ModelRule;
  using ignored = WS;
};

}  // namespace fmulti
