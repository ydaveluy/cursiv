#pragma once

// A terminal whose text converter asks for a resource, read two ways: as an
// assignment of its own, and as one part of a data rule.

#include <string>
#include <string_view>

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tmemconv {

// The quotes come off, and the answer owns its text.
inline constexpr auto strip = [](std::string_view t) {
  if (t.size() < 2) return std::string(t);
  return std::string(t.substr(1, t.size() - 2));
};

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string direct;
  string_view joined;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Q : terminal<as<strip>(lit<"'"> + some(cr<"a-z">) + lit<"'">)> {};
  // The same terminal as one part of a rule that joins what its parts mean.
  struct Joined : rule<ref<Q> + ref<Q>> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::direct>(ref<Q>) +
                                      assign<&ast::Doc::joined>(ref<Joined>))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace tmemconv
