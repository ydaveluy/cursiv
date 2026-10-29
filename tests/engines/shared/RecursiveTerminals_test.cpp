#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LexRightLinear.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/RecursiveTerminalsGrammar.hpp"
#include "lexicalbackendsll/generated/Ll/Module.hpp"
#include "lexicalbackendsll/generated/Ll/Table.hpp"
#include "lexicalbackendslr/generated/Lr/Module.hpp"
#include "lexicalbackendslr/generated/Lr/Table.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace recursive_terminals_cases {

namespace {

void check(const cursiv::Parser& parser, std::string_view text, bool expected) {
  const auto result = parser.parse(text);
  const bool actual = result.accepted && result.diagnostics.empty() &&
                      result.value_as<trecursive::Node>() != nullptr;
  if (actual != expected)
    std::printf("recursive terminal input=%.*s expected=%d actual=%d\n",
                static_cast<int>(text.size() > 80 ? 80 : text.size()),
                text.data(), expected, actual);
  CHECK(actual == expected);
}

void exercise(const cursiv::Parser& parser) {
  for (const auto text : {"comment:/**/!",
                          "comment:/* /* x */ /* y */ */!",
                          "comment:/*/*/**/*/*/!",
                          "pair:/**/x/* /* */ */",
                          "mutual:()!",
                          "mutual:([([x])])!",
                          "code:{}!",
                          "code:{\"}{\" {\"}\"}}!",
                          "code:{\"escaped \\\" }\"}!",
                          "continuation:aab!",
                          "continuation:ab!",
                          "continuation:abb!",
                          "optional:q!",
                          "optional:nnq!",
                          "asserted:{{}}!",
                          "refused:b!",
                          "many:z!",
                          "many:{}{{}}z!",
                          "some:{}z!",
                          "exact:{}{}z!",
                          "through:{} body!",
                          "through:{{}}!",
                          "emptyopen:;",
                          "emptyopen:a;",
                          "emptyopen:;a;",
                          "marked:<==>{{}}==;",
                          "unicode:λω!",
                          "unicode:λλωω!",
                          "echo:<-->body]--;",
                          "echo:<->]-;x<-->]--;",
                          "paddecho:<->]-;",
                          "paddecho:<->]---;",
                          "emptyecho:<>] ",
                          "emptyecho:<>]   ",
                          "altecho:<aa:]bbbaa;",
                          "altecho:(bb:]aaabb;",
                          "altecho:<a:]a;x(b:]ab;",
                          "guardonly:#(())",
                          "dataonly:b()",
                          "dataonly:b(())",
                          "section:<(())END",
                          "same:<<!",
                          "longclose:xyyy!",
                          "opener:ab!",
                          "multi:{|(|x|}|)!",
                          "open:<*",
                          "open:<* x",
                          "open:<* <* x *> y",
                          "open:<* <* x *> y *>",
                          "positive:@/* /* */ */",
                          "negative:~/* /* */",
                          "negative:~plain"})
    check(parser, text, true);
  for (const auto text : {"comment:/* /* x */!",
                          "comment:/* unclosed!",
                          "comment:/**/x/**/!",
                          "mutual:([x)!",
                          "mutual:((x))!",
                          "code:{\"unclosed}!",
                          "code:{{}!",
                          "continuation:aa!",
                          "asserted:{{}!",
                          "asserted:x!",
                          "asserted:",
                          "refused:{}!",
                          "some:z!",
                          "exact:{}z!",
                          "exact:{}{}{}z!",
                          "through:{}",
                          "emptyopen:",
                          "emptyopen:x",
                          "marked:<==>{{}}=;",
                          "unicode:x!",
                          "unicode:δω!",
                          "echo:<-->body]-;",
                          "paddecho:<->];",
                          "emptyecho:<>]",
                          "altecho:<aa:]bbba;",
                          "altecho:(bb:]aaab;",
                          "unicode:",
                          "guardonly:#(()",
                          "dataonly:b(x)",
                          "dataonly:b(()",
                          "section:<(())EN",
                          "same:<<<<!",
                          "shortclose:xyyy!",
                          "opener:aabb!",
                          "multi:{|(|x|}!",
                          "positive:@/* /* */",
                          "negative:~/**/"})
    check(parser, text, false);

  const auto pair = parser.parse("pair:/**/x/* /* */ */");
  const auto* node = pair.value_as<trecursive::Node>();
  CHECK(node != nullptr);
  if (node != nullptr) {
    CHECK(node->parts.size() == 2);
    if (node->parts.size() == 2) {
      CHECK(node->parts[0] == "/**/");
      CHECK(node->parts[1] == "/* /* */ */");
    }
  }

  std::string deep = "comment:";
  for (unsigned i = 0; i < 100000; ++i) deep += "/*";
  for (unsigned i = 0; i < 100000; ++i) deep += "*/";
  deep += '!';
  check(parser, deep, true);

  std::string generic = "unicode:";
  for (unsigned i = 0; i < 2000; ++i) generic += "λ";
  for (unsigned i = 0; i < 2000; ++i) generic += "ω";
  generic += '!';
  check(parser, generic, true);

  const std::string mark(2048, '-');
  check(parser, "echo:<" + mark + ">" + mark + "!", false);
  check(parser, "echo:<" + mark + ">]" + mark + ";", true);
}

}  // namespace

