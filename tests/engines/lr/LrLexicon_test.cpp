#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alikereduced/generated/Reduced/Module.hpp"
#include "alikereduced/generated/Reduced/Table.hpp"
#include "backref/generated/G/Parser.hpp"
#include "backref/generated/G/Table.hpp"
#include "candidateruns/generated/G/Module.hpp"
#include "candidateruns/generated/G/Parser.hpp"
#include "candidateruns/generated/G/Table.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/lr_lex_reading.hpp"
#include "cursiv/engines/lr/runtime/lr_lookahead.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_rescan.hpp"
#include "cursiv/engines/shared/gen/CLiteral.hpp"
#include "cursiv/engines/shared/runtime/AstConvThunks.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/AlikeCandidatesGrammar.hpp"
#include "engines/lr/AuditGrammar.hpp"
#include "engines/lr/KeywordHandoffGrammar.hpp"
#include "engines/lr/LexGuardGrammars.hpp"
#include "engines/lr/MemConvGrammar.hpp"
#include "engines/lr/ParseChecks.hpp"
#include "engines/lr/RefusedHeadWordsGrammar.hpp"
#include "engines/lr/RefusedShorterGrammar.hpp"
#include "engines/lr/ReservedChoiceGrammar.hpp"
#include "engines/lr/TypoAlternativesGrammar.hpp"
#include "engines/lr/WideTerminalsGrammar.hpp"
#include "guard/generated/G/Module.hpp"
#include "guard/generated/G/Parser.hpp"
#include "guard/generated/G/Table.hpp"
#include "guardtrivia/generated/G/Module.hpp"
#include "guardtrivia/generated/G/Table.hpp"

#include "lraudit/generated/G/Module.hpp"
#include "lraudit/generated/G/Table.hpp"
#include "astfusedlr/generated/Lr/Module.hpp"
#include "onemode/generated/G/Module.hpp"
#include "onemode/generated/G/Parser.hpp"
#include "onemode/generated/G/Table.hpp"
#include "relayoverlap/generated/G/Module.hpp"
#include "relayoverlap/generated/G/Parser.hpp"
#include "relayoverlap/generated/G/Table.hpp"
#include "rulerefusal/generated/G/Module.hpp"
#include "shorterlr/generated/Lr/Module.hpp"
#include "shorterlr/generated/Lr/Parser.hpp"
#include "shorterlr/generated/Lr/Table.hpp"
#include "wideterms/generated/G/Module.hpp"

#ifdef CURSIV_LR_LEXER_CONTRACT
#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#endif

// The lexer's tables: candidate runs, typo alternatives, guards and
// conversions, wide terminal sets and converters that read memory.

namespace candidate_runs {

namespace {

namespace gen = tcandrun::generated::G;

void candidates(std::string text, unsigned from, unsigned end,
                std::vector<std::string_view> names) {
  const auto& a = gen::kTable;
  const auto mode = a.mode[0];
  CHECK(a.modeMulti[mode] != 0);
  std::vector<cursiv::Scan> room(a.maxCands);
  const auto n =
      gen::kDrive.scanText(text.data(), static_cast<unsigned>(text.size()),
                           mode, from, room.data(), a.maxCands);
  std::vector<std::string_view> got;
  for (unsigned k = 0; k < n; ++k) {
    CHECK(room[k].end == end);
    got.push_back(a.cfg.term_name(room[k].term));
  }
  std::sort(got.begin(), got.end());
  std::sort(names.begin(), names.end());
  CHECK(got == names);
}

}  // namespace

void run() {
  const auto parser = gen::create_parser();
  for (const unsigned from : {0u, 1u}) {
    const std::string blanks(from, ' ');
    for (const unsigned count : {1u, 2u, 3u, 4u, 128u})
      for (const char closer : {'!', '?'}) {
        const auto result =
            parser->parse(blanks + std::string(count, 'a') + closer);
        CHECK(result.accepted && result.diagnostics.empty());
      }
    for (const unsigned count : {0u, 1u, 2u})
      for (const char closer : {'!', '?'}) {
        const auto result = parser->parse(
            blanks + '"' + std::string(count, 'x') + '"' + closer);
        CHECK(result.accepted && result.diagnostics.empty());
      }
    const std::string prefix(from, '#');
    for (const unsigned count : {1u, 7u, 8u, 9u, 15u, 16u, 17u, 63u, 128u}) {
      const std::string word(count, 'a');
      for (const unsigned byte : {0u, 33u, 58u, 97u, 122u, 127u, 128u, 255u}) {
        const unsigned end = from + count + (byte >= 'a' && byte <= 'z');
        candidates(prefix + word + static_cast<char>(byte), from, end,
                   {"Word", "Twin"});
      }
      candidates(prefix + word, from, from + count, {"Word", "Twin"});
      candidates(prefix + word + ":x", from, from + count, {"Word", "Twin"});
      candidates(prefix + word + "::", from, from + count + 2, {"Longer"});
      const std::string guarded = prefix + '@' + std::string(count, 'g');
      candidates(guarded, from, from + count + 1, {"Bounded", "OtherBounded"});
      candidates(guarded + ';', from, from + count,
                 count == 1 ? std::vector<std::string_view>{}
                            : std::vector<std::string_view>{"Bounded", "OtherBounded"});
      for (unsigned byte = 0; byte < 256; ++byte) {
        if (byte == '"') continue;
        const std::string quoted =
            prefix + '"' + std::string(count, static_cast<char>(byte));
        candidates(quoted + '"', from, from + count + 2,
                   {"Quoted", "OtherQuoted"});
        candidates(quoted, from, 0, {});
      }
    }
    candidates(prefix + "while", from, from + 5, {"while"});
    candidates(prefix + "while!", from, from + 5, {"while"});
    candidates(prefix + "whilex", from, from + 6, {"Word", "Twin"});
  }
}

}  // namespace candidate_runs

// A lexical edit has no knowledge of the syntax after its span. Recovery
// retains its alternatives and elects the one that preserves that syntax.
namespace typo_alternatives {

namespace g = tguard::generated::G;

namespace {
void one_edit(const cursiv::Parser& parser, const char* text,
               cursiv::TextOffset begin, cursiv::TextOffset end) {
  const std::string source(text);
  const auto result = parser.parse(source);
  CHECK(result.accepted);
  CHECK(result.value != nullptr);
  CHECK(result.diagnostics.size() == 1);
  if (result.diagnostics.size() == 1) {
    const auto& d = result.diagnostics.front();
    CHECK(d.kind == cursiv::DiagnosticKind::replaced);
    CHECK(d.begin == begin);
    CHECK(d.end == end);
  }
  if (const auto* doc = result.value_as<ttyporead::Doc>())
    CHECK(doc->value == std::string_view(source).substr(begin, end - begin));
}

void the_continuation_decides_the_repaired_span() {
  const auto parser = g::create_parser();
  for (const char* text : {"abc;", "field tail", "[word!", "[word?"}) {
    const auto result = parser->parse(text);
    CHECK(result.accepted);
    CHECK(result.diagnostics.empty());
  }
  one_edit(*parser, "ab;", 0, 2);          // keep the following semicolon
  one_edit(*parser, "abx;", 0, 3);         // replace the terminal's last byte
  one_edit(*parser, "fiel= tail", 0, 5);   // consuming '=' avoids a second edit
  one_edit(*parser, "[worx?", 1, 5);       // the second same-spelling terminal
}

// Every distinct re-reading is a candidate, ranked: fewer faults first, then
// the longer span. The walk bounds how many there are, not the room.
void every_distinct_reading_is_a_ranked_candidate() {
  const std::string text = "ab;";
  cursiv::Cst cst;
  cursiv::CstBuilder builder(cst);
  cursiv::LrContext c(text, builder);
  c.drive = &g::kDrive;
  std::uint64_t read = 0;
  bool cut = false;
  const auto candidates = cursiv::lr_rescan_candidates<g::kTable>(
      c, 0, 0, static_cast<cursiv::TextOffset>(text.size()), read, cut);
  CHECK(candidates.size() > 1);
  for (std::size_t i = 1; i < candidates.size(); ++i) {
    const auto& a = candidates[i - 1];
    const auto& b = candidates[i];
    CHECK(a.faults < b.faults || (a.faults == b.faults && a.end >= b.end));
    CHECK(a.term != b.term || a.end != b.end);
  }
}
}  // namespace

void run() {
  the_continuation_decides_the_repaired_span();
  every_distinct_reading_is_a_ranked_candidate();
}

}  // namespace typo_alternatives

