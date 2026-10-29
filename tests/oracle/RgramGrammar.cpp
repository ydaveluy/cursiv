#include "oracle/RgramGrammar.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "oracle/RgramRandom.hpp"

namespace cursiv {
namespace {

constexpr char kToks[] = {'a', 'b', 'c', 'd'};

struct Family {
  std::string_view name;
  RgramGrammar::Features features;
};
constexpr Family kFamilies[] = {
    {"conditions", {}},
    {"ast", {.ast = true}},
    {"regions+ast", {.ast = true, .regions = true}},
    {"regions", {.regions = true}},
    {"closed-regions+ast",
     {.ast = true, .regions = true, .closedRegions = true}},
    {"optional-runs+ast", {.ast = true, .optionalRuns = true}},
    {"commits+ast", {.ast = true, .commits = true}},
    {"all",
     {.ast = true,
      .regions = true,
      .closedRegions = true,
      .commits = true,
      .eofChoice = true}},
};

// A committed item opens on the token it commits to and has a body after it.
RgramExpr one(RgramKind kind, RgramExpr kid, bool commit = false) {
  RgramExpr e{.kind = kind, .committed = commit};
  if (commit) {
    const auto n = kid.kids.size();
    if (kid.kids.empty() || kid.kids[0].kind != RgramKind::tok)
      kid.kids.insert(kid.kids.begin(), {.kind = RgramKind::tok,
                                         .tok = kToks[n % 4]});
    if (kid.kids.size() < 2)
      kid.kids.push_back({.kind = RgramKind::tok, .tok = kToks[(n + 1) % 4]});
  }
  e.kids.push_back(std::move(kid));
  return e;
}

// A rule's first alternative calls only the rules after it, so every rule
// derives some text.
struct Drawer {
  RgramRandom r;
  RgramGrammar::Features f;
  unsigned k = 0;

  RgramExpr tok() { return {.kind = RgramKind::tok, .tok = kToks[r.below(4)]}; }

  RgramExpr cond() {
    constexpr RgramCond kinds[] = {
        RgramCond::notTok, RgramCond::andTok,   RgramCond::line,
        RgramCond::noLine, RgramCond::lineOrTok, RgramCond::notTok,
        RgramCond::none,   RgramCond::notNone,  RgramCond::lineOrEof};
    const char x = kToks[r.below(4)];
    return {.kind = RgramKind::cond, .tok = x, .cond = kinds[r.below(9)]};
  }

  RgramExpr item(unsigned rule, unsigned depth, bool base) {
    double w = r.unit();
    if (depth > 2) w = std::min(w, 0.5);
    if (w < 0.36 || (base && w < 0.55)) {
      RgramExpr t = tok();
      if (f.ast && r.unit() < 0.35) t.kind = RgramKind::tokNode;
      return t;
    }
    if (w < 0.55) {
      if (!base) return {.kind = RgramKind::ref, .ref = r.below(k)};
      if (rule + 1 >= k) return tok();
      return {.kind = RgramKind::ref, .ref = r.range(rule + 1, k - 1)};
    }
    const bool commit = f.commits && r.unit() < 0.4;
    if (w < 0.66) return one(RgramKind::opt, seq(rule, depth + 1, base), commit);
    if (w < 0.74)
      return one(RgramKind::many, seq(rule, depth + 1, base), commit);
    if (w < 0.79)
      return one(RgramKind::some, seq(rule, depth + 1, base), commit);
    if (w < 0.83) return one(RgramKind::sep, seq(rule, depth + 1, base));
    if (w < 0.86) {
      const unsigned lo = r.range(0, 2);
      RgramExpr e = one(RgramKind::rep, seq(rule, depth + 1, base));
      e.lo = lo;
      e.hi = r.range(std::max(lo, 1u), 3);
      return e;
    }
    if (w < 0.97 && f.conditions) {
      if (f.eofChoice && r.unit() < 0.3) {
        RgramExpr end{.kind = RgramKind::seq};
        end.kids.push_back({.kind = RgramKind::cond, .cond = RgramCond::eof});
        RgramExpr e{.kind = RgramKind::alt};
        e.kids.push_back(std::move(end));
        e.kids.push_back(seq(rule, depth + 1, base));
        return e;
      }
      if (f.regions && r.unit() < 0.3) {
        RgramExpr body = seq(rule, depth + 1, base);
        RgramExpr inner{.kind = RgramKind::seq};
        inner.kids.push_back(tok());
        for (RgramExpr& x : body.kids) inner.kids.push_back(std::move(x));
        if (f.closedRegions) inner.kids.push_back(tok());
        return one(RgramKind::skip, std::move(inner));
      }
      return cond();
    }
    RgramExpr e{.kind = RgramKind::alt};
    e.kids.push_back(seq(rule, depth + 1, base));
    e.kids.push_back(seq(rule, depth + 1, base));
    return e;
  }

