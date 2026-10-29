#pragma once

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/validation/RepairMessage.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/SkippedLeaf.hpp"

// The recovery corpus as a lane: fifty broken documents over five shipped
// languages, each a named break (a typo, a closer gone, an intruder, a
// truncation -- the corpus manifest names each), plus the valid twin
// every one of them was cut from.
//
// What a row asks, and the last one is the property the whole feature lives
// or dies by:
//
//   1. The valid twin parses clean -- `accepted && !recovered`. A repair
//      that fires on a correct document is a defect, whatever it recovers.
//   2. The broken one is accepted, with its error spans situated: the
//      signature carries every span, so a repair that drifts by a byte is a
//      line that moved, not a bit that flipped.
//   3. Completeness: how much of the valid document's token list survives.
//      One number per document, pinned -- a repair that gives up early
//      shows as a number that fell.
//   4. Nothing invented: every token of the repaired tree is a token of the
//      valid tree, counted as a multiset. A recovery may lose text; it may
//      never make text up.
//   5. What each repair says: its kind, the position it names, and the
//      sentence an author is shown -- rendered here by the very function the
//      validator calls, so a wording that drifts is a line that moved.
//   6. The floor, in `language` below: what no regeneration may bless.

namespace cursiv::test {

inline std::string document(std::string_view name) {
  return read_file(std::string(CURSIV_RECOVERY_CORPUS) + "/" +
                   std::string(name));
}

inline std::string shared_reference_path() {
  return std::string(CURSIV_RECOVERY_CORPUS) + "/REFERENCE.txt";
}

// Where this engine's own reference stands, when it has one. Its absence is
// the statement that this engine reads the corpus the way the shared file
// says, and `--write` then refreshes that file rather than minting this one.
inline std::string own_reference_path() {
  const std::string dir(CURSIV_RECOVERY_CORPUS);
#if defined(CURSIV_ENGINE_LR)
  return dir + "/REFERENCE-lr.txt";
#elif defined(CURSIV_ENGINE_LL)
  return dir + "/REFERENCE-ll.txt";
#else
  return dir + "/REFERENCE.txt";
#endif
}

// The reference a lane is held to is the engine's own where its repairs
// differ: a corpus with no `REFERENCE-<engine>.txt` beside it is one every
// engine reads the same way.
inline std::string reference_path() {
  const std::string own = own_reference_path();
  return std::filesystem::exists(own) ? own : shared_reference_path();
}

// The tokens a tree carries: the visible leaves, trivia and error spans
// left out -- what the document still says after the repair.
struct Proj {
  std::vector<std::string> tokens;
  std::vector<std::pair<cursiv::TextOffset, cursiv::TextOffset>> errors;
  // One per diagnostic: what the repair did, and where.
  std::vector<std::string> diags;
  bool accepted = false;
  bool recovered = false;
  // Whether the tree reaches the last byte. Not the same question as
  // `accepted`: a document may be out of the language and still be covered,
  // which is what an editor needs -- features stop where the tree stops.
  bool covered = false;
  // Record ends never decrease along a tape: a run laid twice breaks it.
  bool monotone = true;
  // The builder made a tree of it.
  bool value = false;
};

inline constexpr std::string_view kKind[] = {"ins", "del", "rep", "eof",
                                             "conv", "bnd"};

// What a repair was owed, named: a terminal is spelled by the grammar or it is
// named, and nothing else is owed -- a repair inserts bytes the lexer would
// match, never a position of a tree above.
inline std::string_view owed_name(const cursiv::Grammar& grammar,
                                  cursiv::ElementId e) {
  if (e == cursiv::kNoElement) return "?";
  const auto spellings = grammar.terminalSpellings(e);
  if (spellings.size() == 1) return spellings.front();
  const std::string_view n = grammar.terminalName(e);
  return n.empty() ? "?" : n;
}

inline Proj project(const cursiv::Grammar& grammar, const cursiv::Cst& cst,
                    const std::string& text) {
  Proj p;
  p.accepted = cst.accepted();
  p.recovered = cst.recovered();
  // Up to the trailing blanks: ignored whitespace commits no node, so the
  // last one always stops short of a file's final newline.
  cursiv::TextOffset reach = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    if (cst[i].end > reach) reach = cst[i].end;
  p.covered = true;
  for (std::size_t i = reach; i < text.size(); ++i)
    if (static_cast<unsigned char>(text[i]) > ' ') p.covered = false;
  for (cursiv::NodeId i = 1; i < cst.size(); ++i)
    if (cst[i].end < cst[i - 1].end) p.monotone = false;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (n.subtreeSize != 1) continue;  // leaves alone
    // An error span is text the repair threw away: a skipped leaf names no
    // state, since no shift ever reached it. A token the repair invented and
    // one it re-read as the terminal owed both name the state that took
    // them -- both are still tokens, counted by `inv` against the valid twin.
    if (skipped_leaf(grammar, n)) {
      p.errors.emplace_back(n.begin, n.end);
      continue;
    }
    if (n.hidden) continue;
    p.tokens.emplace_back(text, n.begin, n.end - n.begin);
  }
  for (const cursiv::ParseDiagnostic& d : cst.diagnostics()) {
    // What an editor would name, navigated from the position the repair
    // reports -- a literal by its spelling, a rule by its name -- and then
    // the sentence itself, which is the only part an author ever reads.
    const bool sane = d.begin <= d.end && d.end <= text.size();
    const std::string_view over =
        sane ? std::string_view(text).substr(d.begin, d.end - d.begin)
             : std::string_view{};
    p.diags.emplace_back(
        std::string(kKind[static_cast<int>(d.kind)]) + ":" +
        std::string(owed_name(grammar, d.element)) + "@" +
        std::to_string(d.begin) + "<" +
        cursiv::repair_message(grammar, d, over, d.end >= text.size()) + ">");
  }
  return p;
}