namespace audit_regression {

void run() {
  auto parser = taudit::generated::G::create_parser();
  for (const auto text :
       {"relaxed_ok", "reservedx", "reserved_ok", "united_ok", "relaxed;ok"}) {
    const std::string_view src = text;
    const auto scan = cursiv::lr_match_text<taudit::generated::G::kTable>(
        src.data(), src.size(), taudit::generated::G::kTable.errMode, 0);
    const bool id = parser->getGrammar().terminalName(scan.term) == "ID";
    CHECK(id);
  }
  for (const std::string text :
       {"owned ownedword", "slot slotword", "bare sharedword"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto at = static_cast<cursiv::TextOffset>(text.find(' ') + 1);
    const auto word = std::string_view(text).substr(at);
    bool found = false;
    for (const auto& f : checks::features(*parser, r, at, at)) {
      const auto spellings = parser->getGrammar().terminalSpellings(f.feature);
      if (std::ranges::find(spellings, word) == spellings.end()) continue;
      found = true;
      CHECK(f.property == (text.starts_with("bare") ? "" : "text"));
      if (text.starts_with("bare"))
        CHECK(f.type == nullptr && f.narrow == cursiv::kNoNarrow);
    }
    CHECK(found);
  }
  for (const auto text :
       {"reserve other", "reserve reservedx", "reserve relaxed_ok"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
  }
  for (const auto text :
       {"reserve reserved", "reserve reserved_ok", "reserve relaxed",
        "reserve relaxed;ok", "reserve united", "reserve united_ok",
        "reserve united;ok"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::replaced);
  }
  for (const auto text :
       {"upcab", "uqcab", "upcz", "uqcz", "rx cad", "guards a_z"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
  }
  for (const auto text : {"[ ]", "{~}", "( )"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value != nullptr);
    if (r.value) {
      const auto* doc = r.value_as<taudit::Doc>();
      const auto* held = text[0] == '(' ? doc->child : doc;
      CHECK(held && held->text == std::string_view(text + 1, 1));
    }
  }
  {
    const auto r = parser->parse("f");
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value != nullptr);
    if (r.value) CHECK(r.value_as<taudit::Doc>()->text.empty());
    const auto invalid = parser->parse("fe");
    CHECK(invalid.accepted && invalid.diagnostics.size() == 1);
  }
  {
    const auto r = parser->parse("tree n");
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value != nullptr);
    if (r.value) {
      const auto* doc = r.value_as<taudit::Doc>();
      CHECK(doc->child && doc->child->child);
      if (doc->child && doc->child->child) {
        CHECK(doc->child->parent == doc);
        CHECK(doc->child->child->parent == doc->child);
      }
    }
  }
  {
    taudit::conversions = 0;
    const auto r = parser->parse("join j j");
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value != nullptr);
    if (r.value) CHECK(r.value_as<taudit::Doc>()->text == "12");
    CHECK(taudit::conversions == 2);
    taudit::conversions = 0;
    const auto single = parser->parse("single j");
    CHECK(single.accepted && single.diagnostics.empty());
    CHECK(single.value && single.value_as<taudit::Doc>()->text == "1");
    CHECK(taudit::conversions == 1);
    taudit::conversions = 0;
    const auto wrapped = parser->parse("wrapped j");
    CHECK(wrapped.accepted && wrapped.diagnostics.empty());
    CHECK(wrapped.value && wrapped.value_as<taudit::Doc>()->text == "wrapped");
    CHECK(taudit::conversions == 1);
    const auto singlefail = parser->parse("singlefail bad");
    CHECK(singlefail.accepted && !singlefail.diagnostics.empty());
    CHECK(singlefail.value &&
          singlefail.value_as<taudit::Doc>()->text == "unchanged");
    const auto constants = parser->parse("const k k");
    CHECK(constants.accepted && constants.diagnostics.empty());
    CHECK(constants.value != nullptr);
    if (constants.value)
      CHECK(constants.value_as<taudit::Doc>()->text == "constantconstant");
    const auto failed = parser->parse("fail bad bad");
    CHECK(failed.accepted && !failed.diagnostics.empty());
    CHECK(failed.value != nullptr);
    if (failed.value)
      CHECK(failed.value_as<taudit::Doc>()->text == "unchanged");
  }
  for (const auto text : {"partdirect part", "partjoined part"}) {
    taudit::conversions = 0;
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value && r.value_as<taudit::Doc>()->text == "converted part");
    CHECK(taudit::conversions == 1);
  }
  {
    auto r = parser->parse("flag");
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(r.value && r.value_as<taudit::Doc>()->flagged);
    parser.reset();
    const auto* grammar = r.cst().grammar();
    CHECK(grammar != nullptr);
    if (grammar) {
      CHECK(grammar->spellingDoc("fLaG") == "flag documentation");
      bool found = false;
      for (cursiv::NodeId i = 0; i < r.cst().size(); ++i)
        found = found || grammar->recordWrites(r.cst()[i], "flagged") ||
                grammar->recordFills(r.cst()[i], "flagged");
      CHECK(found);
      constexpr std::string_view binary{"a\0b", 3};
      CHECK(grammar->spellingDoc(binary) == "binary literal");
    }
  }
}

}  // namespace audit_regression

// A grammar of more terminals than a fixed set of eight words holds builds
// and reads its words.
namespace wide_terminals {

void run() {
  const auto p = twide::generated::G::create_parser();
  const cursiv::ParseResult r = p->parse("k000 k519 k300");
  CHECK(r.accepted && r.diagnostics.empty());
  const auto* const doc = r.value_as<twide::ast::Doc>();
  CHECK(doc != nullptr && doc->words.size() == 3 && doc->words[1] == "k519");
  CHECK(!p->parse("k520").diagnostics.empty());
}

}  // namespace wide_terminals

// A terminal means the same thing wherever it stands: assigned on its own, or
// joined as one part of a data rule. A converter that asks for a resource to
// write its answer into is the same converter as one that does not.
namespace mem_conv {

namespace {

cursiv::ConversionResult<std::string_view> borrowed;
using BorrowedConverter =
    cursiv::detail::text_conv<[]() -> auto& { return borrowed; }>;
static_assert(cursiv::detail::DenotesText<BorrowedConverter>);
cursiv::ConversionResult<std::string> borrowedString;
using BorrowedStringConverter =
    cursiv::detail::text_conv<[]() -> auto& { return borrowedString; }>;

void borrowed_conversion_results_are_not_consumed() {
  std::vector<cursiv::ParseDiagnostic> diagnostics;
  cursiv::AstBuildCtx ctx;
  ctx.diags = &diagnostics;
  borrowed = decltype(borrowed)::err("retained conversion failure");
  for (unsigned i = 0; i < 2; ++i) {
    const auto text =
        cursiv::detail::denoted_text<BorrowedConverter>(ctx, cursiv::kNoNode);
    CHECK(!text);
  }
  CHECK(diagnostics.size() == 2);
  for (const auto& diagnostic : diagnostics)
    CHECK(diagnostic.message == "retained conversion failure");
  CHECK(borrowed.error.message == "retained conversion failure");

  borrowed = decltype(borrowed)::ok("retained value");
  for (unsigned i = 0; i < 2; ++i) {
    const auto text =
        cursiv::detail::denoted_text<BorrowedConverter>(ctx, cursiv::kNoNode);
    CHECK(text && *text == "retained value");
    CHECK(borrowed && *borrowed.value == "retained value");
  }
  CHECK(diagnostics.size() == 2);

  borrowedString = decltype(borrowedString)::ok("retained owned value");
  for (unsigned i = 0; i < 2; ++i) {
    std::string value;
    const bool converted =
        cursiv::detail::invoke_conv<BorrowedStringConverter, std::string, 0,
                                    false>(ctx, cursiv::kNoNode, &value);
    CHECK(converted && value == "retained owned value");
    CHECK(borrowedString && *borrowedString.value == "retained owned value");
  }
}

}  // namespace

void run() {
  borrowed_conversion_results_are_not_consumed();
  const auto engine = tfused::generated::Lr::create_parser();
  const std::string text = "'ab' 'cd' 'ef'";
  const cursiv::ParseResult r = engine->parse(text);
  CHECK(r.accepted && r.diagnostics.empty());
  const auto* const doc = r.value_as<tmemconv::ast::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->direct == "ab");
  // The join is what the parts MEAN, so the quotes are off there too.
  CHECK(doc->joined == "cdef");
  std::printf("  direct=`%.*s` joined=`%.*s`\n",
              static_cast<int>(doc->direct.size()), doc->direct.data(),
              static_cast<int>(doc->joined.size()), doc->joined.data());
}

}  // namespace mem_conv

