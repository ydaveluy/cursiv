// What both generators say to the author on stderr, each led by the grammar's
// name.
#pragma once

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/gen/LexCheckIncomplete.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/UnboundedCondition.hpp"
#include "cursiv/shared/gen/stamp_split.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

// The dead branches, branches that repeat earlier ones, flags that may read
// nothing, repetitions of a run and literals that may stop inside a word that
// the rules of `reached` spell: a warning each.
void report_grammar_warnings(std::string_view grammar, GrammarImage& image,
                             std::span<const ImageRuleDesc* const> reached);

// Warns, in this order, of each condition of `conds` read to the end of an
// unbounded run, each lexical check of `lex` that could not conclude, then the
// warnings of `image`. `rule_name` names a rule by its index.
template <class CondCfg, class LexCfg, class RuleName>
void report_warnings(std::string_view grammar, GrammarImage& image,
                     std::span<const ImageRuleDesc* const> reached,
                     const CondCfg& conds, const LexCfg& lex,
                     RuleName rule_name) {
  const auto n = static_cast<int>(grammar.size());
  for (const UnboundedCondition& u : conds.unbounded)
    std::fprintf(stderr, "%.*s: warning: %s\n", n, grammar.data(),
                 unbounded_condition_text(conds, rule_name(u.rule), u).c_str());
  for (const LexCheckIncomplete& k : lex.lexIncomplete)
    std::fprintf(stderr, "%.*s: warning: %s\n", n, grammar.data(),
                 lex_check_incomplete_text(lex, k).c_str());
  report_grammar_warnings(grammar, image, reached);
}

// What a generator's arguments name: the directory it writes to, and the
// TextMate file `--textmate=<file>` names, empty where none does.
struct GenPaths {
  const char* dir = nullptr;
  std::string_view textmate;
};

// The paths a generator's arguments name. Other flags go to `option`, false
// where wrong; a wrong argument or no directory prints the usage, with the
// engine's `options`, and returns no directory.
template <class Option>
GenPaths gen_paths(int argc, char** argv, const char* options,
                   Option&& option) {
  constexpr std::string_view kTextMate = "--textmate=";
  GenPaths paths;
  bool bad = false;
  for (int i = 1; i < argc && !bad; ++i) {
    const std::string_view arg = argv[i];
    if (arg.starts_with(kTextMate))
      paths.textmate = arg.substr(kTextMate.size());
    else if (arg.starts_with("-"))
      bad = !option(arg);
    else
      paths.dir = argv[i];
  }
  if (!bad && paths.dir != nullptr) return paths;
  std::fprintf(stderr, "usage: %s %s [--textmate=<file>] <out-dir>\n", argv[0],
               options);
  return {};
}

// An arm that begins and ends with its own rule, a refusal each: true where
// there was one.
bool refuse_self_bounded_arms(std::string_view grammar, GrammarImage& image,
                              std::span<const ImageRuleDesc* const> reached);

// A terminal that matches the empty string is a token the lexer never hands
// back: `terminal` names the first one, empty where there is none.
bool refuse_empty_match(std::string_view grammar, std::string_view terminal);

// A fault the lowering or the build recorded: its code, then its sentence.
inline void refuse_fault(std::string_view grammar, const GrammarFault& fault,
                         const std::string& message) {
  const std::string_view code = grammar_fault_name(fault.code);
  std::fprintf(stderr, "%.*s: refused (%.*s)\n%s\n",
               static_cast<int>(grammar.size()), grammar.data(),
               static_cast<int>(code.size()), code.data(), message.c_str());
}

// A limit of the generated parser the grammar overran, `cap` saying which,
// refused under the grammar's name.
inline void refuse_too_large(std::string_view grammar, std::string_view cap) {
  std::fprintf(stderr,
               "%.*s: refused: the grammar is too large for the generated "
               "parser: %.*s -- simplify the grammar, or split it\n",
               static_cast<int>(grammar.size()), grammar.data(),
               static_cast<int>(cap.size()), cap.data());
}

// Splits a record's bits between `rows` and `states` into `stamp`, `forced`
// being the row width a lane asks for; true, with a refusal, where none fits.
inline bool refuse_unstamped(std::string_view grammar, StampLayout& stamp,
                             std::uint32_t rows, std::uint32_t states,
                             unsigned forced = 0) {
  if (const auto split = stamp_split(rows, states, forced)) {
    stamp = *split;
    return false;
  }
  refuse_too_large(grammar, stamp_refusal(rows, states, forced));
  return true;
}

// The first terminal whose match is empty: one a mode's start accepts, or a
// relay the start launches ends where its automaton cannot see.
template <class Built>
std::string_view empty_match_terminal(const Built& a) {
  const auto empty = lex_empty_nodes(a.cfg);
  for (std::uint32_t m = 0; m < a.nmodes; ++m) {
    const std::uint16_t start = a.modeStart[m];
    if (a.dfaAccept[start] != 0)
      return a.cfg.term_name(a.dfaAccept[start] - 1u);
    for (auto r = a.dfaRelay[start]; r != kNoRelay; r = a.relays[r].next)
      if (empty[a.cfg.termBuild[a.relays[r].term].lex])
        return a.cfg.term_name(a.relays[r].term);
  }
  return {};
}

// What the lowering or the build recorded as refused: its sentence, else the
// limit that overflowed. True where there was one.
template <class Built>
bool refuse_unbuilt(std::string_view grammar, const Built& a,
                    std::string_view rule) {
  const auto n = static_cast<int>(grammar.size());
  if (a.cfg.lowerWhy != LowerFail::none)
    std::fprintf(stderr, "%.*s: refused: %s\n", n, grammar.data(),
                 lower_fail_sentence(a.cfg, rule).c_str());
  else if (a.overflow)
    refuse_too_large(grammar, a.overflowCap
                                  ? a.overflowCap
                                  : "it overran a limit the generator does "
                                    "not name");
  else
    return false;
  return true;
}

}  // namespace cursiv::engine
