#pragma once

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/SkippedLeaf.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/parser/DefaultParserErrorMessageProvider.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"

// The recovery corpus as a lane: broken documents, each with the valid twin
// it was cut from. A row checks that the twin parses clean, then the broken
// one's spans, kept and invented tokens, repairs and minimum checks.

namespace cursiv::test {

inline std::string document(std::string_view name) {
  return read_file(std::string(CURSIV_RECOVERY_CORPUS) + "/" +
                   std::string(name));
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

namespace detail {

inline constexpr std::string_view kKind[] = {"ins", "del", "rep", "eof",
                                             "conv", "bnd"};

// The name of what a repair expected: a terminal's spelling, or its name. A
// repair inserts bytes the lexer would match, never a node of a tree above.
inline std::string_view expected_name(const cursiv::Grammar& grammar,
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
    // A skipped leaf is an error span and names no state. A token a repair
    // invented or re-read names the state that took it.
    if (skipped_leaf(grammar, n)) {
      p.errors.emplace_back(n.begin, n.end);
      continue;
    }
    if (n.hidden) continue;
    p.tokens.emplace_back(text, n.begin, n.end - n.begin);
  }
  const cursiv::DefaultParserErrorMessageProvider words;
  for (const cursiv::ParseDiagnostic& d : cst.diagnostics()) {
    // What an editor would name, navigated from the position the repair
    // reports -- a literal by its spelling, a rule by its name -- and then
    // the sentence itself, which is the only part an author ever reads.
    p.diags.emplace_back(std::string(kKind[static_cast<int>(d.kind)]) + ":" +
                         std::string(expected_name(grammar, d.element)) + "@" +
                         std::to_string(d.begin) + "<" +
                         words.buildMessage(grammar, d, text) + ">");
  }
  return p;
}

}  // namespace detail

// Through the type-erased `Parser`, like every other lane: naming the grammar
// here would compile its engine a second time.
inline Proj project(const cursiv::Parser& engine, const std::string& text) {
  const cursiv::ParseResult result = engine.parse(text);
  Proj p = detail::project(engine.getGrammar(), result.cst(), text);
  p.value = result.value != nullptr;
  return p;
}

namespace detail {

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
// reference file holds these; the checks below hold what `--write` cannot
// accept.
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

}  // namespace detail

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
    // Only the reference that was read is refreshed.
    const std::string path = referencePath();
    if (write) {
      // An engine's own file stands only while its repairs differ: once they
      // read like the shared file again, the copy goes.
      if (path != sharedReferencePath() &&
          cursiv::test::read_file(sharedReferencePath()) == text) {
        std::filesystem::remove(path);
        return;
      }
      // Written beside the reference, then renamed over it: a write that
      // fails, down to the last flush, leaves the reference as it was.
      const std::string written = path + ".tmp";
      std::FILE* const f = std::fopen(written.c_str(), "wb");
      bool whole = f != nullptr;
      if (whole) {
        whole = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        whole = std::fclose(f) == 0 && whole;
      }
      std::error_code error;
      if (whole) std::filesystem::rename(written, path, error);
      if (!whole || error) {
        std::filesystem::remove(written, error);
        std::printf("cannot write %s\n", path.c_str());
        ++failures;
      }
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

 private:
  static std::string sharedReferencePath() {
    return std::string(CURSIV_RECOVERY_CORPUS) + "/REFERENCE.txt";
  }

  // Where this engine's own reference stands, when it has one. Without it, this
  // engine reads the corpus as the shared file says, and `--write` refreshes
  // that file rather than creating this one.
  static std::string ownReferencePath() {
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
  static std::string referencePath() {
    const std::string own = ownReferencePath();
    return std::filesystem::exists(own) ? own : sharedReferencePath();
  }
};

// One language: the valid twin, then every broken document of the corpus.
inline void language(Lane& lane, const cursiv::Parser& engine,
                     std::string_view validName,
                     const std::vector<std::string>& broken) {
  const Proj valid = project(engine, document(validName));
  // The valid twin is clean. Everything below is read against it.
  if (!valid.accepted || valid.recovered) {
    std::printf(
        "FAIL %s: the valid document is not taken cleanly (acc=%d "
        "rec=%d)\n",
        lane.lang.c_str(), int(valid.accepted), int(valid.recovered));
    ++failures;
    return;
  }
  lane.row(validName, detail::signature(valid, valid.tokens.size(), 0));
  for (const std::string& name : broken) {
    const std::string text = document(name);
    if (text.empty()) {
      std::printf("FAIL %s/%s: document not found\n", lane.lang.c_str(),
                  name.c_str());
      ++failures;
      continue;
    }
    const Proj p = project(engine, text);
    const int inv = detail::invented(p.tokens, valid.tokens);
    lane.row(name, detail::signature(p, valid.tokens.size(), inv));
    if (!p.monotone) {
      std::printf("FAIL %s/%s: a record's end decreases along the tape\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    // A broken document is never taken as a clean one: it is repaired,
    // or it is refused. Silently accepting a break is the one answer an
    // editor cannot work with.
    if (p.accepted && !p.recovered) {
      std::printf("FAIL %s/%s: a broken document accepted with no repair\n",
                  lane.lang.c_str(), name.c_str());
      ++failures;
    }
    // An accepted document has a tree: a repair that leaves the builder
    // nothing to build has not repaired anything.
    if (p.accepted && !p.value) {
      std::printf("FAIL %s/%s: accepted without an AST\n", lane.lang.c_str(),
                  name.c_str());
      ++failures;
    }
    // Invention is re-reading: a token the valid document lacks is the same
    // bytes read another way. Bounded, so a repair that guesses fails.
    if (inv > 4) {
      std::printf("FAIL %s/%s: %d tokens re-read -- a repair is guessing\n",
                  lane.lang.c_str(), name.c_str(), inv);
      ++failures;
    }
    // The three checks below, which `--write` cannot accept.
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