#ifdef CURSIV_LR_LEXER_CONTRACT
namespace case_lexercontract {
// Three lexer entry points share one contract: the walk over the frozen table,
// the walk the generated drive calls, and the sweep that holds a position's
// rivals. These comparisons also cover the repair's reading of a token
// refused in its middle, without relying only on whole parses to exercise
// each path.

namespace {

using cursiv::Scan;
using cursiv::TextOffset;

// The sweep, where the grammar has one: every terminal this position reads,
// and it answers ONE extent -- the same one the walk answers.
using Sweep = std::uint32_t (*)(const char*, TextOffset, std::uint16_t,
                                TextOffset, Scan*, std::uint32_t);

// The bytes a grammar's own text is made of, plus -- one draw in five -- any
// byte at all, so the dead edges of the automaton are walked too.
struct Draw {
  std::mt19937 bits;
  std::string_view alpha;
  unsigned char byte() {
    return bits() % 5u == 0
               ? static_cast<unsigned char>(bits())
               : static_cast<unsigned char>(alpha[bits() % alpha.size()]);
  }
};

bool same(Scan x, Scan y) { return x.end == y.end && x.term == y.term; }

// One byte per class the mode tells apart: classes that every state it
// reaches moves on alike and every boundary it tests holds alike are one, so
// the other cases of a table split no word of this mode. What a relay or a
// decoding test reads past the moves keeps its classes whole.
template <const auto& A>
std::string mode_alphabet(unsigned m) {
  std::string rep;
  for (unsigned k = 0; k < A.ncls; ++k)
    for (unsigned b = 0; b < 256; ++b)
      if (A.cls[b] == k) {
        rep.push_back(static_cast<char>(b));
        break;
      }
  std::vector<std::uint16_t> reach{A.modeStart[m]}, tests;
  std::vector<char> seen(A.dfaAccept.size(), 0), whole(A.ncls, 0);
  seen[A.modeStart[m]] = 1;
  const auto enter = [&](std::uint16_t to) {
    if (to == cursiv::kDeadState || seen[to] != 0) return;
    seen[to] = 1;
    reach.push_back(to);
  };
  const auto keep = [&](std::uint16_t off, std::uint16_t len) {
    for (unsigned j = 0; j < len; ++j)
      whole[A.cls[static_cast<unsigned char>(A.cfg.names[off + j])]] = 1;
  };
  for (std::size_t i = 0; i < reach.size(); ++i) {
    const std::uint16_t s = reach[i];
    for (unsigned t = 0; t < A.cfg.nterms; ++t)
      if (A.dfa_acc_set(s).test(t) && A.dfa_acc_bound(s, t) != cursiv::kNoSet)
        tests.push_back(A.dfa_acc_bound(s, t));
    if constexpr (A.relays.size() != 0)
      for (auto k = A.dfaRelay[s]; k != cursiv::kNoRelay;
           k = A.relays[k].next) {
        const auto& r = A.relays[k];
        tests.push_back(r.markSet);
        keep(r.beforeOff, r.beforeLen);
        keep(r.afterOff, r.afterLen);
        enter(r.resume);
      }
    for (const char c : rep) enter(A.next_byte(s, static_cast<unsigned char>(c)));
  }
  for (const std::uint16_t k : tests)
    if (A.cfg.setDecode[k] != cursiv::kNoSet)
      for (unsigned b = 0x80; b < 256; ++b) whole[A.cls[b]] = 1;
  std::vector<std::vector<std::uint32_t>> kinds;
  std::string out;
  for (const char c : rep) {
    const auto b = static_cast<unsigned char>(c);
    std::vector<std::uint32_t> kind;
    for (const std::uint16_t s : reach) kind.push_back(A.next_byte(s, b));
    for (const std::uint16_t k : tests) kind.push_back(A.cfg.sets[k].has(b));
    kind.push_back(A.cfg.wordBytes.has(b));
    if (whole[A.cls[b]] != 0) kind.push_back(0x10000u + A.cls[b]);
    if (std::ranges::find(kinds, kind) != kinds.end()) continue;
    kinds.push_back(std::move(kind));
    out.push_back(c);
  }
  return out;
}

// One mode, the drive's walk and sweep against `lr_match_text`.
template <const auto& A>
void mode_agrees(const char* lang, unsigned m, const cursiv::LrDrive& drive) {
  const Sweep sweep = drive.scanText;
  const auto shared = [&](const char* s, TextOffset len, TextOffset from) {
    return cursiv::lr_match_text<A>(s, len, static_cast<std::uint16_t>(m),
                                    from);
  };
  // What the mode's states act on, and trivia.
  std::vector<char> reads(A.cfg.nterms, 0);
  for (unsigned st = 0; st < A.nstates; ++st)
    for (unsigned t = 0; t < A.cfg.nterms; ++t)
      if (A.mode[st] == m &&
          (A.cold.follow[st].test(t) || A.cfg.terms[t].trivia))
        reads[t] = 1;
  const auto one = [&](const char* s, TextOffset len, TextOffset from) {
    TextOffset halt = 0;
    const Scan want = cursiv::lr_match_halt<A>(
        s, len, static_cast<std::uint16_t>(m), from, halt);
    bool ok =
        same(drive.walk(s, len, static_cast<std::uint16_t>(m), from), want);
    // A terminal is the unit, so the sweep holds ONE extent, and it is the
    // walk's. A refusal is not a lexical failure, so the sweep may answer
    // where the walk did not -- never at another extent.
    if (sweep != nullptr) {
      std::vector<Scan> room(A.maxCands != 0 ? A.maxCands : 1);
      const std::uint32_t got = sweep(s, len, static_cast<std::uint16_t>(m),
                                      from, room.data(), A.maxCands);
      for (std::uint32_t k = 1; k < got; ++k)
        ok = ok && room[k].end == room[0].end;
      if (got != 0 && want.term != cursiv::kInvalidSymbol) {
        ok = ok && room[0].end == want.end;
        // The walk's own answer is one of the readings the sweep holds, or
        // the drive would elect a token its rivals disown.
        bool held = false;
        for (std::uint32_t k = 0; k < got; ++k)
          held = held || room[k].term == want.term;
        ok = ok && held;
      }
      // Nothing to read is nothing to read, in both forms.
      if (want.term == cursiv::kInvalidSymbol || want.end == from)
        ok = ok && got == 0;
      // A rival the mode reads stands only behind a winner its mode flags:
      // the drive sweeps behind those alone.
      if (want.term < A.cfg.nterms && !A.cfg.terms[want.term].trivia)
        for (std::uint32_t k = 0; k < got; ++k)
          if (room[k].term != want.term && reads[room[k].term] != 0)
            ok = ok && A.modeWin[m].test(want.term);
    }
    // `halt` is what a caller splicing text reads the answer's finality from:
    // stopped before the end means the bytes past it were never looked at, so
    // cutting there answers the same.
    ok = ok && halt <= len;
    if (halt < len) ok = ok && same(shared(s, halt + 1, from), want);
    if (!ok) {
      const auto say = [&](const char* who, Scan v) {
        std::printf("  %-8s end=%u term=%u\n", who, v.end, v.term);
      };
      std::printf("FAIL %s mode %u at %u of '%.*s'\n", lang, m, from,
                  static_cast<int>(len), s);
      std::printf("  bytes:");
      for (TextOffset k = 0; k < len; ++k)
        std::printf(" %02x", static_cast<unsigned char>(s[k]));
      std::printf("\n");
      say("shared", want);
      say("walk", drive.walk(s, len, static_cast<std::uint16_t>(m), from));
      if (sweep != nullptr) {
        std::vector<Scan> room(A.maxCands != 0 ? A.maxCands : 1);
        const std::uint32_t got = sweep(s, len, static_cast<std::uint16_t>(m),
                                        from, room.data(), A.maxCands);
        for (std::uint32_t k = 0; k < got; ++k) say("sweep", room[k]);
      }
      ++failures;
    }
    return ok;
  };

  // The empty document, and a cursor already at the end: both answer that
  // nothing matches, and both are positions a repair really reads.
  CHECK(one("", 0, 0));
  CHECK(one("ab", 2, 2));

  if constexpr (A.relays.size() != 0) {
    std::vector<std::string> cases = {
        "#<abc-abc>!", "#<abc-abc>?", "#<abc-abc>_", "#<abc-abd>!",
        "#@abc:abc! bodyabc?", "#@:body!", "#@abc:abc?bodyabc!",
        "#~aaa!", "#~aaaaa!", "#~aa!"};
    for (const unsigned n : {64u, 65u, 126u, 127u, 255u}) {
      const std::string eq(n, '='), mark(n, 'a');
      cases.push_back("#[" + eq + "[x]" + eq + "]");
      cases.push_back("#<" + mark + "-" + mark + ">_");
      cases.push_back("#@" + mark + ":body" + mark + "?");
    }
    for (const auto& text : cases)
      for (TextOffset end = 1; end <= text.size(); ++end)
        CHECK(one(text.data(), end, 1));
  }

  // Deep keyword prefixes rarely occur in random byte strings.
  for (unsigned term = 0; term < A.cfg.nterms; ++term) {
    if (!A.cfg.terms[term].spelled) continue;
    const std::string_view spelling = A.cfg.term_text(term);
    for (std::size_t n = 1; n <= spelling.size(); ++n) {
      const std::string prefix = "#" + std::string(spelling.substr(0, n));
      if (!one(prefix.data(), static_cast<TextOffset>(prefix.size()), 1))
        return;
      for (unsigned b = 0; b < 256; ++b) {
        const std::string text = prefix + static_cast<char>(b) + "_suffix";
        if (!one(text.data(), static_cast<TextOffset>(text.size()), 1))
          return;
      }
    }
  }

  // Every word the budget allows, exhaustively. The mode tells bytes apart
  // by class and by nothing else, so one byte per class IS every byte, and a
  // mode that grows a class grows this sweep with it. The depth is what the
  // class count leaves room for, so a narrow mode is exhausted deeper than a
  // wide one and neither costs more than the other.
  const std::string rep = mode_alphabet<A>(m);
  {
    constexpr double kWords = 32768;
    std::size_t depth = 1;
    for (double n = static_cast<double>(rep.size()); n * rep.size() <= kWords;
         n *= static_cast<double>(rep.size()))
      ++depth;
    std::string w(depth, '\0');
    std::vector<std::size_t> at(depth, 0);
    for (std::size_t d = 1; d <= depth; ++d) {
      std::fill(at.begin(), at.begin() + static_cast<std::ptrdiff_t>(d), 0);
      for (;;) {
        for (std::size_t k = 0; k < d; ++k) w[k] = rep[at[k]];
        for (unsigned from = 0; from < d; ++from)
          if (!one(w.data(), static_cast<unsigned>(d), from)) return;
        std::size_t k = d;
        while (k-- > 0) {
          if (++at[k] < rep.size()) break;
          at[k] = 0;
        }
        if (k == static_cast<std::size_t>(-1)) break;
      }
    }
  }

  Draw d{std::mt19937{1000u + m}, rep};
  char buf[192];
  for (int t = 0; t < 3000; ++t) {
    const unsigned n = d.bits() % (sizeof buf + 1);
    // Half the draws are runs. A byte drawn on its own almost never repeats,
    // and a run is what the ladder crosses eight bytes at a time and what a
    // region body is made of: the forms part company there or nowhere.
    if ((t & 1) == 0) {
      for (unsigned k = 0; k < n;) {
        const char c = rep[d.bits() % rep.size()];
        for (unsigned r = 1 + d.bits() % 40u; r != 0 && k < n; --r)
          buf[k++] = c;
      }
    } else {
      for (unsigned k = 0; k < n; ++k) buf[k] = static_cast<char>(d.byte());
    }
    // Every prefix of a drawn word: a token cut by the end of file is the
    // edge the forms are most likely to spell apart.
    const unsigned from = n != 0 ? d.bits() % n : 0;
    if (!one(buf, n, from)) return;
    if (n != 0 && !one(buf, n - 1, from < n - 1 ? from : 0)) return;
  }
}

template <const auto& A>
void relay_runs_agree() {
  if constexpr (A.relays.size() != 0) {
    const cursiv::LrLexTape<A> tape{};
    std::vector<bool> reached(A.dfaAccept.size());
    std::vector<std::uint16_t> states;
    const auto add = [&](std::uint16_t state) {
      if (state == cursiv::kDeadState || reached[state]) return;
      reached[state] = true;
      states.push_back(state);
    };
    for (const auto& relay : A.relays) add(relay.resume);
    for (std::size_t i = 0; i < states.size(); ++i)
      for (unsigned byte = 0; byte < 256; ++byte)
        add(tape.step(states[i], static_cast<unsigned char>(byte)));
    for (const auto s : states) {
      if (!cursiv::lex_run_blind(tape, static_cast<std::uint16_t>(s))) continue;
      for (unsigned b = 0; b < 256; ++b) {
        const std::string text = "#" + std::string(65, static_cast<char>(b)) +
                                 static_cast<char>(b ^ 255u);
        for (const TextOffset len : {1u, 2u, 8u, 9u, 10u, 32u, 65u, 66u, 67u}) {
          TextOffset expected = 1;
          while (expected < len && A.next_byte(s, static_cast<unsigned char>(
                                                      text[expected])) == s)
            ++expected;
          CHECK(tape.run(static_cast<std::uint16_t>(s), text.data(), len, 1) ==
                expected);
        }
      }
    }
  }
}

// Recovery traverses to the first acceptance; election then stops that branch.
template <const auto& A>
void owed_terminals_agree() {
  for (std::size_t root = 0; root < A.dfaAccept.size(); ++root) {
    std::vector<bool> seen(A.dfaAccept.size());
    std::vector<std::uint16_t> todo{static_cast<std::uint16_t>(root)};
    seen[root] = true;
    cursiv::SymbolId best = cursiv::kInvalidSymbol;
    auto bestTier = 0u;
    while (!todo.empty()) {
      const auto s = todo.back();
      todo.pop_back();
      if (A.dfaAccept[s] != 0) {
        const auto term = static_cast<cursiv::SymbolId>(A.dfaAccept[s] - 1u);
        const auto tier = A.dfa_tier(s, term);
        if (cursiv::LrLexTape<A>{}.prefers(term, tier, best, bestTier)) {
          best = term;
          bestTier = tier;
        }
        continue;
      }
      for (auto k = A.derived.typoSuccBase[s];
           k < A.derived.typoSuccBase[s + 1u]; ++k)
        if (const auto to = A.derived.typoSucc[k]; !seen[to]) {
          seen[to] = true;
          todo.push_back(to);
        }
    }
    CHECK(A.derived.lexOwed[root] == best);
  }
}

template <const auto& A>
void grammar_agrees(const char* lang, const cursiv::LrDrive& drive) {
  owed_terminals_agree<A>();
  relay_runs_agree<A>();
  for (unsigned m = 0; m < A.modeStart.size(); ++m)
    mode_agrees<A>(lang, m, drive);
}

template <const auto& A>
std::vector<std::string_view> long_runs_agree(const char* lang,
                                              const cursiv::LrDrive& drive) {
  const cursiv::LrLexTape<A> tape{};
  std::vector<bool> tested(A.dfaAccept.size());
  std::vector<std::string_view> terminals;
  unsigned runs = 0;
  for (unsigned mode = 0; mode < A.modeStart.size(); ++mode) {
    std::vector<bool> seen(A.dfaAccept.size());
    std::vector<std::uint16_t> states{A.modeStart[mode]};
    std::vector<std::string> prefixes(1);
    seen[states.front()] = true;
    for (std::size_t at = 0; at < states.size(); ++at) {
      const auto state = states[at];
      const std::string prefix = prefixes[at];
      std::string members;
      for (unsigned byte = 0; byte < 256; ++byte) {
        const auto next = tape.step(state, static_cast<unsigned char>(byte));
        if (next == state) members.push_back(static_cast<char>(byte));
        if (next == cursiv::kDeadState || seen[next]) continue;
        seen[next] = true;
        states.push_back(next);
        prefixes.push_back(prefix + static_cast<char>(byte));
      }
      if (tested[state] || members.empty() || A.dfaAccept[state] == 0 ||
          !cursiv::lex_run_blind(tape, state))
        continue;
      std::string word = prefix;
      for (unsigned i = 0; i < 40; ++i)
        word.push_back(members[(37u * i) % members.size()]);
      const auto m = static_cast<std::uint16_t>(mode);
      const auto wordLen = static_cast<TextOffset>(word.size());
      const Scan whole = cursiv::lr_match_text<A>(word.data(), wordLen, m, 0);
      if (whole.term == cursiv::kInvalidSymbol || whole.end != wordLen)
        continue;
      tested[state] = true;
      terminals.push_back(A.cfg.term_name(whole.term));
      ++runs;
      const auto agrees = [&](const std::string& text, TextOffset len,
                              TextOffset from) {
        TextOffset halt = from;
        const Scan want =
            cursiv::lr_match_halt<A>(text.data(), len, m, from, halt);
        const Scan got = drive.walk(text.data(), len, m, from);
        if (same(got, want)) return true;
        std::printf(
            "FAIL run %s mode %u state %u at %u len %u: "
            "end %u/%u term %u/%u\n",
            lang, mode, state, from, len, got.end, want.end, got.term,
            want.term);
        for (TextOffset i = from; i < len; ++i)
          std::printf(" %02x", static_cast<unsigned char>(text[i]));
        std::printf("\n");
        return false;
      };
      for (const TextOffset from : {0u, 1u, 7u}) {
        std::string text(from, '?');
        text += word;
        text += '!';
        const auto len = static_cast<TextOffset>(text.size());
        for (unsigned lane = 0; lane < 8; ++lane) {
          const auto pos = from + prefix.size() + 8u + lane;
          const char saved = text[pos];
          for (unsigned byte = 0; byte < 256; ++byte) {
            text[pos] = static_cast<char>(byte);
            if (!agrees(text, len, from)) {
              CHECK(false);
              return {};
            }
          }
          text[pos] = saved;
        }
        // The backing bytes stay readable past the logical EOF.
        for (unsigned tail = 0; tail <= 25; ++tail)
          CHECK(agrees(text,
                       static_cast<TextOffset>(from + prefix.size() + tail),
                       from));
        for (const std::string_view suffix :
             {"\xC3\xA9", "\xE6\x97\xA5", "\xF0\x9D\x92\xB3", "\xC3",
              "\xE6\x97", "\xF0\x9D\x92", "\x80", "\xFF"}) {
          for (unsigned tail = 7; tail <= 9; ++tail) {
            text.resize(from + prefix.size() + tail);
            text += suffix;
            CHECK(agrees(text, static_cast<TextOffset>(text.size()), from));
            text.assign(from, '?');
            text += word;
            text += '!';
          }
        }
      }
    }
  }
  CHECK(runs != 0);
  return terminals;
}

// One guard row per acceptance of a state the lexer kept: minimisation leaves
// none behind for the states it merged away.
template <const auto& A, const auto& Cold>
void guard_rows_are_packed(const char* lang) {
  std::size_t accepted = 0;
  CHECK(Cold.accBoundBase.size() == A.dfaAccept.size() + 1u);
  for (unsigned s = 0; s < A.dfaAccept.size(); ++s) {
    CHECK(Cold.accBoundBase[s] == accepted);
    for (unsigned t = 0; t < A.cfg.nterms; ++t)
      if (A.dfa_acc_set(s).test(t)) ++accepted;
  }
  CHECK(Cold.accBoundBase.back() == accepted);
  if (accepted != Cold.accBound.size())
    std::printf("%s: %zu guard rows for %zu acceptances\n", lang,
                Cold.accBound.size(), accepted);
  CHECK(accepted == Cold.accBound.size());
}

// Where a keyword's boundary refuses, the spelling goes back to the names it
// reserved -- and every form must hand back the same one. A name whose
// alphabet is narrower than the word class, the same past the width a ladder
// is emitted for, a guard refusing a word byte beside another, and two names
// of one spelling.
// Representative shapes exercise the common lexer entry points without
// repeating the same combinatorial oracle for every product grammar.
#define LANE(ns, name)                                 \
  (grammar_agrees<ns::kTable>(name, ns::kDrive),       \
   guard_rows_are_packed<ns::kTable, ns::kCold>(name))

void the_hard_shapes_answer_the_same_from_every_form() {
  LANE(arith::generated::Arith, "arithmetics");
  LANE(tcandrun::generated::G, "candidate and ASCII runs");
  LANE(tnestedlex::overlap::generated::G, "relayoverlap");
  LANE(tonemode::generated::G, "onemode");
  // One mode lexes as every other table does, through the walk of all.
  CHECK(tonemode::generated::G::kTable.modeStart.size() == 1);
  const auto one = tonemode::generated::G::create_parser();
  CHECK(one->parse(" \n ").diagnostics.empty());
  CHECK(one->parse(" x ").diagnostics.size() == 1);
}

// A token whose rest is a search's: the forms hand the search the same mark
// and read the same rest.
void an_echo_answers_the_same_from_every_form() {
  LANE(tbackref::generated::G, "backref");
}

// Where a keyword's boundary refuses, the spelling goes back to the names it
// reserved -- and every form must hand back the same one.
void a_refused_keyword_hands_the_spelling_back_the_same_way() {
  LANE(tguard::generated::G, "guard");
  // A refused word behind a head takes its terminal off the position, and
  // what stands without it is the answer. Exercise each lexer form
  // explicitly: random bytes seldom spell a guard.
  namespace rt = tguard::generated::G;
  const auto& a = rt::kTable;
  const auto mode = [&] {
    for (unsigned state = 0; state < a.nstates; ++state) {
      const auto term = a.derived.expect.shiftTerm[state];
      if (term < a.cfg.nterms && a.cfg.term_text(term) == "behind")
        return a.mode[state];
    }
    CHECK(false);
    return a.mode[0];
  }();
  CHECK(a.modeMulti[mode] != 0);
  for (const std::string_view text : {"$$include!", "$$includes", "$$inc", "%bcd",
                                      "~~abc", "~~def", "~~ghi", "~~de"}) {
    const TextOffset expected = text.front() == '$' ? 3 :
        text == "~~ghi" || text == "~~de" ? 2 : 1;
    const auto len = static_cast<TextOffset>(text.size());
    CHECK(cursiv::lr_match_text<a>(text.data(), len, mode, 0).end == expected);
    CHECK(rt::kDrive.walk(text.data(), len, mode, 0).end == expected);
    std::vector<Scan> room(a.maxCands);
    const auto n =
        rt::kDrive.scanText(text.data(), len, mode, 0, room.data(), a.maxCands);
    CHECK(n != 0);
    for (unsigned i = 0; i < n; ++i) CHECK(room[i].end == expected);
  }

  namespace relay = tnestedlex::overlap::generated::G;
  const auto& ar = relay::kTable;
  const auto rm = ar.mode[0];
  CHECK(ar.modeMulti[rm] != 0);
  for (const std::string_view text : {"/*x*/", "/*xx*/", "/*x/*y*/z*/", "/*x",
                                      "<*x*>"}) {
    const auto len = static_cast<TextOffset>(text.size());
    const auto scan = cursiv::lr_match_text<ar>(text.data(), len, rm, 0);
    CHECK(scan.end == len);
    if (text == "/*x*/" || text == "<*x*>")
      CHECK(scan.term != cursiv::kInvalidSymbol && ar.cfg.terms[scan.term].spelled);
    CHECK(same(relay::kDrive.walk(text.data(), len, rm, 0), scan));
    std::vector<Scan> room(ar.maxCands);
    const auto n = relay::kDrive.scanText(text.data(), len, rm, 0, room.data(),
                                          ar.maxCands);
    CHECK(n != 0);
    if (text == "/*x*/" || text == "<*x*>") CHECK(n == 1);
    for (unsigned k = 0; k < n; ++k) CHECK(room[k].end == len);
  }

}
#undef LANE

// The repair's reading against the lexer, over a valid document in every mode:
// silent on every token the lexer reads, reading no further than the lexer
// where it reads past its match and finds nothing, and on a token whose body a
// refused byte broke, ending where the lexer ended it -- the fault never moves
// the closer.
template <const auto& A>
void the_reading_ends_where_the_lexer_does(const char* lang,
                                           const std::string& text) {
  CHECK(!text.empty());
  std::vector<char> used(A.modeStart.size(), 0);
  for (unsigned st = 0; st < A.nstates; ++st) used[A.mode[st]] = 1;
  const auto len = static_cast<TextOffset>(text.size());
  std::size_t broken = 0, bad = 0;
  for (unsigned m = 0; m < A.modeStart.size(); ++m) {
    if (used[m] == 0) continue;
    const auto mode = static_cast<std::uint16_t>(m);
    for (TextOffset i = 0; i < len; ++i) {
      TextOffset halt = i;
      const Scan want =
          cursiv::lr_match_halt<A>(text.data(), len, mode, i, halt);
      if (want.term == cursiv::kInvalidSymbol) continue;
      TextOffset reached = i;
      const cursiv::LexReading read =
          cursiv::lr_lex_reading<A>(text.data(), len, mode, i, reached);
      if (halt > want.end) {
        if (read.term == cursiv::kInvalidSymbol && reached > halt && bad++ < 3)
          std::printf(
              "FAIL %s mode %u: nothing read at %u, to %u past the "
              "lexer's %u\n",
              lang, m, i, reached, halt);
        continue;
      }
      if (read.term != cursiv::kInvalidSymbol) ++bad;
      std::uint16_t s = A.modeStart[m];
      for (TextOffset k = i; k + 1 < want.end; ++k) {
        const auto b = static_cast<unsigned char>(text[k]);
        const std::uint16_t nx = A.next_byte(s, b);
        if (nx == cursiv::kDeadState) break;
        if (nx == s && A.derived.lexBodies[s] != 0) {
          unsigned x = 1;
          while (x < 256 && (A.next_byte(s, static_cast<unsigned char>(x)) !=
                                 cursiv::kDeadState ||
                             x == '\n' || x == '\r'))
            ++x;
          if (x < 256) {
            std::string bent = text;
            bent[k] = static_cast<char>(x);
            reached = i;
            const cursiv::LexReading r =
                cursiv::lr_lex_reading<A>(bent.data(), len, mode, i, reached);
            ++broken;
            if (r.term != want.term || r.end != want.end || !r.closed) {
              if (bad++ < 3)
                std::printf(
                    "FAIL %s mode %u: token at %u broken at %u ends "
                    "at %u, the lexer's at %u\n",
                    lang, m, i, k, r.end, want.end);
            }
          }
        }
        s = nx;
      }
    }
  }
  std::printf("  %-13s broken bodies=%zu bad=%zu\n", lang, broken, bad);
  failures += bad;
}

}  // namespace

void run() {
  const auto tested =
      long_runs_agree<tcandrun::generated::G::kTable>(
          "candidate and ASCII runs", tcandrun::generated::G::kDrive);
  for (const std::string_view terminal : {"Hex", "High", "Asymmetric", "Byte", "Nul"})
    CHECK(std::ranges::find(tested, terminal) != tested.end());
  long_runs_agree<arith::generated::Arith::kTable>(
      "arithmetics", arith::generated::Arith::kDrive);
  the_reading_ends_where_the_lexer_does<arith::generated::Arith::kTable>(
      "arithmetics", "module m /* body */ def f(x): x + 123; f(2);");
  a_refused_keyword_hands_the_spelling_back_the_same_way();
  the_hard_shapes_answer_the_same_from_every_form();
  an_echo_answers_the_same_from_every_form();
}
}  // namespace case_lexercontract