void run() {
  exercise(*tlexbackends::generated::Ll::create_parser());
  exercise(*tlexbackends::generated::Lr::create_parser());
  const auto pureLl = tlexbackends::generated::Ll::create_parser();
  const auto pureLr = tlexbackends::generated::Lr::create_parser();
  for (const auto* parser : {pureLl.get(), pureLr.get()}) {
    check(*parser, "(())", true);
    check(*parser, "(", false);
  }
}

}  // namespace recursive_terminals_cases

namespace lex_right_linear_cases {

namespace {
void check(const cursiv::Parser& parser, std::string_view prefix,
           std::string_view body, bool expected = true) {
  const std::string source =
      "rightlinear:" + std::string(prefix) + std::string(body);
  const auto r = parser.parse(source);
  const auto* node = r.value_as<trightlinear::Node>();
  const bool clean =
      r.accepted && !r.cst().recovered() && r.diagnostics.empty() && node;
  if (clean != expected)
    std::fprintf(stderr, "right-linear %.*s (%zu bytes): expected %d, got %d\n",
                 static_cast<int>(prefix.size()), prefix.data(), body.size(),
                 expected, clean);
  CHECK(clean == expected);
  if (clean && expected) CHECK(node->text == body);
}

template <class Table>
std::string_view term_name(const Table& t, unsigned i) {
  if constexpr (requires { t.cfg.term_name(i); })
    return t.cfg.term_name(i);
  else
    return t.term_name(i);
}
template <class Table>
unsigned term_count(const Table& t) {
  if constexpr (requires { t.cfg.nterms; })
    return t.cfg.nterms;
  else
    return t.nterms;
}
template <class Table>
bool native(const Table& table, std::string_view name) {
  bool found = false;
  for (unsigned i = 0; i < term_count(table); ++i)
    found |= term_name(table, i) == name;
  CHECK(found);
  bool recursive = false;
  for (unsigned i = 0; i < table.nrelays; ++i)
    if (term_name(table, table.relays[i].term) == name)
      recursive |= table.relays[i].recognizes != nullptr;
  return found && !recursive;
}
template <class Cfg>
bool normalized(Cfg cfg) {
  cursiv::validate_lex_recursion(cfg);
  bool optimized = true;
  for (auto name :
       {"Run", "Sequence", "Ambiguous", "Alternative", "Reversed",
        "SequenceAlternative", "Unicode", "Counted", "Variable", "BigCount"}) {
    bool found = false;
    for (unsigned t = 0; t < cfg.nterms; ++t) {
      if (cfg.term_name(t) != name) continue;
      const auto root = cfg.lex[cfg.termBuild[t].lex];
      const auto body = root.kind == cursiv::LexKind::call ? cfg.lex[root.a]
                                                           : cursiv::LexNode{};
      found = root.kind == cursiv::LexKind::call &&
              root.b == cursiv::kNoLexNode &&
              body.kind == cursiv::LexKind::rep && body.min == 1 &&
              body.max == cursiv::kEndless;
    }
    CHECK(found);
    optimized &= found;
  }
  return optimized;
}
template <class Table>
void check_backends(const Table& table) {
  // Keep recognition rather than expanding a potentially huge or ambiguous
  // regular prefix into NFA/DFA states. Only the recursive cycle is removed.
  for (auto name : {"Run", "Sequence", "Ambiguous", "Alternative", "Reversed",
                    "SequenceAlternative", "Unicode", "Counted", "Variable",
                    "BigCount", "Marked", "Guarded", "EndGuarded", "Nested"})
    CHECK(!native(table, name));
}
void exercise(const cursiv::Parser& parser, bool optimized) {
  for (unsigned n = 1; n <= 12; ++n) {
    check(parser, "run:", std::string(n, 'a'));
    check(parser, "alternative:", std::string(n, 'c'));
    check(parser, "reversed:", std::string(n, 'd'));
    check(parser, "counted:", std::string(n, 'e'));
    check(parser, "suffix:", std::string(n + 1, 'a') + "!");
    check(parser, "wrapped:", "#" + std::string(n, 'a') + "#");
    std::string pairs, unicode;
    for (unsigned k = 0; k < n; ++k) {
      pairs += "ab";
      unicode += "λ😀";
    }
    check(parser, "sequence:", pairs);
    check(parser, "seqalt:", pairs);
    check(parser, "unicode:", unicode);
  }
  check(parser, "bigcount:", std::string(100000, 'z'));
  for (auto text : {"a", "ab", "aab", "aba", "abab", "abaab"})
    check(parser, "ambiguous:", text);
  for (auto text : {"ab!", "abb!", "aabb!", "abab!"})
    check(parser, "ambsuffix:", text);
  check(parser, "variable:", "xyxxxyxxy");
  check(parser, "marked:", "mmm");
  check(parser, "marked:", "msm");
  check(parser, "marked:", "mmsm");
  check(parser, "marked:", "mss", false);
  check(parser, "guarded:", "ggg");
  check(parser, "guarded:", "gx", false);
  check(parser, "end:", "q");
  check(parser, "end:", "qq", false);
  check(parser, "nested:", "((()))");
  check(parser, "nested:", "((())", false);
  for (auto prefix :
       {"run:", "sequence:", "ambiguous:", "alternative:", "reversed:",
        "seqalt:", "unicode:", "counted:", "variable:"})
    check(parser, prefix, "", false);
  check(parser, "sequence:", "aba", false);
  check(parser, "suffix:", "a!", false);
  check(parser, "wrapped:", "#aaa@", false);

  // Fail a missing optimization before allocating quadratic endpoint caches.
  if (optimized) {
    check(parser, "run:", std::string(100000, 'a'));
    check(parser, "alternative:", std::string(100000, 'c'));
    std::string pairs;
    for (unsigned i = 0; i < 50000; ++i) pairs += "ab";
    check(parser, "sequence:", pairs);
  }
}
}  // namespace
void run() {
  const auto ll = tlexbackends::generated::Ll::create_parser();
  const auto lr = tlexbackends::generated::Lr::create_parser();
  check_backends(tlexbackends::generated::Ll::kTable);
  check_backends(tlexbackends::generated::Lr::kTable);
  exercise(*ll, normalized(cursiv::LlLowering<trightlinear::Ll>::table().cfg));
  exercise(*lr, normalized(cursiv::LrLowering<trightlinear::Lr>::table().cfg));
}

}  // namespace lex_right_linear_cases