// Through the erased door, like every other lane: the language's own module is
// where its engine is compiled, and a lane that named the grammar would build
// the state chain a second time.
inline Proj project(const cursiv::Parser& engine, const std::string& text) {
  const cursiv::ParseResult result = engine.parse(text);
  Proj p = project(engine.getGrammar(), result.cst(), text);
  p.value = result.value != nullptr;
  return p;
}

// How many tokens of the repaired tree the valid tree does not have, as a
// multiset -- the invention count.
inline int invented(const std::vector<std::string>& got,
                    const std::vector<std::string>& valid) {
  std::map<std::string, int> left;
  for (const std::string& t : valid) ++left[t];
  int n = 0;
  for (const std::string& t : got) {
    const auto it = left.find(t);
    if (it == left.end() || it->second == 0) {
      ++n;
      continue;
    }
    --it->second;
  }
  return n;
}

// One line per document: the verdict, the spans, the two numbers. The
// reference file holds these; the properties below hold what no
// regeneration may bless.
inline std::string signature(const Proj& p, std::size_t validTokens, int inv) {
  std::string s = "acc=" + std::to_string(int(p.accepted)) +
                  " cov=" + std::to_string(int(p.covered)) +
                  " rec=" + std::to_string(int(p.recovered)) +
                  " tok=" + std::to_string(p.tokens.size()) + "/" +
                  std::to_string(validTokens) + " inv=" + std::to_string(inv);
  for (const auto& [b, e] : p.errors)
    s += " [" + std::to_string(b) + "," + std::to_string(e) + ")";
  for (const std::string& d : p.diags) s += " " + d;
  return s;
}

// The lane's state: rows collected, then compared to (or written into) the
// language's reference file.
struct Lane {
  std::string lang;
  std::vector<std::string> lines;
  bool write = false;