#endif



namespace case_lexguard {
// A candidate the priority outranks is refused by its own guard and by
// nothing else: neither the winner's boundary nor a sibling alternative's.
// The lexical tables are held to the same guards, byte by byte.

namespace g = tguard::generated::G;

namespace {

// A case's document: its opening keyword, then what the case reads.
std::string in(std::string_view lead, std::string_view text) {
  return std::string(lead) + " " + std::string(text);
}

// The mode a case lexes its first token in: the state its keyword enters.
template <const auto& A>
std::uint16_t mode_of(std::string_view lead) {
  for (unsigned s = 0; s < A.nstates; ++s) {
    const auto t = A.derived.expect.shiftTerm[s];
    if (t < A.cfg.nterms && A.cfg.terms[t].spelled &&
        A.cfg.term_text(t) == lead)
      return A.mode[s];
  }
  CHECK(false);
  return 0;
}

template <const auto& A>
std::uint16_t term_named(std::string_view name) {
  for (unsigned t = 0; t < A.cfg.nterms; ++t)
    if (A.cfg.term_name(static_cast<std::uint16_t>(t)) == name)
      return static_cast<std::uint16_t>(t);
  CHECK(false);
  return cursiv::kInvalidSymbol;
}

const cursiv::Parser& parser() {
  static const std::unique_ptr<cursiv::Parser> p = g::create_parser();
  return *p;
}

void reads(std::string_view lead, const char* text, bool clean) {
  cursiv::Cst cst;
  const std::string doc = in(lead, text);
  cursiv::lr_parser<g::kTable>.parse(doc, cst, nullptr, &g::kDrive);
  const bool got = cst.diagnostics().empty();
  if (got != clean) {
    std::printf("FAIL %s: %s, %zu diagnostic(s)\n", doc.c_str(),
                clean ? "owed a clean read" : "owed a diagnostic",
                cst.diagnostics().size());
    ++failures;
  }
}

// A clean parse that took no repair.
void whole(std::string_view lead, const char* text) {
  const std::string doc = in(lead, text);
  const cursiv::ParseResult r = parser().parse(doc);
  if (r.cst().accepted() && !r.cst().recovered()) return;
  std::printf("FAIL '%s': accepted=%d repaired=%d\n", doc.c_str(),
              static_cast<int>(r.cst().accepted()),
              static_cast<int>(r.cst().recovered()));
  ++failures;
}

// `a;` reads B, and B is owed `_`: A's own guard refused the `;`.
void a_candidate_keeps_its_own_guard() {
  reads("own", "a;", false);
  reads("own", "a ;", true);
  reads("own", "a_", true);
  reads("own", "a _", true);
}

// Two literals of one terminal, each guarded against its own class.
void each_literal_keeps_its_own_class_guard() {
  reads("twoguards", "a +;", true);
  reads("twoguards", "a -;", true);
  reads("twoguards", "a ++;", false);
  reads("twoguards", "a --;", false);
  reads("twoguards", "a +-;", false);
}

// `&&B` reads AND through its `&&` alternative, which no word closes.
void a_candidate_inherits_no_sibling_boundary() {
  reads("mixed", "&&B", true);
  reads("mixed", "&& B", true);
  reads("mixed", "&&C", true);
  reads("mixed", "and B", true);
  reads("mixed", "andB", false);
}

// The typo alone, or no re-reading of the author's bytes at all: what a guard
// refuses behind the terminal is never passed for it.
template <const auto& A>
void repairs_in(const std::string& doc, bool alone,
                const cursiv::LrDrive* drive) {
  cursiv::Cst cst;
  cursiv::lr_parser<A>.parse(doc, cst, nullptr, drive);
  bool reread = false;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    reread = reread || (cst[i].recovered && cst[i].begin < cst[i].end &&
                        A.stamp.state(cst[i]) != 0);
  const bool got = cst.diagnostics().size() == 1 && reread;
  if (got != alone) {
    std::printf("FAIL %s: %s, %zu diagnostic(s)\n", doc.c_str(),
                alone ? "owed the typo alone" : "owed more than the typo",
                cst.diagnostics().size());
    ++failures;
  }
}

void repairs(const char* text, bool alone) {
  repairs_in<g::kTable>(in("typo", text), alone, &g::kDrive);
}

// `abc;` is no reading at all, so a repair that re-reads `axbc;` as one may
// not pass it for whole: the guard `B` declares refuses the `;` behind it.
void a_re_reading_keeps_the_guard_of_what_it_names() {
  reads("typo", "abc;", false);
  reads("typo", "abc ;", true);
  reads("typo", "abc_", true);
  repairs("axbc ;", true);
  repairs("axbc;", false);
  // Replacing the last character traverses a real accepting edge. The
  // terminal's frontier still applies after that substituted character.
  repairs("abx ;", true);
  repairs("abx;", false);
  // A refused word of two bytes takes the match back just as a byte does.
  repairs_in<g::kTable>("typomulti dxef ; ;", true, &g::kDrive);
  repairs_in<g::kTable>("typomulti dxef;;", false, &g::kDrive);
}

// The caret's own reading of the same law: one spelling, two terminals, and
// the answer is the terminal whose guard admits what stands behind the caret.
void a_caret_offers_the_terminal_whose_own_guard_admits() {
  const auto offered = [&](const char* text) {
    const auto at = static_cast<cursiv::TextOffset>(std::strlen("own "));
    return checks::features(parser(), parser().parse(in("own", text)), at, at)
        .size();
  };
  // `A` refuses a `;` right behind it, `B` does not; both spell `a` and both
  // stand where the case opens.
  CHECK(offered(";") == 1);
  CHECK(offered("_") == 2);
  CHECK(offered(" ;") == 2);
  CHECK(offered("") == 2);
}

// A keyword whose boundary refuses hands its spelling back to the names it
// reserved, whatever else the guard refuses beside a word byte.
void a_refused_keyword_reserves_nothing() {
  const auto clean = [](std::string_view lead, const char* text, bool want) {
    const std::string doc = in(lead, text);
    const cursiv::ParseResult r = parser().parse(doc);
    if (r.cst().diagnostics().empty() != want) {
      std::printf("FAIL %s: %s, %zu diagnostic(s)\n", doc.c_str(),
                  want ? "owed a clean read" : "owed a diagnostic",
                  r.cst().diagnostics().size());
      ++failures;
    }
  };
  // `x` is a word byte of this grammar and the name's alphabet stops at `b`,
  // so `abx` is the name `ab` and the terminal `x`.
  clean("reserved", "abx", true);
  clean("reserved", "ab;", true);
  clean("reserved", "abbx", true);
  // The guard refuses `_` and `;`: the reserve lifts on the word byte alone.
  clean("bytewise", "ab_", true);
  clean("bytewise", "ab?", true);
  clean("bytewise", "ab!", true);
  // Two names of one spelling: the sweep is what tells them apart.
  clean("rival", "ab_", true);
  clean("rival", "ab_;", true);
  clean("rival", "ab!", true);
}

// One candidate left is still an answer: the walk elected the name, the
// keyword's reserve took it away, and what the sweep kept is what stands.
void one_candidate_left_is_still_the_answer() {
  const auto owed = [&](const char* text, std::size_t want) {
    const std::string doc = in("sole", text);
    const cursiv::ParseResult r = parser().parse(doc);
    if (r.cst().diagnostics().size() != want) {
      std::printf("FAIL %s: owed %zu diagnostic(s), got %zu\n", doc.c_str(),
                  want, r.cst().diagnostics().size());
      ++failures;
    }
  };
  owed("and!", 0);  // the keyword, which owes `!`
  owed("and;", 1);  // `and` is no name here, so `;` stands for the `!`
  owed("&&!", 0);
  owed("abc;", 0);
}

// A caret offers what the position would READ, and a spelling that glues into
// a longer terminal is not read there: `<` typed before `<a` spells `<<`, and
// no reading of the parse ever cuts that back into two.
void a_caret_offers_only_what_the_position_reads() {
  const auto at = static_cast<cursiv::TextOffset>(std::strlen("shorter "));
  const auto asks = [&](const char* text, cursiv::TextOffset from,
                        cursiv::TextOffset to, std::string_view want) {
    return checks::offers(parser(), in("shorter", text), at + from, at + to,
                          want);
  };
  const auto offers = [&](const char* text, cursiv::TextOffset from,
                          cursiv::TextOffset to, std::string_view want) {
    if (asks(text, from, to, want)) return;
    std::printf("FAIL '%s' [%u,%u): '%.*s' is not offered\n", text, from, to,
                static_cast<int>(want.size()), want.data());
    ++failures;
  };
  const auto refuses = [&](const char* text, cursiv::TextOffset from,
                           cursiv::TextOffset to, std::string_view want) {
    if (!asks(text, from, to, want)) return;
    std::printf("FAIL '%s' [%u,%u): '%.*s' is offered and glues\n", text, from,
                to, static_cast<int>(want.size()), want.data());
    ++failures;
  };
  offers("", 0, 0, "<");     // nothing to glue to
  offers("<<a", 1, 2, "<");  // the token it already is
  refuses("<a", 0, 0, "<");  // would spell `<<a`, which no alternative reads
  refuses("<<a", 0, 1, "<");
}

// A terminal is the unit, and the mode is what tells two spellings apart: the
// state inside the nest reads `>` and never `>>`, so its longest match is
// `>`. Nothing here re-reads a position the walk already answered.
void a_mode_that_reads_no_long_token_matches_the_short_one() {
  whole("nested", "a<b<c>>");
  whole("nested", "a>>b");
}

// Every acceptance at the state is refused and none of them stands: the walk
// hands the match back to what the path accepted shorter, and nothing else
// holds that reading.
void a_state_refused_whole_gives_the_match_back_to_the_path() {
  whole("stump", "ab!");  // the guard admits `!`, so `ab` is the token
  whole("stump", "ab;");  // it refuses `;`, and nothing at that extent stands
}

// The boundary test, in its four answers: no boundary at all, a boundary with
// nothing behind the match, one the byte behind refuses, one it admits. Both
// entries of the shared walk take them, since each inlines the walk of its
// own.
void the_boundary_test_answers_four_ways() {
  static constexpr auto& A = g::kTable;
  const std::uint16_t mode = mode_of<A>("stump");
  const auto both = [&](const char* text, cursiv::TextOffset want,
                        std::uint16_t term) {
    const auto len = static_cast<cursiv::TextOffset>(std::strlen(text));
    cursiv::TextOffset halt = 0;
    const cursiv::Scan a = cursiv::lr_match_text<A>(text, len, mode, 0);
    const cursiv::Scan b = cursiv::lr_match_halt<A>(text, len, mode, 0, halt);
    if (a.end == want && a.term == term && b.end == want && b.term == term)
      return;
    std::printf("FAIL boundary '%s': %u/%u and %u/%u, wanted %u/%u\n", text,
                a.end, a.term, b.end, b.term, want, term);
    ++failures;
  };
  // `One` spells `a` and declares no boundary; `Two` spells `ab` under one
  // that refuses `;`.
  const auto one = term_named<A>("One"), two = term_named<A>("Two");
  both("a!", 1, one);   // no boundary on the state that answers
  both("ab", 2, two);   // a boundary, and nothing behind the match
  both("ab!", 2, two);  // a boundary the byte behind admits
  both("ab;", 1, one);  // one it refuses, and the path answers instead
}

// The winner's guard refuses the byte behind and the one it outranked
// declares a boundary that admits it: the refusal path weighs a BOUNDED
// acceptance, not only an unbounded one.
void a_refused_winner_weighs_a_bounded_rival() {
  static constexpr auto& A = g::kTable;
  const std::uint16_t mode = mode_of<A>("twin");
  const auto reads = [&](const char* text, cursiv::TextOffset want) {
    const auto len = static_cast<cursiv::TextOffset>(std::strlen(text));
    const cursiv::Scan s = cursiv::lr_match_text<A>(text, len, mode, 0);
    if (s.end == want && s.term != cursiv::kInvalidSymbol) return;
    std::printf("FAIL twin '%s': %u/%u\n", text, s.end, s.term);
    ++failures;
  };
  reads("ab;", 2);  // the winner refused, the rival's own boundary admits
  reads("ab!", 2);  // the other way round
  whole("twin", "ab!");
  whole("twin", "ab;");
}

// The spelled token refused by the byte behind it, the name and a trivia
// declared first both spell the same bytes: the name stands, as it does
// where nothing is refused.
void a_refused_election_ranks_a_token_before_a_trivia() {
  namespace tv = tguard::trivia::generated::G;
  static constexpr auto& A = tv::kTable;
  const cursiv::Scan s = cursiv::lr_match_text<A>("ab!", 3, A.mode[0], 0);
  CHECK(s.end == 2 && s.term != cursiv::kInvalidSymbol &&
        !A.cfg.terms[s.term].trivia && !A.cfg.terms[s.term].spelled);
  const std::unique_ptr<cursiv::Parser> p = tv::create_parser();
  for (const char* t : {"ab!", "ba!"}) {
    const cursiv::ParseResult r = p->parse(t);
    const auto* doc = r.value_as<tguard::Items>();
    CHECK(r.accepted && r.diagnostics.empty() && doc != nullptr &&
          doc->items.size() == 2 && doc->items[0] == std::string_view(t, 2) &&
          doc->items[1] == "!");
  }
}

// The match stands while the refused word is not spelled whole, and is
// taken back where it is -- giving the match before it back -- unless the
// word's own boundary rescues it.
void a_refused_word_takes_the_match_back_where_it_is_spelled_whole() {
  namespace deep = tguard::generated::G;
  const auto& A = deep::kTable;
  const auto at = [&](const char* text) {
    const auto len = static_cast<cursiv::TextOffset>(std::strlen(text));
    return cursiv::lr_match_text<A>(text, len, mode_of<A>("deep"), 0).end;
  };
  CHECK(at("<!DOCTYPE x> y") == 12);   // DTD
  CHECK(at("<!-x> y") == 5);           // DTD: `--` not spelled whole
  CHECK(at("<!-- c --> y") == 10);     // COMMENT, the longer match
  CHECK(at("<![CDATA[x]]> y") == 13);  // CDATA
  CHECK(at("<!-- open") == 1);         // DTD taken back: `<` stands
  CHECK(at("<![CDATA[ open") == 1);
  CHECK(at("<![CDATX[ x>") == 12);     // DTD: `[CDATA[` not spelled whole
  CHECK(at("#includes") == 1);         // HASH: rescued by the word byte
  CHECK(at("#include x") == 0);        // HASH taken back, nothing before it
  CHECK(at("#inc") == 1);
  std::printf("  a refused word behind a match is read on to its end\n");
}

struct Coverage {
  bool fallback = false;
  bool reserved = false;
  bool wideHead = false;
  bool duplicateHead = false;
} coverage;

// The generated lexical decisions equal the grammar's DFA and guards,
// including alternatives that the hot lexer immediately overwrites.
template <const auto& A>
void audit() {
  using namespace cursiv;
  const auto& rows = A.derived;
  const int before = failures;
  CHECK(rows.lexElseBase.size() == A.dfaAccept.size() + 1);
  CHECK(rows.typoSuccBase.size() == A.dfaAccept.size() + 1);
  CHECK(rows.lexElseBase.back() == rows.lexElseRows.size());
  CHECK(rows.typoSuccBase.back() == rows.typoSucc.size());
  std::uint32_t stops = 0;
  std::uint64_t read = 0;
  const LrTypoTape<A> tape{{nullptr, 0, 0, &read}, nullptr, &stops};
  for (std::uint16_t s = 0; s < A.dfaAccept.size(); ++s) {
    coverage.reserved |= A.dfa_res_set(s).any();
    CHECK(rows.lexElseBase[s] <= rows.lexElseBase[s + 1u]);
    CHECK(rows.lexElseBase[s + 1u] <= rows.lexElseRows.size());
    CHECK(rows.typoSuccBase[s] <= rows.typoSuccBase[s + 1u]);
    CHECK(rows.typoSuccBase[s + 1u] <= rows.typoSucc.size());
    if (failures != before) return;
    for (unsigned b = 0; b < 256; ++b) {
      const auto byte = static_cast<unsigned char>(b);
      SymbolId expected = kInvalidSymbol;
      if (A.dfaAccept[s] != 0 && A.dfa_bounded(s) &&
          A.cfg.sets[A.dfaBound[s]].has(byte)) {
        bool reserved = false;
        for (unsigned t = 0; t < A.cfg.nterms; ++t) {
          if (!A.dfa_acc_set(s).test(t)) continue;
          const auto bound = A.dfa_acc_bound(s, t);
          const auto tier = A.dfa_tier(s, t);
          reserved = reserved ||
                     (tier == kTierLiteral &&
                      (bound == kNoSet || !A.cfg.sets[bound].has(byte))) ||
                     (tier == kTierKeyword && !A.cfg.wordBytes.has(byte));
        }
        // Tier, then a token before a trivia, then the declaration.
        const auto rank = [&](unsigned t) {
          return lex_rank(A.dfa_tier(s, t), A.cfg.terms[t].trivia);
        };
        for (unsigned t = 0; t < A.cfg.nterms; ++t) {
          if (!A.dfa_acc_set(s).test(t)) continue;
          if (reserved && A.dfa_res_set(s).test(t)) continue;
          const auto bound = A.dfa_acc_bound(s, t);
          if (bound != kNoSet && A.cfg.sets[bound].has(byte)) continue;
          if (expected == kInvalidSymbol || rank(t) > rank(expected))
            expected = static_cast<SymbolId>(t);
        }
      }
      SymbolId actual = kInvalidSymbol;
      unsigned matches = 0;
      for (auto i = rows.lexElseBase[s]; i < rows.lexElseBase[s + 1u]; ++i) {
        if (!rows.lexElseRows[i].bytes.has(byte)) continue;
        actual = rows.lexElseRows[i].term;
        ++matches;
      }
      CHECK(matches <= 1);
      CHECK(actual == expected);
      coverage.fallback |= expected != kInvalidSymbol;
    }

    std::vector<std::uint16_t> expected;
    std::vector<std::uint8_t> live;
    for (unsigned b = 0; b < 256; ++b) {
      const auto to = A.next_byte(s, static_cast<unsigned char>(b));
      if (to == kDeadState) continue;
      if (std::find(live.begin(), live.end(), A.cls[b]) == live.end())
        live.push_back(A.cls[b]);
      if (std::find(expected.begin(), expected.end(), to) == expected.end())
        expected.push_back(to);
    }
    const auto liveClasses = static_cast<unsigned>(live.size());
    const auto actual = tape.successors(s);
    CHECK(std::ranges::equal(actual, expected));
    const bool head = std::ranges::find(A.modeStart, s) != A.modeStart.end();
    coverage.wideHead |= head && expected.size() > kTypoEdges;
    coverage.duplicateHead |= head && expected.size() < liveClasses;
  }
}

}  // namespace

void run() {
  a_refused_word_takes_the_match_back_where_it_is_spelled_whole();
  a_candidate_keeps_its_own_guard();
  a_candidate_inherits_no_sibling_boundary();
  each_literal_keeps_its_own_class_guard();
  a_re_reading_keeps_the_guard_of_what_it_names();
  a_caret_offers_the_terminal_whose_own_guard_admits();
  a_refused_keyword_reserves_nothing();
  one_candidate_left_is_still_the_answer();
  a_caret_offers_only_what_the_position_reads();
  a_mode_that_reads_no_long_token_matches_the_short_one();
  a_state_refused_whole_gives_the_match_back_to_the_path();
  the_boundary_test_answers_four_ways();
  a_refused_winner_weighs_a_bounded_rival();
  a_refused_election_ranks_a_token_before_a_trivia();
  audit<g::kTable>();
  audit<tguard::trivia::generated::G::kTable>();
  CHECK(coverage.fallback);
  CHECK(coverage.reserved);
  CHECK(coverage.wideHead);
  CHECK(coverage.duplicateHead);
}
}  // namespace case_lexguard