namespace lex_right_linear_shape_cases {

// Compare all endpoints against an independent small-language evaluator.
// The generated LL/LR suite separately exercises captures and call scopes.
namespace {
using cursiv::kEndless;
using cursiv::kNoLexNode;
using cursiv::LexKind;
using cursiv::LexNode;

struct Graph {
  std::uint16_t nlex = 0, nterms = 2;
  std::vector<LexNode> lex = std::vector<LexNode>(256);
  std::vector<std::uint16_t> lexKid;
  std::vector<cursiv::ByteSet> sets;
  struct Term {
    std::uint16_t lex = 0;
  };
  std::array<Term, 4> termBuild;

  std::uint16_t node(LexNode value) {
    lex[nlex] = value;
    return nlex++;
  }
  std::uint16_t character(char c) {
    cursiv::ByteSet set;
    set.add(static_cast<unsigned char>(c));
    auto at = std::find(sets.begin(), sets.end(), set);
    if (at == sets.end()) {
      sets.push_back(set);
      at = sets.end() - 1;
    }
    return node(
        {LexKind::chars, static_cast<std::uint16_t>(at - sets.begin())});
  }
  std::uint16_t list(LexKind kind, const std::vector<std::uint16_t>& kids) {
    const auto at = static_cast<std::uint16_t>(lexKid.size());
    lexKid.insert(lexKid.end(), kids.begin(), kids.end());
    return node({kind, at, static_cast<std::uint16_t>(kids.size())});
  }
  std::uint16_t repeat(std::uint16_t kid, unsigned min, unsigned max) {
    return node({LexKind::rep, kid, kNoLexNode, min, max});
  }
  // Reification shares byte sets but creates distinct expression nodes.
  std::uint16_t copy(std::uint16_t at) {
    const auto e = lex[at];
    if (e.kind == LexKind::chars) return node(e);
    if (e.kind == LexKind::rep) return repeat(copy(e.a), e.min, e.max);
    std::vector<std::uint16_t> kids;
    for (unsigned i = 0; i < e.b; ++i) kids.push_back(copy(lexKid[e.a + i]));
    return list(e.kind, kids);
  }
};

using Endpoints = std::set<unsigned>;

class Oracle {
 public:
  Oracle(const Graph& graph, const std::string& text)
      : _g(graph), _text(text) {}

