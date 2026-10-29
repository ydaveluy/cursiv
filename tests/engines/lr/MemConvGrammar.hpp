#pragma once

// A terminal whose text converter asks for a resource, read two ways: as an
// assignment of its own, and as one part of a data rule.

#include <memory_resource>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tmemconv {

// The quotes come off, and the answer is written into the resource so the
// view outlives the call.
inline constexpr auto strip = [](std::string_view t,
                                 std::pmr::memory_resource* m) {
  if (t.size() < 2) return t;
  const std::string_view in = t.substr(1, t.size() - 2);
  char* const p = static_cast<char*>(m->allocate(in.size(), alignof(char)));
  for (std::size_t i = 0; i < in.size(); ++i) p[i] = in[i];
  return std::string_view(p, in.size());
};

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view direct;
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