namespace case_refusedheadwords {
// Words refused at the head of a name that is also read where each is a
// keyword: a name is the word with a byte past it, before it or short of it,
// a word alone is its keyword, and where only a name reads it is refused.

namespace {

constexpr std::string_view kWords =
    "a an and any array as ask assert begin bind bit block body bool break "
    "by case cast catch char check class const continue copy count create "
    "declare default define delete do else elseif end enum exit extern false "
    "fetch for from func goto if import in index inline int is let like loop "
    "match mod new nil not null of or out pass print raise read ref repeat "
    "return select set sort static step struct switch then to true try type "
    "union unless until use var while with write yield alpha bravo charlie "
    "delta echo foxtrot golf hotel india juliet kilo lima mike november "
    "oscar papa quebec romeo sierra tango uniform victor whiskey xray yankee "
    "zulu apple banana cherry damson elder fig grape hazel iris jade kiwi "
    "lemon mango nectar olive peach quince rowan sorrel thyme ugli vanilla "
    "walnut ";

constexpr std::string_view kOthers =
    "zephyr zodiac zenith zigzag zealot zebra zinc zonal zippy zircon zydeco "
    "zero zone zeta zing zest zap zoo zany zeal zilch zither zombie zoetic "
    "zonule zorro zucchini zwieback zygote zoology zoning zipper zestful "
    "zealous zooid zymase zibeline zanzibar zymurgy zirconia ";

std::vector<std::string> split(std::string_view text) {
  std::vector<std::string> out;
  std::istringstream in{std::string(text)};
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

bool is_word(const std::vector<std::string>& all, const std::string& s) {
  for (const std::string& w : all)
    if (w == s) return true;
  return false;
}

}  // namespace

void run() {
  const auto parser = tguard::generated::G::create_parser();
  const std::vector<std::string> all = split(kWords);
  CHECK(all.size() > 80);

  std::string doc = "heads ";
  std::vector<std::string> want;
  const auto add = [&](const std::string& s) {
    const bool word = is_word(all, s);
    doc += s + (word ? "!;\n" : ";\n");
    want.push_back(word ? '!' + s : s);
  };
  for (const std::string& w : all) {
    add(w);
    add(w + "x");
    add(w + "_");
    add(w + "9");
    add("x" + w);
    add(w + w);
    if (w.size() > 1) add(w.substr(0, w.size() - 1));
    if (w.size() > 2) add(w.substr(0, 2) + "q" + w.substr(2));
    if (w.size() > 1) add(w.substr(1));
  }
  for (const std::string& e : split(kOthers)) {
    doc += e + "?;\n";
    want.push_back(e);
    add(e + "x");
    add("x" + e);
    add(e.substr(0, e.size() - 1));
  }
  const auto r = parser->parse(doc);
  CHECK(r.accepted && r.diagnostics.empty());
  const auto* d = r.value_as<trefusedheads::Doc>();
  CHECK(d != nullptr && d->items.size() == want.size());
  if (d != nullptr && d->items.size() == want.size())
    for (std::size_t i = 0; i < want.size(); ++i) {
      const trefusedheads::Item* item = d->items[i];
      CHECK((item->keyword ? '!' + std::string(item->name)
                           : std::string(item->name)) == want[i]);
    }

  for (const std::string& w : all) {
    CHECK(!parser->parse("heads =" + w + ";").diagnostics.empty());
    CHECK(parser->parse("heads =" + w + "x;").diagnostics.empty());
    CHECK(parser->parse("heads = " + w + "_ ;").diagnostics.empty());
  }
}
}  // namespace case_refusedheadwords

namespace case_refusedshorter {
// A terminal refused at its head leaves the bytes to what stands without it,
// and what its own state accepted inside a run is still owed to them: the
// table's walk, the generated walk and the sweep read alike.

namespace {

namespace g = trefusedshorter::generated;
constexpr auto& A = g::Lr::kTable;
using cursiv::Scan;
using cursiv::TextOffset;

struct Case {
  std::string_view text;
  std::string_view tokens;
  TextOffset end;
  std::string_view term;
};

std::string tokens(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<trefusedshorter::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const trefusedshorter::Item* i : d->items)
    out += i->bang            ? "! "
           : !i->wide.empty() ? "W:" + std::string(i->wide) + ' '
                              : "N:" + std::string(i->narrow) + ' ';
  return out;
}

std::string_view name_of(const Scan& s) {
  return s.term == cursiv::kInvalidSymbol ? std::string_view("-")
                                          : A.cfg.term_name(s.term);
}

bool held(const Scan* room, std::uint32_t n, const Scan& s) {
  for (std::uint32_t k = 0; k < n; ++k)
    if (room[k].term == s.term) return true;
  return false;
}

void reads(std::string_view lead, const std::vector<Case>& cases) {
  const auto lr = g::Lr::create_parser();
  const std::uint16_t mode = case_lexguard::mode_of<A>(lead);
  for (const Case& c : cases) {
    const std::string doc = std::string(lead) + ' ' + std::string(c.text);
    const auto b = lr->parse(doc);
    CHECK(b.accepted && b.diagnostics.empty());
    CHECK(tokens(b) == c.tokens);

    const auto len = static_cast<TextOffset>(c.text.size());
    const Scan table = cursiv::lr_match_text<A>(c.text.data(), len, mode, 0);
    const Scan gen = g::Lr::kDrive.walk(c.text.data(), len, mode, 0);
    const bool right = table.end == c.end && name_of(table) == c.term &&
                       gen.end == c.end && name_of(gen) == c.term;
    if (!right)
      std::printf(
          "`%.*s` after `%.*s`: table %u/%.*s, walk %u/%.*s, wanted "
          "%u/%.*s\n",
          static_cast<int>(c.text.size()), c.text.data(),
          static_cast<int>(lead.size()), lead.data(), table.end,
          static_cast<int>(name_of(table).size()), name_of(table).data(),
          gen.end, static_cast<int>(name_of(gen).size()), name_of(gen).data(),
          c.end, static_cast<int>(c.term.size()), c.term.data());
    CHECK(right);
  }
}

// Every word of three bytes, up to seven.
template <class Each>
void each_word(Each&& each) {
  for (unsigned len = 1; len <= 7; ++len) {
    unsigned n = 1;
    for (unsigned k = 0; k < len; ++k) n *= 3;
    for (unsigned code = 0; code < n; ++code) {
      std::string text;
      for (unsigned k = 0, c = code; k < len; ++k, c /= 3) text += "ab!"[c % 3];
      each(text);
    }
  }
}

// The walks agree, and the sweep, where it answers, answers the walk's extent
// with the walk's own token among them.
void walks_agree(std::string_view lead) {
  const std::uint16_t mode = case_lexguard::mode_of<A>(lead);
  std::vector<Scan> room(A.maxCands != 0 ? A.maxCands : 1);
  each_word([&](const std::string& text) {
    const auto l = static_cast<TextOffset>(text.size());
    const Scan table = cursiv::lr_match_text<A>(text.data(), l, mode, 0);
    const Scan gen = g::Lr::kDrive.walk(text.data(), l, mode, 0);
    const auto got = g::Lr::kDrive.scanText(text.data(), l, mode, 0,
                                            room.data(), A.maxCands);
    const bool ok = table.end == gen.end && table.term == gen.term &&
                    (got == 0 || (table.term != cursiv::kInvalidSymbol &&
                                  room[0].end == table.end &&
                                  held(room.data(), got, table)));
    if (!ok)
      std::printf(
          "`%s` after `%.*s`: table %u/%.*s, walk %u/%.*s, sweep %u (%u)\n",
          text.c_str(), static_cast<int>(lead.size()), lead.data(), table.end,
          static_cast<int>(name_of(table).size()), name_of(table).data(),
          gen.end, static_cast<int>(name_of(gen).size()), name_of(gen).data(),
          got, got != 0 ? room[0].end : 0u);
    CHECK(ok);
  });
}

}  // namespace

void run() {
  reads("head", {{"abab!", "N:ab N:ab ! ", 2, "HeadA"},
                 {"ab!", "N:ab ! ", 2, "HeadA"},
                 {"abab", "W:abab ", 4, "HeadP"},
                 {"ababab!", "W:abab N:ab ! ", 4, "HeadP"}});
  reads("wide", {{"abab!", "N:aba W:b ! ", 3, "WideA"},
                 {"ab!", "W:ab ! ", 2, "WideP"},
                 {"abab", "W:abab ", 4, "WideP"},
                 {"ababab!", "W:ababab ! ", 6, "WideP"}});
  reads("run", {{"abab!", "N:ab W:ab ! ", 2, "RunA"},
                {"ab!", "W:ab ! ", 2, "RunP"},
                {"abab", "W:abab ", 4, "RunP"},
                {"ababab!", "W:ababab ! ", 6, "RunP"}});
  for (const char* lead : {"head", "wide", "run"}) walks_agree(lead);
}
}  // namespace case_refusedshorter