  Endpoints read(unsigned at, unsigned pos) {
    const auto key = std::pair(at, pos);
    if (const auto found = _memo.find(key); found != _memo.end())
      return found->second;
    const auto e = _g.lex[at];
    Endpoints out;
    switch (e.kind) {
      case LexKind::chars:
        if (pos < _text.size() &&
            _g.sets[e.a].has(static_cast<unsigned char>(_text[pos])))
          out.insert(pos + 1);
        break;
      case LexKind::call:
        out = read(cursiv::lex_callee(_g, e), pos);
        break;
      case LexKind::seq:
        out.insert(pos);
        for (unsigned i = 0; i < e.b; ++i) {
          Endpoints next;
          for (const auto from : out) {
            const auto ends = read(_g.lexKid[e.a + i], from);
            next.insert(ends.begin(), ends.end());
          }
          out = std::move(next);
        }
        break;
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i) {
          const auto ends = read(_g.lexKid[e.a + i], pos);
          out.insert(ends.begin(), ends.end());
        }
        break;
      case LexKind::rep: {
        Endpoints cur{pos};
        // Every repeated body used by the oracle consumes. No successful
        // path can therefore need more rounds than the input has bytes.
        for (unsigned n = 0; n <= _text.size() + 1 && n <= e.max; ++n) {
          if (n >= e.min) out.insert(cur.begin(), cur.end());
          Endpoints next;
          for (const auto from : cur) {
            const auto ends = read(e.a, from);
            next.insert(ends.begin(), ends.end());
          }
          if (next.empty()) break;
          cur = std::move(next);
        }
        break;
      }
      default:
        CHECK(false);  // unsupported shapes are tested without this oracle
        break;
    }
    return _memo[key] = out;
  }

