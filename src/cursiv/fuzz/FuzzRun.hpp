// The lane: one grammar, N documents written by the declarative walk, and one
// verdict on each -- the erased door reads the document clean, and the AST it
// builds matches the grammar's own directives. Nothing here names an engine;
// and nothing here reads a table.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/fuzz/AstCompare.hpp"
#include "cursiv/fuzz/AstExcused.hpp"
#include "cursiv/fuzz/CstInvariants.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "cursiv/fuzz/CursivLexicon.hpp"
#include "cursiv/fuzz/DeclaredModel.hpp"
#include "cursiv/fuzz/GrammarCost.hpp"
#include "cursiv/fuzz/GrammarDerivation.hpp"
#include "cursiv/fuzz/GrammarGenerator.hpp"
#include "cursiv/fuzz/SeededSource.hpp"
#include "cursiv/fuzz/TapeSeal.hpp"
#include "cursiv/test/Check.hpp"

namespace cursiv::fuzzer {

// One instance per compiled grammar, for the whole program.
template <class G>
inline const CursivLexicon<G> kLexicon{kDeclaredModel<G>};
template <class G>
inline const CursivActions<G> kActions{};

// A document may hold any byte a character range admits, so a report shows it
// escaped rather than letting a control byte cut the line.
inline void show(std::string_view text) {
  for (const char c : text)
    if (c >= 0x20 && c < 0x7f)
      std::putchar(c);
    else
      std::printf("\\x%02x", static_cast<unsigned char>(c));
}

[[nodiscard]] inline unsigned popcount(unsigned v) {
  unsigned n = 0;
  for (; v != 0; v >>= 1) n += v & 1u;
  return n;
}

struct Lane {
  std::uint64_t docs = 0, complete = 0, refused = 0, repaired = 0;
  std::uint64_t bytes = 0;
  std::uint64_t capHits = 0, unrendered = 0;
  // The oracle: the tree the grammar builds against the tree the engine built.
  std::uint64_t judged = 0, treeGaps = 0;
  // A tree the walk did not model is this harness's limit; a tree the engine
  // did not build is a broken contract. Never the same skip.
  std::uint64_t unmodelled = 0, nullTrees = 0;
  // The tape, weighed on what a postorder tree owes whatever wrote it.
  std::uint64_t tapes = 0, tapeGaps = 0;
  unsigned styles = 0;
  // What no instrument reaches, by name: a member the walk could not write and
  // a member no reader reads. Neither is ever counted as an agreement.
  std::map<std::string, unsigned> unwritten;
  std::map<std::string, unsigned> blind;
};

// Every column of every record of one tree, in one number, so two tapes are
// weighed on the same scale.
inline std::uint64_t tape_seal(const cursiv::Cst& c) {
  TapeSeal s;
  seal_records(c, s);
  return s.digest;
}

template <class G>
void run(const char* lang, const cursiv::Parser& parser,
         const cursiv::AstReflection& reflection, unsigned docs,
         std::uint64_t seed) {
  const GrammarCost cost(kDeclaredModel<G>, kActions<G>);
  Lane lane;

  for (unsigned i = 0; i < docs; ++i) {
    SeededSource src(seed + std::uint64_t{i} * 0x9e3779b97f4a7c15ull);
    // The arena owns the walk's own tree and the bytes its values view; it
    // dies with the document that was judged on it.
    std::pmr::monotonic_buffer_resource pool;
    cursiv::AstArena arena(&pool);
    GrammarGenerator gen{kDeclaredModel<G>,
                         kLexicon<G>,
                         kActions<G>,
                         cost,
                         3 + (i % 12) * 3,
                         nullptr};
    const GrammarDerivation d = gen.generate(src, arena);

    ++lane.docs;
    lane.capHits += d.capHits;
    lane.unrendered += d.unrendered;
    if (!d.complete) continue;
    ++lane.complete;
    lane.bytes += d.text.size();
    lane.styles |= d.styles;

    const cursiv::ParseResult r = parser.parse(d.text);
    if (!r.cst().accepted()) {
      if (++lane.refused == 1) {
        std::printf("FAIL %s: a generated document is refused: <<", lang);
        show(d.text);
        std::printf(">>\n");
      }
      continue;
    }
    if (r.cst().recovered()) {
      if (++lane.repaired == 1) {
        std::printf("FAIL %s: a generated document is repaired: <<", lang);
        show(d.text);
        std::printf(">>\n");
      }
      continue;
    }

    ++lane.tapes;
    const CstInvariants::Result inv = CstInvariants{}.run(r.cst(), d.text);
    if (!inv.ok && ++lane.tapeGaps == 1)
      std::printf(
          "FAIL %s: the tape the parser wrote is not a tree: %s at record %u "
          "for <<%s>>\n",
          lang, inv.why.c_str(), inv.at, d.text.c_str());


    // The oracle. The CST is an intermediate form and is not predicted here:
    // a prediction of what the engine owes would be its reading checked
    // against a copy of itself.
    // Accepted and unrepaired: an engine that builds no tree here has broken
    // the contract, and a skip would count it nowhere.
    if (r.value == nullptr) {
      if (++lane.nullTrees == 1)
        std::printf(
            "FAIL %s: a document is accepted with no tree built: <<%s>>\n",
            lang, d.text.c_str());
      continue;
    }
    if (d.tree == nullptr) {
      ++lane.unmodelled;
      continue;
    }
    ++lane.judged;
    for (const AstExcused& x : d.unwritten) ++lane.unwritten[x.member];
    const AstCompare::Result diff =
        AstCompare{reflection, kActions<G>}.run(*d.tree, *r.value, d.unwritten);
    for (const std::string& n : diff.blind) ++lane.blind[n];
    if (!diff.equal && ++lane.treeGaps == 1)
      std::printf(
          "FAIL %s/#%u at %s: the grammar builds <%s>, the engine built <%s> "
          "for <<%s>>\n",
          lang, i, diff.where.c_str(), diff.want.c_str(), diff.got.c_str(),
          d.text.c_str());
  }

  std::printf(
      "fuzz %-14s %5llu docs (%llu complete, %llu bytes) | render styles %u/4  "
      "caps=%llu unrendered=%llu\n",
      lang, (unsigned long long)lane.docs, (unsigned long long)lane.complete,
      (unsigned long long)lane.bytes, popcount(lane.styles),
      (unsigned long long)lane.capHits, (unsigned long long)lane.unrendered);
  std::printf(
      "     ORACLE: %llu trees judged against the grammar's directives, %llu "
      "gaps | refused=%llu repaired=%llu unmodelled=%llu\n",
      (unsigned long long)lane.judged, (unsigned long long)lane.treeGaps,
      (unsigned long long)lane.refused, (unsigned long long)lane.repaired,
      (unsigned long long)lane.unmodelled);
  std::printf("     TAPE: %llu weighed as postorder trees, %llu gaps\n",
              (unsigned long long)lane.tapes,
              (unsigned long long)lane.tapeGaps);
  for (const auto& [name, n] : lane.blind)
    std::printf(
        "    blind spot: `%s` (%u values) -- no reader reaches it, so it is "
        "named rather than weighed\n",
        name.c_str(), n);
  for (const auto& [name, n] : lane.unwritten)
    std::printf(
        "    not modelled: `%s` (%u writes) -- the walk holds no reading of "
        "this member, and never claims one\n",
        name.c_str(), n);

  // The gates. A lane that generated nothing, or judged nothing, proves
  // nothing -- that is a failure of its own.
  if (lane.complete == 0 || lane.judged == 0) {
    std::printf("FAIL %s: the lane decides nothing\n", lang);
    ++failures;
  }
  if (lane.refused != 0 || lane.repaired != 0) ++failures;
  if (lane.treeGaps != 0 || lane.tapeGaps != 0) ++failures;
  if (lane.nullTrees != 0) ++failures;
  // Every tape weighed is either judged or declared unmodelled: a document that
  // left by some other door would leave the count silent about it.
  if (lane.judged + lane.unmodelled + lane.nullTrees != lane.tapes) {
    std::printf("FAIL %s: %llu tapes, %llu judged, %llu unmodelled\n", lang,
                (unsigned long long)lane.tapes, (unsigned long long)lane.judged,
                (unsigned long long)lane.unmodelled);
    ++failures;
  }
}

}  // namespace cursiv::fuzzer