namespace case_alikecandidates {
// Two names read one word. Where every state of the mode answers both alike,
// the table elects and carries no candidate; where a state shifts each, the
// next token decides.

namespace {

std::string items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<talike::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const talike::Item* i : d->items) {
    if (!i->note.empty()) out += std::string(i->note) + '|';
    out += std::string(i->name) + (i->value ? "=" : "") + ' ';
  }
  return out;
}

}  // namespace

void run() {
  static_assert(!talike::generated::Reduced::kTable.multiCand);
  static_assert(tguard::generated::G::kTable.multiCand);
  const auto mode =
      case_lexguard::mode_of<tguard::generated::G::kTable>("shifted");
  CHECK(tguard::generated::G::kTable.modeMulti[mode] != 0);

  const auto reduced = talike::generated::Reduced::create_parser();
  const auto r = reduced->parse(
      "type \"a\" f: \"b\" yes: g: enum \"c\" one two \"d\" yesx no1 \"e\" a");
  CHECK(r.accepted && r.diagnostics.empty());
  CHECK(items(r) == "\"a\"|f \"b\"|yes g \"c\"|one two \"d\"|yesx no1 \"e\"|a ");
  for (const std::string_view bad :
       {"type enum yes", "type enum \"d\" no", "type enum one yes"})
    CHECK(!reduced->parse(bad).diagnostics.empty());

  const auto shifted = tguard::generated::G::create_parser();
  const std::string source =
      "shifted a: b= yes: yesx= c: nox: yes0123456789abcdef= "
      "abcdefghijklmnop0123456789:";
  const auto s = shifted->parse(source);
  CHECK(s.accepted && s.diagnostics.empty());
  CHECK(items(s) ==
        "a b= yes yesx= c nox yes0123456789abcdef= abcdefghijklmnop0123456789 ");
  for (const std::string_view bad : {"yes=", "no=", "a: yes="})
    CHECK(!shifted->parse("shifted " + std::string(bad)).diagnostics.empty());
}
}  // namespace case_alikecandidates