 private:
  const Graph& _g;
  const std::string& _text;
  std::map<std::pair<unsigned, unsigned>, Endpoints> _memo;
};

enum class Prefix {
  single,
  sequence,
  ambiguous,
  counted,
  variable,
  nullable_part
};
enum class Form { optional, base_first, recursive_first };

Graph grammar(Prefix shape, Form form) {
  Graph g;
  const auto a = g.character('a'), b = g.character('b');
  std::vector<std::uint16_t> prefix;
  switch (shape) {
    case Prefix::single:
      prefix = {a};
      break;
    case Prefix::sequence:
      prefix = {a, b};
      break;
    case Prefix::ambiguous:
      prefix = {g.list(LexKind::alt, {a, g.list(LexKind::seq, {a, b})})};
      break;
    case Prefix::counted:
      prefix = {a, g.repeat(b, 1, 3)};
      break;
    case Prefix::variable:
      prefix = {g.repeat(a, 1, kEndless)};
      break;
    case Prefix::nullable_part:
      prefix = {g.repeat(a, 0, 1), b};
      break;
  }
  const auto call = g.node({LexKind::call, kNoLexNode, 1});
  std::uint16_t root;
  if (form == Form::optional) {
    prefix.push_back(g.repeat(call, 0, 1));
    root = g.list(LexKind::seq, prefix);
  } else {
    std::vector<std::uint16_t> copied;
    for (const auto kid : prefix) copied.push_back(g.copy(kid));
    const auto base =
        copied.size() == 1 ? copied[0] : g.list(LexKind::seq, copied);
    prefix.push_back(call);
    const auto recursive = g.list(LexKind::seq, prefix);
    root =
        g.list(LexKind::alt, form == Form::base_first
                                 ? std::vector<std::uint16_t>{base, recursive}
                                 : std::vector<std::uint16_t>{recursive, base});
  }
  g.termBuild[1].lex = root;
  return g;
}

void endpoint_differential() {
  for (const auto shape :
       {Prefix::single, Prefix::sequence, Prefix::ambiguous, Prefix::counted,
        Prefix::variable, Prefix::nullable_part})
    for (const auto form :
         {Form::optional, Form::base_first, Form::recursive_first}) {
      auto before = grammar(shape, form), after = before;
      const auto root = before.termBuild[1].lex;
      cursiv::detail::LexRightLinear<Graph>{after}.run();
      CHECK(after.lex[root].kind == LexKind::call);
      CHECK(after.lex[root].b == kNoLexNode);
      const auto closure = after.lex[after.lex[root].a];
      CHECK(closure.kind == LexKind::rep);
      CHECK(closure.min == 1 && closure.max == kEndless);
      CHECK(before.lexKid == after.lexKid);
      const auto once = after.lex;
      const auto count = after.nlex;
      cursiv::detail::LexRightLinear<Graph>{after}.run();
      CHECK(after.nlex == count && after.lex == once);
      for (unsigned n = 0; n <= 8; ++n)
        for (unsigned bits = 0; bits < (1u << n); ++bits) {
          std::string text(n, 'a');
          for (unsigned i = 0; i < n; ++i)
            if (bits & (1u << i)) text[i] = 'b';
          CHECK(Oracle(before, text).read(root, 0) ==
                Oracle(after, text).read(root, 0));
        }

      const auto call = before.node({LexKind::call, kNoLexNode, 1});
      const auto caller =
          before.list(LexKind::seq, {call, before.character('a')});
      auto called = before;
      cursiv::detail::LexRightLinear<Graph>{called}.run();
      CHECK(called.lex[call].kind == LexKind::call);
      for (unsigned n = 0; n < 9; ++n) {
        const std::string text(n, 'a');
        CHECK(Oracle(before, text).read(caller, 0) ==
              Oracle(called, text).read(caller, 0));
      }
    }
}

void excluded_shapes() {
  // Synthetic unsupported prefixes establish the whitelist. Full generated
  // grammars exercise their actual validity, refusal codes and scopes.
  for (const auto kind :
       {LexKind::call, LexKind::mark, LexKind::same, LexKind::nguard,
        LexKind::positive, LexKind::negative, LexKind::end_mark, LexKind::token,
        LexKind::skip, LexKind::through_lit, LexKind::through_same,
        LexKind::through_nested, LexKind::gap_has, LexKind::gap_is,
        LexKind::implied}) {
    auto g = grammar(Prefix::single, Form::optional);
    const auto root = g.termBuild[1].lex;
    g.lex[g.lexKid[g.lex[root].a]].kind = kind;
    const auto old = g.lex;
    cursiv::detail::LexRightLinear<Graph>{g}.run();
    CHECK(g.lex == old);
  }
  {
    auto g = grammar(Prefix::single, Form::optional);
    const auto root = g.termBuild[1].lex;
    g.lex[g.lexKid[g.lex[root].a]] = LexNode{LexKind::seq, 0, 0};
    const auto old = g.lex;
    cursiv::detail::LexRightLinear<Graph>{g}.run();
    CHECK(g.lex == old);  // E itself is nullable
  }
  {
    auto g = grammar(Prefix::single, Form::optional);
    const auto root = g.termBuild[1].lex;
    g.lexKid[g.lex[root].a] = g.repeat(g.list(LexKind::seq, {}), 0, kEndless);
    const auto old = g.lex;
    cursiv::detail::LexRightLinear<Graph>{g}.run();
    CHECK(g.lex == old);  // nullable repetition must keep its refusal
  }
  {
    auto g = grammar(Prefix::single, Form::optional);
    g.sets[0] = {};
    const auto old = g.lex;
    cursiv::detail::LexRightLinear<Graph>{g}.run();
    CHECK(g.lex == old);  // unproductive E
  }
}

void shared_roots_and_limits() {
  {
    auto g = grammar(Prefix::sequence, Form::optional);
    const auto root = g.termBuild[1].lex;
    g.termBuild[2].lex = root;
    g.nterms = 3;
    const auto count = g.nlex;
    cursiv::detail::LexRightLinear<Graph>{g}.run();
    CHECK(g.termBuild[1].lex == root && g.termBuild[2].lex == root);
    CHECK(g.lex[root].kind == LexKind::call && g.lex[root].b == kNoLexNode);
    CHECK(g.nlex == count + 2);
  }
  for (const auto shape : {Prefix::single, Prefix::sequence})
    for (const unsigned spare : {0u, 1u, 2u}) {
      auto g = grammar(shape, Form::optional);
      const auto root = g.termBuild[1].lex;
      g.lex.resize(kNoLexNode);
      g.nlex = static_cast<std::uint16_t>(kNoLexNode - spare);
      const auto count = g.nlex;
      const auto old = g.lex;
      const unsigned needed = shape == Prefix::single ? 1 : 2;
      cursiv::detail::LexRightLinear<Graph>{g}.run();
      if (spare < needed) {
        CHECK(g.nlex == count && g.lex == old);
      } else {
        CHECK(g.nlex == count + needed);
        CHECK(g.lex[root].kind == LexKind::call && g.lex[root].b == kNoLexNode);
      }
    }
}
}  // namespace

void run() {
  endpoint_differential();
  excluded_shapes();
  shared_roots_and_limits();
}

}  // namespace lex_right_linear_shape_cases

int main() {
  cursiv::test::count_expectations();
  recursive_terminals_cases::run();
  lex_right_linear_cases::run();
  lex_right_linear_shape_cases::run();
  return cursiv::test::report("RecursiveTerminals_test");
}