  void row(std::string_view name, const std::string& sig) {
    lines.emplace_back(std::string(name) + "\t" + sig);
  }
  void close() const {
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    // WHAT IS REFRESHED IS WHAT WAS READ. Writing a second file wherever the
    // answer moved forked the reference under the engine that owns the corpus
    // and left the shared one to rot; an engine that needs a reference of its
    // own is one whose file already stands beside it.
    const std::string path = reference_path();
    if (write) {
      // An engine's own file stands only while its repairs differ: once they
      // read like the shared file again, the copy goes.
      if (path != shared_reference_path() &&
          cursiv::test::read_file(shared_reference_path()) == text) {
        std::filesystem::remove(path);
        return;
      }
      std::FILE* const f = std::fopen(path.c_str(), "w");
      if (f == nullptr) {
        std::printf("cannot write %s\n", path.c_str());
        ++failures;
        return;
      }
      std::fwrite(text.data(), 1, text.size(), f);
      std::fclose(f);
      return;
    }
    const std::string want = cursiv::test::read_file(path);
    if (want == text) return;
    ++failures;
    std::printf("FAIL %s: the repairs moved\n", lang.c_str());
    std::size_t a = 0, b = 0;
    while (a < text.size() || b < want.size()) {
      const std::size_t ea = text.find('\n', a), eb = want.find('\n', b);
      const std::string la = text.substr(a, ea - a),
                        lb = want.substr(b, eb - b);
      if (la != lb)
        std::printf("  got  %s\n  want %s\n", la.c_str(), lb.c_str());
      if (ea == std::string::npos || eb == std::string::npos) break;
      a = ea + 1;
      b = eb + 1;
    }
    std::printf("  regenerate with: recovery_test --write\n");
  }
};

// One language: the valid twin, then every broken document of the corpus.
inline void language(Lane& lane, const cursiv::Parser& engine,
                     std::string_view validName,
                     const std::vector<std::string>& broken) {
  const Proj valid = project(engine, document(validName));
  // (1) The valid twin is clean. Everything below is read against it.
  if (!valid.accepted || valid.recovered) {
    std::printf(
        "FAIL %s: the valid document is not taken cleanly (acc=%d "
        "rec=%d)\n",
        lane.lang.c_str(), int(valid.accepted), int(valid.recovered));
    ++failures;
    return;
  }
  lane.row(validName, signature(valid, valid.tokens.size(), 0));
  for (const std::string& name : broken) {
    const std::string text = document(name);
    if (text.empty()) {
      std::printf("FAIL %s/%s: document not found\n", lane.lang.c_str(),
                  name.c_str());
      ++failures;
      continue;
    }
    const Proj p = project(engine, text);
    const int inv = invented(p.tokens, valid.tokens);
    lane.row(name, signature(p, valid.tokens.size(), inv));
    if (!p.monotone) {
      std::printf("FAIL %s/%s: a record's end decreases along the tape\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    // (2) A broken document is never taken as a clean one: it is repaired,
    // or it is refused. Silently accepting a break is the one answer an
    // editor cannot work with.
    if (p.accepted && !p.recovered) {
      std::printf("FAIL %s/%s: a broken document accepted with no repair\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    // (3) An accepted document has a tree: a repair that leaves the builder
    // nothing to build has not repaired anything.
    if (p.accepted && !p.value) {
      std::printf("FAIL %s/%s: accepted without an AST\n", lane.lang.c_str(),
                  name.c_str());
      ++failures;
    }
    // (4) Invention is re-reading, not fabrication. No repair inserts text
    // -- spans are skipped and elements retried -- so a token the valid
    // document lacks is the same bytes read another way (`de` where `def`
    // was meant). The number is pinned rather than forbidden: it moves when
    // a repair starts guessing.
    if (inv > 4) {
      std::printf("FAIL %s/%s: %d tokens re-read -- a repair is guessing\n",
                  lane.lang.c_str(), name.c_str(), inv);
      ++failures;
    }
    // (6) The floor, and it is what `--write` may not reach. A signature
    // records what the recovery did, and regenerating it is how a
    // deliberate change lands, so nothing above refuses a row saying the
    // recovery did nothing -- the floor is the three checks below instead.
    if (!p.covered) {
      std::printf("FAIL %s/%s: the tree does not reach the last byte\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    if (!p.recovered) {
      std::printf("FAIL %s/%s: a break with nothing said about it\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    if (p.tokens.size() * 2 < valid.tokens.size()) {
      std::printf("FAIL %s/%s: %zu of %zu tokens kept -- the repair gave up\n",
                  lane.lang.c_str(), name.c_str(), p.tokens.size(),
                  valid.tokens.size());
      ++failures;
    }
  }
}

}  // namespace cursiv::test