namespace case_keywordhandoff {
// The lexer's rank of a token, the literal over the name, is applied where
// the look-ahead settles a choice and where the graph does: a document reads
// as the same tree on either side of the look-ahead's depth, with fixed AST
// expectations.

namespace {

std::string words(unsigned n, const char* head) {
  std::string text = head;
  for (unsigned i = 0; i < n; ++i) text += "a ";
  return text + "x";
}

template <class Parser>
void reads_as_the_keyword(Parser& parser, const std::string& text,
                          unsigned n) {
  const auto r = parser->parse(text);
  CHECK(r.accepted && r.diagnostics.empty());
  const auto* doc = r.template value_as<tkeyword::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->keyed != nullptr && doc->named == nullptr);
  CHECK(doc->keyed != nullptr && doc->keyed->words.size() == n);
}

template <class Lr>
void reads_the_keyword(Lr& lr, const char* head) {
  constexpr unsigned kDepth = cursiv::kLrLookBudget;
  for (const unsigned n :
       {1u, kDepth - 1, kDepth, kDepth + 1, 4 * kDepth, 3000u}) {
    const std::string text = words(n, head);
    reads_as_the_keyword(lr, text, n);
  }
}

}  // namespace

void run() {
  const auto blr = tguard::generated::G::create_parser();
  reads_the_keyword(blr, "handoff p ");
}
}  // namespace case_keywordhandoff