  RgramExpr seq(unsigned rule, unsigned depth, bool base) {
    RgramExpr s{.kind = RgramKind::seq};
    if (f.optionalRuns && depth == 0 && r.unit() < 0.5) {
      const unsigned n = r.range(8, 12);
      for (unsigned i = 0; i < n; ++i) {
        RgramExpr x = item(rule, 2, base);
        if (x.kind != RgramKind::cond && r.unit() < 0.75) {
          RgramExpr run{.kind = RgramKind::seq};
          run.kids.push_back(std::move(x));
          s.kids.push_back(one(RgramKind::opt, std::move(run)));
        } else {
          s.kids.push_back(std::move(x));
        }
      }
      return s;
    }
    const unsigned n = r.range(1, depth < 2 ? 3 : 2);
    for (unsigned i = 0; i < n; ++i) s.kids.push_back(item(rule, depth, base));
    return s;
  }

  std::vector<RgramExpr> rules() {
    std::vector<RgramExpr> out;
    for (unsigned i = 0; i < k; ++i) {
      RgramExpr alts{.kind = RgramKind::alt};
      alts.kids.push_back(seq(i, 0, true));
      for (unsigned j = r.range(0, 2); j > 0; --j)
        alts.kids.push_back(seq(i, 0, false));
      if (f.conditions && r.unit() < 0.15)
        alts.kids[r.below(static_cast<unsigned>(alts.kids.size()))]
            .kids.push_back({.kind = RgramKind::cond, .cond = RgramCond::eof});
      out.push_back(std::move(alts));
    }
    return out;
  }
};

bool nullable(const RgramExpr& e, const std::vector<char>& null) {
  switch (e.kind) {
    case RgramKind::tok:
    case RgramKind::tokNode:
      return false;
    case RgramKind::ref:
      return null[e.ref] != 0;
    case RgramKind::opt:
    case RgramKind::many:
    case RgramKind::sep:
    case RgramKind::cond:
      return true;
    case RgramKind::some:
    case RgramKind::skip:
      return nullable(e.kids[0], null);
    case RgramKind::rep:
      return e.lo == 0 || nullable(e.kids[0], null);
    case RgramKind::seq:
      return std::ranges::all_of(
          e.kids, [&](const RgramExpr& x) { return nullable(x, null); });
    case RgramKind::alt:
      return std::ranges::any_of(
          e.kids, [&](const RgramExpr& x) { return nullable(x, null); });
  }
  return false;
}

std::vector<char> nullable_rules(const std::vector<RgramExpr>& rules) {
  std::vector<char> null(rules.size(), 0);
  for (bool moved = true; moved;) {
    moved = false;
    for (std::size_t i = 0; i < rules.size(); ++i)
      if (null[i] == 0 && nullable(rules[i], null)) null[i] = 1, moved = true;
  }
  return null;
}

// A repetition of what may match nothing reads one text many ways, which the
// generator refuses: such a body opens on a token.
void fix(RgramExpr& e, const std::vector<char>& null, Drawer& d) {
  switch (e.kind) {
    case RgramKind::tok:
    case RgramKind::tokNode:
    case RgramKind::ref:
    case RgramKind::cond:
      return;
    case RgramKind::skip:
      fix(e.kids[0], null, d);
      return;
    case RgramKind::opt:
    case RgramKind::many:
    case RgramKind::some:
    case RgramKind::sep:
    case RgramKind::rep:
      fix(e.kids[0], null, d);
      if (nullable(e.kids[0], null))
        e.kids[0].kids.insert(e.kids[0].kids.begin(), d.tok());
      return;
    case RgramKind::seq:
    case RgramKind::alt:
      for (RgramExpr& x : e.kids) fix(x, null, d);
      return;
  }
}

std::string cond_text(const RgramExpr& e) {
  const std::string lit = std::string("lit<\"") + e.tok + "\">";
  switch (e.cond) {
    case RgramCond::notTok:
      return "(!" + lit + ")";
    case RgramCond::andTok:
      return "(&" + lit + ")";
    case RgramCond::line:
      return "gap::line";
    case RgramCond::noLine:
      return "(!gap::line)";
    case RgramCond::lineOrTok:
      return "(gap::line | &" + lit + ")";
    case RgramCond::none:
      return "gap::none";
    case RgramCond::notNone:
      return "(!gap::none)";
    case RgramCond::lineOrEof:
      return "(gap::line | eof)";
    case RgramCond::eof:
      return "eof";
  }
  return "eof";
}

std::string text(const RgramExpr& e, bool ast) {
  const auto joined = [&](std::string_view sep) {
    if (e.kids.size() == 1) return text(e.kids[0], ast);
    std::string out = "(";
    for (std::size_t i = 0; i < e.kids.size(); ++i) {
      if (i != 0) out += sep;
      out += text(e.kids[i], ast);
    }
    return out + ")";
  };
  const auto call = [&](std::string_view name) {
    if (!e.committed)
      return std::string(name) + "(" + text(e.kids[0], ast) + ")";
    RgramExpr rest{.kind = RgramKind::seq};
    rest.kids.assign(e.kids[0].kids.begin() + 1, e.kids[0].kids.end());
    return std::string(name) + "(commit(" + text(e.kids[0].kids[0], ast) +
           ", " + text(rest, ast) + "))";
  };
  const std::string ref = "ref<R" + std::to_string(e.ref) + ">";
  switch (e.kind) {
    case RgramKind::tok:
      return std::string("lit<\"") + e.tok + "\">";
    case RgramKind::tokNode:
      return std::string("append<&ast::Node::kids>(ref<T") + e.tok + ">)";
    case RgramKind::ref:
      return ast ? "append<&ast::Node::kids>(" + ref + ")" : ref;
    case RgramKind::opt:
      return call("option");
    case RgramKind::many:
      return call("many");
    case RgramKind::some:
      return call("some");
    case RgramKind::sep:
      return "many(" + text(e.kids[0], ast) + ", lit<\",\">)";
    case RgramKind::rep:
      return "repeat<" + std::to_string(e.lo) + ", " + std::to_string(e.hi) +
             ">(" + text(e.kids[0], ast) + ")";
    case RgramKind::cond:
      return cond_text(e);
    case RgramKind::skip:
      return "tight(" + text(e.kids[0], ast) + ")";
    case RgramKind::seq:
      return joined(" + ");
    case RgramKind::alt:
      return joined(" | ");
  }
  return "";
}

}  // namespace

RgramGrammar RgramGrammar::draw(unsigned seed) {
  constexpr auto kCount = sizeof(kFamilies) / sizeof(kFamilies[0]);
  const Family& family = kFamilies[seed % kCount];
  RgramGrammar g{.seed = seed, .family = family.name,
                 .features = family.features};
  Drawer d{.r = {.state = std::uint64_t{seed} * 0x2545f4914f6cdd1dull},
           .f = family.features};
  d.k = d.r.range(2, 5);
  g.rules = d.rules();
  for (int pass = 0; pass < 2; ++pass) {
    const std::vector<char> null = nullable_rules(g.rules);
    for (RgramExpr& e : g.rules) fix(e, null, d);
  }
  return g;
}

std::string RgramGrammar::header() const {
  const std::string ns = "rgram" + std::to_string(seed);
  const bool ast = features.ast;
  std::string out =
      "// Drawn by rgram_write from seed " + std::to_string(seed) + " (" +
      std::string(family) +
      ").\n"
      "#pragma once\n"
      "#include \"cursiv/core/grammar/GrammarSpec.hpp\"\n"
      "#include \"cursiv/core/syntax-tree/AstNode.hpp\"\n"
      "namespace " + ns + " {\nnamespace ast {\n";
  if (ast) {
    out += "struct Node : cursiv::Extends<cursiv::AstNode> { vector<Node*> kids; };\n";
    for (std::size_t i = 0; i < rules.size(); ++i)
      out += "struct N" + std::to_string(i) + " : cursiv::Extends<Node> {};\n";
    for (const char x : kToks)
      out += std::string("struct T") + x + " : cursiv::Extends<Node> {};\n";
  } else {
    out += "struct Doc : cursiv::Extends<cursiv::AstNode> {};\n";
  }
  out += "}  // namespace ast\nstruct G : cursiv::GrammarSpec {\n"
         "  struct WS : terminal<some(s)> {};\n";
  for (std::size_t i = 0; i < rules.size(); ++i)
    out += "  struct R" + std::to_string(i) + ";\n";
  if (ast)
    for (const char x : kToks)
      out += std::string("  struct T") + x + " : rule<create<ast::T" + x +
             ">(lit<\"" + x + "\">)> {};\n";
  for (std::size_t i = rules.size(); i-- > 0;) {
    const std::string body = text(rules[i], ast);
    out += "  struct R" + std::to_string(i) + " : rule<" +
           (ast ? "create<ast::N" + std::to_string(i) + ">(" + body + ")"
                : body) +
           "> {};\n";
  }
  if (ast) {
    out += "  using entry = R0;\n";
  } else {
    out += "  struct Top : rule<create<ast::Doc>(ref<R0>)> {};\n"
           "  using entry = Top;\n";
  }
  out += "  using ignored = WS;\n};\n}  // namespace " + ns + "\n";
  return out;
}

}  // namespace cursiv
