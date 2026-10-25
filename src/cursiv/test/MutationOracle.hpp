#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/RecoveryLane.hpp"

// The mutation oracle: every one-byte mutation of a valid document is
// accepted, with an AST, a tree that reaches its last byte, and record ends
// that never decrease. What a repair is worth, where it is said included,
// is the recovery metric's to judge.

namespace cursiv::test {

inline constexpr char kMutationAlphabet[] =
    " \t\n{}[]()<>,;:=+-*/\"'@0123abcXY";

inline std::string mutation_name(char op, std::size_t pos, char byte) {
  std::string s = op == 'i' ? "ins" : op == 'd' ? "del" : "rep";
  s += "@" + std::to_string(pos);
  if (op == 'd') return s;
  char b[8];
  if (byte > 0x20 && byte < 0x7f)
    std::snprintf(b, sizeof b, ":%c", byte);
  else
    std::snprintf(b, sizeof b, ":x%02x", static_cast<unsigned char>(byte));
  return s + b;
}

// One mutated document held to the floor.
inline void mutation_floor(std::string_view lang, const cursiv::Grammar& grammar,
                           const cursiv::ParseResult& r, const std::string& doc,
                           const std::string& what) {
  const char* broken = nullptr;
  if (!r.cst().accepted()) {
    broken = "refused";
  } else if (r.value == nullptr) {
    broken = "accepted without an AST";
  } else {
    const Proj p = project(grammar, r.cst(), doc);
    if (!p.monotone)
      broken = "a record's end decreases along the tape";
    else if (!p.covered)
      broken = "the tree does not reach the last byte";
  }
  if (broken == nullptr) return;
  ++failures;
  std::printf("  FAIL %.*s %s: %s\n", static_cast<int>(lang.size()),
              lang.data(), what.c_str(), broken);
}

inline void mutation_oracle(const cursiv::Parser& engine, std::string_view lang,
                            const std::string& seed) {
  const Proj valid = project(engine, seed);
  if (!valid.accepted || valid.recovered) {
    std::printf("FAIL %.*s oracle: the seed is not taken cleanly\n",
                static_cast<int>(lang.size()), lang.data());
    ++failures;
    return;
  }
  const cursiv::Grammar& grammar = engine.getGrammar();
  const auto judge = [&](const std::string& doc, char op, std::size_t pos,
                         char byte) {
    mutation_floor(lang, grammar, engine.parse(doc), doc,
                   mutation_name(op, pos, byte));
  };
  for (std::size_t pos = 0; pos <= seed.size(); ++pos) {
    for (const char c : std::string_view(kMutationAlphabet)) {
      std::string doc = seed;
      doc.insert(doc.begin() + static_cast<long>(pos), c);
      judge(doc, 'i', pos, c);
    }
    if (pos >= seed.size()) continue;
    std::string doc = seed;
    doc.erase(doc.begin() + static_cast<long>(pos));
    judge(doc, 'd', pos, 0);
    for (const char c : std::string_view(kMutationAlphabet)) {
      if (c == seed[pos]) continue;
      std::string rep = seed;
      rep[pos] = c;
      judge(rep, 'r', pos, c);
    }
  }
}

}  // namespace cursiv::test