namespace case_lrreservedchoice {
// A refusal of a choice of words is one question of the token in hand, not one
// per word: the condition has one leaf, and reads as the words it refuses.

namespace {

namespace g = trefuse::head::generated::G;

void the_choice_is_one_leaf() {
  const auto low = cursiv::LrLowering<treserved::G>::table();
  CHECK(low.cfg.ngapLeaves == 1);
  std::printf("  a choice of words is one leaf\n");
}

void the_refused_words_are_not_names() {
  static const auto parser = g::create_parser();
  for (const std::string text : {
           "both ifx; both if1; both if_; both zzz", "both a; both b; both g"}) {
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
  }
  for (const std::string text : {"both if", "both x; both zz", "both if; both zz"}) {
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(!r.diagnostics.empty());
  }
  std::printf("  the refused words are not names\n");
}

}  // namespace

void run() {
  the_choice_is_one_leaf();
  the_refused_words_are_not_names();
}
}  // namespace case_lrreservedchoice

namespace case_cliteral {
// A string literal spells any bytes a grammar holds: what a compiler reads as
// text stays, what it warns of or cannot hold is escaped.

namespace {

void a_bidi_control_is_escaped() {
  CHECK(cursiv::c_literal("a\xe2\x80\xae" "b") == "\"a\\u202eb\"");
  CHECK(cursiv::c_literal("\xd8\x9c") == "\"\\u061c\"");
  CHECK(cursiv::c_literal("\xe2\x81\xa6\xe2\x81\xa9") == "\"\\u2066\\u2069\"");
  CHECK(cursiv::c_literal("\xc3\xa9") == "\"\xc3\xa9\"");
  CHECK(cursiv::c_literal("\xf0\x9f\x98\x80") == "\"\xf0\x9f\x98\x80\"");
  std::printf("  a bidi control is escaped\n");
}

}  // namespace

void run() {
  a_bidi_control_is_escaped();
}
}  // namespace case_cliteral

namespace case_lrlineend {
// A token the lexer refused in its middle ends at its line whatever ends the
// line: one text gives one set of diagnostics with LF, CRLF and a lone CR.

namespace {

namespace g = tfused::generated::Lr;

// Line and column, a CR LF pair and a lone CR each ending one line.
std::string where(std::string_view text, std::size_t offset) {
  unsigned line = 0, column = 0;
  for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
    const bool pair =
        text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n';
    if (pair && i + 1 == offset) break;
    if (pair) ++i;
    if (pair || text[i] == '\n' || text[i] == '\r') {
      ++line;
      column = 0;
    } else {
      ++column;
    }
  }
  return std::to_string(line) + ":" + std::to_string(column);
}

std::string diagnostics_of(const std::string& text) {
  static const auto parser = g::create_parser();
  const cursiv::ParseResult r = parser->parse(text);
  std::string out;
  for (const auto& d : r.diagnostics)
    out += std::to_string(static_cast<int>(d.kind)) + "@" +
           where(text, d.begin) + "-" + where(text, d.end) + " ";
  return out;
}

std::string joined(const std::vector<std::string_view>& lines,
                   std::string_view terminator) {
  std::string out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) out += terminator;
    out += lines[i];
  }
  return out;
}

void a_broken_token_ends_at_its_line_whatever_ends_it() {
  const std::vector<std::vector<std::string_view>> documents = {
      {"'abc", "'def' 'ef'"},
      {"'ab'", "'x", "'y' 'ef'"},
      {"'a' 'ab'", "'b"},
      {"'one", "", "'two' 'ef'"},
      {"'open", "'ab'", "'next'"},
  };
  for (const auto& lines : documents) {
    const std::string lf = diagnostics_of(joined(lines, "\n"));
    CHECK(!lf.empty());
    CHECK(diagnostics_of(joined(lines, "\r\n")) == lf);
    CHECK(diagnostics_of(joined(lines, "\r")) == lf);
  }
  std::printf("  a broken token ends at its line\n");
}

}  // namespace

void run() {
  a_broken_token_ends_at_its_line_whatever_ends_it();
}
}  // namespace case_lrlineend

int main() {
#ifdef CURSIV_LR_LEXER_CONTRACT
  case_lexercontract::run();
#endif
  case_lexguard::run();
  case_refusedheadwords::run();
  case_refusedshorter::run();
  case_alikecandidates::run();
  case_keywordhandoff::run();
  case_lrreservedchoice::run();
  case_cliteral::run();
  case_lrlineend::run();
  candidate_runs::run();
  typo_alternatives::run();
  audit_regression::run();
  wide_terminals::run();
  mem_conv::run();
  return cursiv::test::report("LrLexicon_test");
}
