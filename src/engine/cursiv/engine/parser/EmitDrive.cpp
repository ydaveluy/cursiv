#include "cursiv/engine/parser/EmitDrive.hpp"
#include "cursiv/engine/generator/Generate.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engine/generator/EmitArray.hpp"

namespace cursiv::engine::gen {

namespace {

// One test of a run over a register: the bytes of `[lo, hi]` under
// `mask`.
struct Lane {
  unsigned lo = 0;
  unsigned hi = 0;
  unsigned mask = 0x7f;
};

// The ASCII ranges of a set, folding the two cases of a letter into one range.
void lanes_of(const ByteSet& set, std::vector<Lane>& lanes) {
  for (unsigned b = 0; b < 0x80; ++b) {
    if (!set.has(b)) continue;
    unsigned hi = b;
    while (hi + 1 < 0x80 && set.has(hi + 1)) ++hi;
    lanes.push_back({b, hi, 0x7f});
    b = hi;
  }
  // An upper-case range and the same one in lower case read as one.
  for (std::size_t i = 0; i < lanes.size(); ++i) {
    if (lanes[i].lo < 0x40 || lanes[i].hi > 0x5f) continue;
    for (std::size_t j = i + 1; j < lanes.size(); ++j)
      if (lanes[j].lo == lanes[i].lo + 0x20 && lanes[j].hi == lanes[i].hi + 0x20) {
        lanes[i].mask = 0x5f;
        lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(j));
        break;
      }
  }
}

class Emitter {
 public:
  Emitter(const Cfg& cfg, const Packed& packed)
      : _cfg(cfg),
        _p(packed),
        _lexed(packed.states.size(), false),
        _dispatched(packed.states.size(), false) {
    for (const Trivia trivia : packed.terminalTrivia)
      _hides = _hides || trivia == Trivia::hidden;
    _splits = std::ranges::any_of(packed.states, [&](const PackedState& state) {
      return state.reduce == kNone && has_arms(state);
    });
  }

  void run(const LanguageNames& names, WriteSource write) {
    const auto ns = names.ns;
    twins();
    shared_shifts();
    std::set<std::uint32_t> marks;
    for (const auto& opening : _cfg.openings)
      for (const auto& [mark, depth] : opening) marks.insert(mark);
    for (const auto mark : marks) {
      std::set<std::uint32_t> states;
      for (const auto& site : _p.gotos[mark]) states.insert(site.target);
      line("inline bool guard", mark, "(std::uint32_t s) {");
      line("  switch (s) {");
      for (const auto state : states) line("    case ", state, ": return true;");
      line("    default: return false;");
      line("  }");
      line("}");
    }
    const std::string shiftRows = take();
    // The states, then the reductions they reach: each asks for more labels.
    // The start state is entered with no token in hand.
    enter(0, false);
    // So are the states a condition's text is read from.
    for (const std::uint32_t reads : _p.conditions) enter(reads, false);
    std::vector<std::string> states(_p.states.size());
    std::vector<std::vector<std::uint32_t>> twinsOf(states.size());
    for (std::uint32_t s = 0; s < states.size(); ++s)
      twinsOf[_dispatchAs[s]].push_back(s);
    for (std::uint32_t s = 0; s < states.size(); ++s) {
      if (_p.states[s].reduce != kNone || _dispatchAs[s] != s) continue;
      const auto& twins = twinsOf[s];
      if (twins.size() > 1 && has_arms(_p.states[s])) {
        if (_twinRank.empty()) _twinRank.resize(states.size());
        for (std::uint32_t i = 1; i < twins.size(); ++i) _twinRank[twins[i]] = i;
      }
      _caller = "D" + std::to_string(s);
      dispatch(s, twins);
      states[s] = take();
    }
    // The actions of the cells with several: each a point a run goes on from,
    // one label for the arms that act alike. An arm of a split of tied
    // terminals reads the token as its own terminal first.
    // Per terminal the matcher's token may stand for, the ties it is in.
    std::map<std::uint16_t, std::vector<std::uint32_t>> tiesOf;
    for (std::uint32_t tie = 0; tie < _p.ties.size(); ++tie)
      for (const std::uint16_t terminal : _p.ties[tie].terminals)
        if (_matchTied.contains(terminal)) tiesOf[terminal].push_back(tie);
    std::map<std::tuple<PackedAction::Key, ArmToken, std::uint16_t>, std::string> arms;
    std::vector<std::string> armLabels;
    for (std::size_t arm = 0; arm < _arms.size(); ++arm) {
      const ArmToken source = _armTokens[arm];
      const bool ofTie = source == ArmToken::tied;
      auto [it, fresh] = arms.try_emplace(
          {_arms[arm].key(_cfg), source, source == ArmToken::held ? std::uint16_t{0} : _arms[arm].terminal});
      if (fresh) {
        if (source == ArmToken::held) it->second = destination(_arms[arm]);
        if (it->second.empty()) it->second = "C" + std::to_string(arm);
      }
      const std::string& label = it->second;
      armLabels.push_back(label);
      if (!fresh || label.front() != 'C') continue;
      open_function(label);
      // Where the matcher read the token, a terminal takes its action where it
      // reads the text alike, or ties by the automaton with one that does; the
      // token is read as the terminal of the action taken.
      const std::uint16_t terminal = _arms[arm].terminal;
      if (source == ArmToken::rival) {
        const auto mode = _p.solo[terminal];
        line("  tok = m.lexing().t.modes[", mode, "].read(m.lexing(), m.lexing().src + tok.begin(m.lexing()));");
        line("  if (tok.terminal() != ", terminal, ") return m.refused({tokenEnd, tokenHead});");
      } else if (ofTie && _p.solo[terminal] != kNoMode) {
        line("  if (tok.terminal() != ", terminal, " && !", reads_as(terminal),
             ") return m.refused(tok);");
      } else if (ofTie && _matchTied.contains(terminal)) {
        std::string reads = "m.lexing().reads_tied(tok, " + std::to_string(terminal) + ")";
        for (const std::uint32_t tie : tiesOf[terminal]) {
          const Packed::Tie& tied = _p.ties[tie];
          if (tied.row == kNone || tied.terminals.front() == terminal) continue;
          reads += " || (m.lexing().reads_tied(tok, " + std::to_string(tied.terminals.front()) +
                   ") && m.lexing().tied(tok, " + std::to_string(tied.row) + "))";
        }
        line("  if (!(", reads, ")) return m.refused({tokenEnd, tokenHead});");
      }
      if (ofTie)
        line("  tok = m.lexing().as_tied(tok, ", terminal, _p.mixed[terminal] ? ", true);" : ", false);");
      act(_arms[arm], "  ", label);
      line("}");
    }
    for (std::size_t n = 0; n < _shifts.size(); ++n) {
      const std::string label = "T" + std::to_string(n);
      open_function(label);
      shift(_shifts[n], "  ", label);
      line("}");
    }
    // A state that reduces with no token reduces with one in hand too: a
    // reading that leaves a node of several entries by another one reduces
    // again from the state.
    for (const PackedState& state : _p.states)
      if (state.reduce != kNone && _reduced.contains({state.reduce, false}))
        reduce_label(state.reduce, true);
    for (std::size_t r = 0; r < _reduces.size(); ++r)
      reduce(_reduces[r].first, _reduces[r].second);
    // Each reachable state enters its mode's reader or its shared dispatch.
    std::vector<unsigned> readers(_p.modeCount);
    std::vector<bool> lexes(states.size(), false);
    std::vector<bool> dispatches(states.size(), false);
    for (std::uint32_t s = 0; s < states.size(); ++s) {
      if (!_lexed[s] && !_dispatched[s]) continue;
      lexes[_lexAs[s]] = true;
      dispatches[_dispatchAs[s]] = true;
    }
    for (std::uint16_t mode = 0; mode < _p.modeCount; ++mode) _firsts.push_back(firsts(mode));
    for (std::uint32_t s = 0; s < states.size(); ++s) {
      const std::uint16_t mode = _p.states[s].mode;
      const std::string dispatchLabel = "D" + std::to_string(_dispatchAs[s]);
      if (lexes[s]) {
        ++readers[mode];
        open_function("S" + std::to_string(s));
        if (_cfg.indent != 0 || _cfg.dedent != 0) {
          // A line's indentation is read before its first token.
          line("  if (m.indented.depth == 0 && m.lexing().line_start(p)) [[unlikely]]");
          line("    if (const Token made = m.lexing().indentation(p, m.indented); made.end != nullptr)");
          line("      CURSIV_MUSTTAIL return ", call(dispatchLabel), "(m, p, open, made.end, made.head);");
        }
        line("  if constexpr (M::kSteps) tok = m.lexing().t.modes[", mode, "].read(m.lexing(), p);");
        line("  else tok = full", mode, "<true>(m.lexing(), p);");
        line("  ", jump(dispatchLabel));
        line("}");
      }
      if (dispatches[s]) {
        open_function(dispatchLabel);
        _out += states[s];
        line("}");
      }
    }
    if (_refutes) again();
    const std::string functions = take();
    // Modes reachable from the parser and its guards.
    _read.resize(_p.modeCount);
    for (std::uint32_t s = 0; s < states.size(); ++s) {
      for (const PackedAction& action : _p.states[s].actions)
        if (action.gap < kNoGap) _read[action.gap] = true;
      for (const PackedAction& action : _p.states[s].actions)
        if (action.kind == PackedAction::Kind::reduce) {
          for (const Test& test : _cfg.productions[action.value].tests)
            if (!test.shadows.empty()) _read[test.alphabet] = true;
        } else if (action.kind == PackedAction::Kind::reread) {
          _read[action.value] = true;
          for (const auto rival : _p.retryTerminals[action.rivals]) _read[_p.solo[rival]] = true;
        }
      if (!_lexed[s] && !_dispatched[s]) continue;
      _read[_p.states[s].mode] = true;
      if (_refutes && _p.states[s].again != kNoMode) _read[_p.states[s].again] = true;
    }
    _sharedWords.resize(_p.wordPaths.size());
    for (std::uint16_t mode = 0; mode < _p.modeCount; ++mode)
      if (_read[mode])
        for (const auto& [code, bytes] : _firsts[mode].byCode)
          if (code >= kFirstWord) ++_sharedWords[code & kFirstIndex];
    for (std::size_t w = 0; w < _sharedWords.size(); ++w) {
      const WordPath& path = _p.wordPaths[w];
      const auto ignored = [&](auto t) { return t != kNoToken && _p.terminalTrivia[t] == Trivia::ignored; };
      if (_sharedWords[w] < 2 || path.spelled.empty() || ignored(path.terminal) ||
          std::ranges::any_of(path.spelled, [&](const auto& x) { return ignored(x.terminal); })) {
        _sharedWords[w] = 0;
        continue;
      }
      line("CURSIV_NOINLINE inline Token word", w, "(Machine& m, const char* p, TextOffset begin, const char* stop) {");
      line("  const Tables& t = m.t;");
      line("  [[maybe_unused]] const char* const end = m.end;");
      word(path);
      line("}");
    }
    for (std::uint16_t mode = 0; mode < _p.modeCount; ++mode)
      if (_read[mode]) lexer(mode, readers[mode]);
    const std::string lexers = take();
    // Resume points whose states or arms act alike share one entry address.
    std::vector<std::string> resumes(2 * states.size(), "refuse");
    for (std::uint32_t s = 0; s < states.size(); ++s) {
      if (const std::uint32_t lone = _p.states[s].reduce; lone != kNone) {
        if (_reduced.contains({lone, false})) resumes[2 * s] = "R" + std::to_string(lone) + "n";
        if (_reduced.contains({lone, true})) resumes[2 * s + 1] = "R" + std::to_string(lone) + "h";
        continue;
      }
      if (!_lexed[s] && !_dispatched[s]) continue;
      resumes[2 * s] = "S" + std::to_string(_lexAs[s]);
      resumes[2 * s + 1] = "D" + std::to_string(_dispatchAs[s]);
    }
    resumes.insert(resumes.end(), armLabels.begin(), armLabels.end());
    std::map<std::string, std::uint32_t> points;
    std::vector<std::uint32_t> resume;
    for (const std::string& label : resumes)
      resume.push_back(points.try_emplace(label, points.size()).first->second);
    std::vector<std::string> entries(points.size());
    for (const auto& [name, i] : points) entries[i] = name;

    const auto* twinType = states.size() <= 0x10000 ? "::std::uint16_t" : "::std::uint32_t";
    const auto* resumeType = points.size() <= 0x10000 ? "::std::uint16_t" : "::std::uint32_t";
    line("// GENERATED by the parser generator -- do not edit.");
    line("#pragma once");
    line("#include <cstdint>");
    line("#include \"cursiv/engine/parser/Language.hpp\"");
    line("");
    line("namespace ", ns, " {");
    line("extern const ::cursiv::engine::Tables kTables;");
    line("extern const ::cursiv::engine::AstTables kAst;");
    line("extern const ::cursiv::engine::GrammarFacts kGrammar;");
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility push(hidden)");
    line("#endif");
    if (!_twinRank.empty()) line("extern const ", twinType, " kTwinRank[];");
    line("extern const ", resumeType, " kResume[];");
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility pop");
    line("#endif");
    line("}  // namespace ", ns);
    write("Table.hpp", take());

    line("// GENERATED by the parser generator -- do not edit.");
    line("#pragma once");
    line("#include <bit>");
    line("#include <cstdint>");
    line("#include <cstring>");
    line("");
    line("#include \"Table.hpp\"");
    line("#include \"cursiv/engine/parser/Explorer.hpp\"");
    line("#include \"cursiv/engine/parser/Machine.hpp\"");
    line("#include \"cursiv/engine/parser/Repairer.hpp\"");
    line("");
    line("// The states hand the token on by value: no address of theirs outlives a");
    line("// tail call, which GCC cannot tell unoptimised.");
    line("#if defined(__GNUC__) && !defined(__clang__)");
    line("#pragma GCC diagnostic ignored \"-Wmaybe-musttail-local-addr\"");
    line("#endif");
    line("");
    line("namespace ", ns, " {");
    line("namespace {");
    line("");
    line("using namespace ::cursiv::engine;");
    line("using ::cursiv::NodeCount;");
    line("using ::cursiv::TextOffset;");
    line("");
    std::array<std::uint8_t, 256> skip;
    for (unsigned b = 0; b < skip.size(); ++b) skip[b] = _p.skipped.has(b);
    array("::std::uint8_t", "kSkip", skip);
    for (std::size_t r = 0; r < _p.runBytes.size(); ++r) scanner(r);
    fuses();
    openings();
    _out += lexers;
    // The states over machine `M`, a function a label, each going on to the
    // next by a tail call: what a run reads from the text `p`, the token in
    // hand `tok` and the record `open` a reduction left.
    line("// The ends of a run, entered as the states are.");
    line("template <class M>");
    line("[[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE Exit accepted", kParameters, " {");
    line("  m.accept();");
    line("  return Exit::accepted;");
    line("}");
    line("template <class M>");
    line("[[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE Exit refuse", kParameters, " {");
    line("  return m.refused({tokenEnd, tokenHead});");
    line("}");
    line("template <class M, bool Flush, auto Step>");
    line("[[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE Exit room_then", kParameters, " {");
    line("  if constexpr (Flush) m.settle();");
    line("  m.make_room();");
    line("  CURSIV_MUSTTAIL return Step(m, p, open, tokenEnd, tokenHead);");
    line("}");
    line("");
    line("template <class M> using State = CURSIV_PRESERVE_NONE Exit(M&, const char*, NodeCount, ",
         "const char*, ::std::uint64_t);");
    for (const auto& label : _functions)
      line("template <class M> ", points.contains(label) ? "CURSIV_NOINLINE " :
           _uses[label] == 1 ? "CURSIV_ALWAYS_INLINE inline " : "inline ", "State<M> ", label, ";");
    line("");
    _out += shiftRows;
    _out += functions;
    line("template <class M>");
    line("CURSIV_ALWAYS_INLINE inline Exit run_states(M& m) {");
    line("  static constexpr decltype(&refuse<M>) entries[] = {");
    for (const auto& label : entries) line("    ", label, "<M>,");
    line("  };");
    line("  return entries[kResume[m.resume]](m, m.at, 0, m.tok.end, m.tok.head);");
    line("}");
    line("");
    line("}  // namespace");
    line("");
    line("}  // namespace ", ns);
    write("ParserStates.hpp", take());

    for (const auto& [type, file] :
         {std::pair{"Machine", "Parser.cpp"}, {"Explorer", "Explorer.cpp"}, {"Repairer", "Repairer.cpp"}}) {
      const bool machine = std::string_view(type) == "Machine";
      line("// GENERATED by the parser generator -- do not edit.");
      line("#include \"ParserStates.hpp\"");
      if (machine && predicts()) line("#include \"cursiv/engine/parser/Split.hpp\"");
      if (machine && !_p.matcher.entries.empty()) line("#include \"cursiv/engine/parser/Match.hpp\"");
      line("");
      const bool scans = machine && std::ranges::any_of(_p.matcher.exits, [](const MatchExit& exit) {
        return exit.action == MatchAction::scan;
      });
      if (scans) {
        line("#include <", names.header, ">");
        line("#include \"cursiv/engine/grammar/Facts.hpp\"");
      }
      line("namespace ", ns, " {");
      if (scans) line("using ScanDependencies = ::cursiv::engine::reached_all_t<::cursiv::engine::Facts<::",
                      names.grammar, ">::stage>;");
      line("::cursiv::engine::Exit drive(::cursiv::engine::", type, "& m) { return run_states(m); }");
      if (machine) {
        if (!_twinRank.empty()) array(twinType, "kTwinRank", _twinRank);
        array(resumeType, "kResume", resume);
        line("::cursiv::engine::Exit drive(::cursiv::engine::Explorer&);");
        line("::cursiv::engine::Exit drive(::cursiv::engine::Repairer&);");
        line("namespace {");
        matcher();
        prefixes();
        tables();
        line("}  // namespace");
        line("constinit const ::cursiv::engine::Tables kTables = make_tables();");
      }
      line("}  // namespace ", ns);
      write(file, take());
    }
  }

 private:
  static constexpr std::string_view kParameters =
      "(M& m, [[maybe_unused]] const char* p, [[maybe_unused]] NodeCount open, "
      "[[maybe_unused]] const char* tokenEnd, [[maybe_unused]] ::std::uint64_t tokenHead)";

  // A state's function begun, its body written next; whether it is inlined is
  // its declaration's.
  void open_function(const std::string& name, bool held = true) {
    _functions.push_back(name);
    _caller = name;
    line("template <class M>");
    line("CURSIV_PRESERVE_NONE Exit ", name, kParameters, " {");
    // A reduction made with no token holds none.
    line(held ? "  [[maybe_unused]] Token tok{tokenEnd, tokenHead};"
              : "  [[maybe_unused]] Token tok{nullptr, 0};");
  }
  // The run going on in `callee`, by a tail call.
  static std::string tail(const std::string& callee) {
    return "CURSIV_MUSTTAIL return " + callee + "(m, p, open, tok.end, tok.head);";
  }
  std::string call(const std::string& label) {
    // Inlining only forward edges leaves every cycle a callable label.
    _uses[label] += _caller < label ? 1 : 2;
    return label + "<M>";
  }
  std::string jump(const std::string& label) { return tail(call(label)); }
  // Room for a step's writes, the hidden trivia written with `flush`; made
  // out of line before `label` is taken again where they are missing.
  static std::string room(std::string_view test, bool flush, const std::string& label) {
    return "if (" + std::string(test) + (flush ? " || !m.flushed()" : "") + ") [[unlikely]] " +
           tail("room_then<M, " + std::string(flush ? "true" : "false") + ", " + label + "<M>>");
  }

  template <class... Parts>
  void line(const Parts&... parts) {
    (append(parts), ...);
    _out += '\n';
  }
  void append(std::string_view text) { _out += text; }
  template <class Number>
    requires std::is_arithmetic_v<Number>
  void append(Number value) { _out += std::to_string(value); }

  std::string take() { return std::exchange(_out, {}); }

  // `case` labels for `values`, a few a line.
  template <class Values>
  static std::string cases(const Values& values, std::string_view indent) {
    std::string out;
    std::size_t count = 0;
    for (const auto value : values) {
      if (count % 10 == 0) {
        if (count != 0) out += '\n';
        out += indent;
      } else {
        out += ' ';
      }
      out += "case " + std::to_string(value) + ":";
      ++count;
    }
    return out;
  }

  static std::string all_of(const std::vector<std::string>& tests) {
    std::string out;
    for (const std::string& test : tests) {
      if (!out.empty()) out += " && ";
      out += test;
    }
    return out;
  }

  // The label a transition to `state` jumps to: its reduction where it reads
  // no token, its dispatch where a token is in hand, its lexer otherwise.
  std::string enter(std::uint32_t state, bool held) {
    if (const std::uint32_t lone = _p.states[state].reduce; lone != kNone)
      return reduce_label(lone, held);
    (held ? _dispatched : _lexed)[state] = true;
    return (held ? "D" : "S") + std::to_string(held ? _dispatchAs[state] : _lexAs[state]);
  }

  // States with the same actions share their dispatch. A dispatch that reads
  // again or splits also depends on its lexical mode.
  void twins() {
    using Row = std::vector<std::pair<std::uint16_t, PackedAction::Key>>;
    using Key = std::tuple<std::uint16_t, std::vector<std::uint16_t>, Row>;
    std::map<Key, std::uint32_t> dispatches;
    std::map<std::pair<std::uint32_t, std::uint16_t>, std::uint32_t> lexers;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s) {
      _dispatchAs.push_back(s);
      _lexAs.push_back(s);
      const PackedState& state = _p.states[s];
      if (state.reduce != kNone) continue;
      const bool mode = _cfg.newline != 0 || !state.passes.empty() || state.again != kNoMode || has_arms(state);
      Key key{mode ? state.mode : kNoMode, state.passes, {}};
      for (PackedAction a : state.actions) {
        if (a.kind == PackedAction::Kind::reread) a.entry = 0;
        std::get<2>(key).emplace_back(a.terminal, a.key(_cfg));
      }
      _dispatchAs[s] = dispatches.try_emplace(std::move(key), s).first->second;
      _lexAs[s] = lexers.try_emplace({_dispatchAs[s], state.mode}, s).first->second;
    }
  }

  // Whether the state's dispatch splits a run: a cell with several actions,
  // a tie of its mode it chooses on, or terminals a token the matcher read
  // may stand for alike.
  bool has_arms(const PackedState& state) const {
    const auto same = [](const PackedAction& a, const PackedAction& b) {
      return a.terminal == b.terminal;
    };
    if (std::ranges::adjacent_find(state.actions, same) != state.actions.end()) return true;
    bool splits = false;
    state.eachMode([&](std::uint16_t mode) {
      if (!splits) {
        const auto [ties, matched] = ties_of(state, mode);
        splits = !ties.empty() || !matched.empty();
      }
    });
    return splits;
  }

  // Lexical ties this state distinguishes in the active mode.
  struct Ties {
    std::vector<std::uint32_t> dfa;
    std::map<std::uint8_t, std::vector<std::uint16_t>> matched;
    auto operator<=>(const Ties&) const = default;
  };
  Ties ties_of(const PackedState& state, std::uint16_t mode) const {
    Ties out;
    // Accept finishes the run without consuming its lookahead.
    const auto accepts = [&](std::uint16_t terminal) {
      const auto cell = on(state, terminal);
      return cell.size() == 1 && cell.front().kind == PackedAction::Kind::accept;
    };
    for (const auto id : _p.modeTies[mode]) {
      const auto& terminals = _p.ties[id].terminals;
      if (std::ranges::any_of(std::span(terminals).subspan(1),
                              [&](auto t) { return !on(state, t).empty(); }) &&
          !std::ranges::all_of(terminals, accepts))
        out.dfa.push_back(id);
    }
    if (_p.matcher.entries.empty()) return out;
    // The end of the text has none to read alike.
    for (const PackedAction& a : state.actions) {
      if (a.terminal == kEndOfInput || !_p.modeTerminals[mode].test(a.terminal)) continue;
      const auto rank = _p.terminalRank[a.terminal];
      for (unsigned r = rank - _p.mixed[a.terminal]; r <= rank; ++r) {
        auto& alike = out.matched[static_cast<std::uint8_t>(r)];
        if (alike.empty() || alike.back() != a.terminal) alike.push_back(a.terminal);
      }
    }
    std::erase_if(out.matched, [&](const auto& group) {
      const auto& alike = group.second;
      return alike.size() < 2 || std::ranges::all_of(alike, accepts) ||
             std::ranges::none_of(alike, [&](auto t) { return _p.matcher.matched[t]; });
    });
    return out;
  }

  // The actions a state takes on `terminal`, its actions being ascending; a
  // reread, which stands last, is none.
  static std::span<const PackedAction> on(const PackedState& state, std::uint16_t terminal) {
    auto [begin, end] =
        std::ranges::equal_range(state.actions, terminal, {}, &PackedAction::terminal);
    if (begin != end && end[-1].kind == PackedAction::Kind::reread) --end;
    return {begin, end};
  }

  std::string reads_as(std::uint16_t terminal) const {
    return "m.lexing().reads_as(tok, " + std::to_string(_p.solo[terminal]) + ", " +
           std::to_string(terminal) + ")";
  }

  std::string reduce_label(std::uint32_t production, bool held) {
    if (_reduced.emplace(production, held).second)
      _reduces.emplace_back(production, held);
    return "R" + std::to_string(production) + (held ? "h" : "n");
  }

  template <class Values>
  void array(std::string_view type, std::string_view name, const Values& values) {
    emit_array(_out, type, name, values, 24);
  }

  // The tables the runtime reads, and the language's code beside them.
  void tables() {
    // The scanners as the lexer's walk calls them: by their number.
    std::string scanners = "constexpr RunScanner kRunScanners[] = {";
    for (std::size_t r = 0; r < _p.runBytes.size(); ++r)
      scanners += "run" + std::to_string(r) + "<false>, ";
    line(scanners, "nullptr};");
    line("");
    std::string bindings;
    const auto field = [&](const std::string& name, const auto& values) {
      using T = std::ranges::range_value_t<decltype(values)>;
      static_assert(std::is_unsigned_v<T>);
      std::string label = "k" + name;
      label[1] = static_cast<char>(name.front() - 'a' + 'A');
      array("::std::uint" + std::to_string(sizeof(T) * 8) + "_t", label, values);
      bindings += "  t." + name + " = " + label + ";\n";
    };
    field("byteClass", _p.byteClass);
    array("::std::uint16_t", "kLexNext", _p.lexNext);
    array("::std::uint32_t", "kNextBlocks", _p.nextBlocks);
    line("constexpr LexState kLexStates[] = {");
    for (std::size_t s = 0; s < _p.nextOf.size(); ++s)
      line("  {", _p.nextOf[s], ", ", _p.lexAccept[s], ", ", _p.lexReachOf[s], "},");
    line("};");
    field("lexRun", _p.lexRun);
    field("runSets", _p.runSets);
    array("::std::uint16_t", "kLexWin", _p.lexWin);
    array("::std::uint32_t", "kWinBlocks", _p.winBlocks);
    array("::std::uint8_t", "kLexOnward", _p.lexOnward);
    field("lexLeadAt", _p.paths.offsets);
    field("lexLeads", _p.paths.targets);
    field("lexBodies", _p.paths.bodies);
    field("lexOpeners", _p.paths.openers);
    field("terminalSets", _p.terminalSets);
    if (!_p.lexTieEnds.empty()) field("lexTieEnds", _p.lexTieEnds);
    field("followOf", _p.followOf);
    field("closeOf", _p.closeOf);
    field("mayCloseOf", _p.mayCloseOf);
    field("wordBytes", _p.wordBytes);
    field("terminalRank", _p.terminalRank);
    field("terminalEntry", _p.terminalElement);
    {
      std::vector<std::uint16_t> order(_cfg.terminals.size(), 0);
      std::uint16_t next = 0;
      for (const bool named : {true, false})
        for (std::size_t t = 0; t < order.size(); ++t)
          if (_cfg.terminals[t].named == named) order[t] = next++;
      field("terminalOrder", order);
    }
    // The keyword a state acts on alone, one a word can stand in place of.
    std::vector<std::uint16_t> forced;
    for (const PackedState& state : _p.states) {
      std::uint16_t keyword = kNoToken;
      const auto acts = std::ranges::count_if(state.actions, [](const PackedAction& a) {
        return a.kind != PackedAction::Kind::reread;
      });
      if (acts == 1 && state.actions[0].terminal != kEndOfInput) {
        const Terminal& terminal = _cfg.terminals[state.actions[0].terminal];
        if (terminal.literal() && std::ranges::all_of(terminal.text, [&](char c) {
              return _p.wordBytes[static_cast<unsigned char>(c)] != 0;
            }))
          keyword = state.actions[0].terminal;
      }
      forced.push_back(keyword);
    }
    field("forced", forced);
    // A state selects a shared list by its one-based index, zero for an empty list.
    const auto lists = [&](std::string name, const auto& source, auto&& get) {
      using Row = std::remove_cvref_t<decltype(get(source.front()))>;
      std::map<Row, std::uint32_t> known;
      std::vector<std::uint32_t> of, offsets{0};
      Row words;
      for (const auto& item : source) {
        const auto& row = get(item);
        if (row.empty()) {
          of.push_back(0);
          continue;
        }
        const auto [it, fresh] = known.try_emplace(row, known.size() + 1);
        if (fresh) {
          words.insert(words.end(), row.begin(), row.end());
          offsets.push_back(static_cast<std::uint32_t>(words.size()));
        }
        of.push_back(it->second);
      }
      field(name + "Of", of);
      field(name + "At", offsets);
      field(name + "Word", words);
    };
    lists("insert", _p.states, [](const PackedState& state) -> const auto& { return state.inserts; });
    std::vector<std::uint8_t> retries;
    for (const auto& state : _p.states)
      retries.push_back(std::ranges::any_of(state.actions, [](const auto& a) {
        return a.kind == PackedAction::Kind::reread;
      }));
    field("retries", retries);
    static constexpr std::string_view kTests[] = {"none", "match", "word", "eof", "waits"};
    std::string text;
    for (const LexResolution& r : _p.resolutions)
      text += "\n    {" + std::to_string(r.token) + ", " + std::to_string(r.otherwise) +
              ", " + std::to_string(r.back) +
              ", LexTest::" + std::string(kTests[static_cast<unsigned>(r.test)]) +
              (r.keyword ? ", true, " : ", false, ") + (r.named ? "true, " : "false, ") +
              (r.literal ? "true, " : "false, ") + std::to_string(r.waits) + "},";
    line("constexpr LexResolution kResolutions[] = {", text.empty() ? "{}" : text, "};");
    text.clear();
    for (const unicode::Range r : _p.wordRanges)
      text += "\n    {" + std::to_string(static_cast<std::uint32_t>(r.lo)) + ", " +
              std::to_string(static_cast<std::uint32_t>(r.hi)) + "},";
    line("constexpr ::cursiv::unicode::Range kWordRanges[] = {",
         text.empty() ? "{}" : text, "};");
    static constexpr std::string_view kTrivia[] = {"none", "hidden", "ignored"};
    text.clear();
    for (const Trivia trivia : _p.terminalTrivia)
      text += " Trivia::" + std::string(kTrivia[static_cast<unsigned>(trivia)]) + ",";
    line("constexpr Trivia kTerminalTrivia[] = {", text, "};");
    line("");
    std::vector<std::uint16_t> stateMode;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s)
      stateMode.push_back(_p.states[s].reduce == kNone && _read[_p.states[s].mode]
                              ? _p.states[s].mode
                              : kNoToken);
    field("stateMode", stateMode);
    field("modeFault", _p.modeFault);
    if (closes()) field("modeClose", _p.modeClose);
    if (!_p.monitors.empty()) {
      std::string monitors = "constexpr TextMonitor kMonitors[] = {";
      for (std::size_t k = 0; k < _p.monitors.size(); ++k) {
        const LexMonitor& m = _p.monitors[k];
        const std::string name = "kMonitor" + std::to_string(k);
        array("::std::uint8_t", name + "Class", m.classOf);
        array("::std::uint16_t", name + "Next", m.next);
        array("::std::uint8_t", name + "Flags", m.flags);
        monitors += "\n    {" + name + "Class, " + name + "Next, " + name + "Flags, " +
                    std::to_string(m.nclasses) + "},";
      }
      line(monitors, "\n};");
      std::vector<std::uint32_t> waitAt{0}, waitWord;
      for (const auto& list : _p.waits) {
        for (const auto& wait : list) waitWord.insert(waitWord.end(), wait.begin(), wait.end());
        waitAt.push_back(static_cast<std::uint32_t>(waitWord.size()));
      }
      field("waitAt", waitAt);
      field("waitWord", waitWord);
    }
    if (!_p.tieSets.empty()) {
      std::vector<std::uint64_t> ties;
      for (const Bits& set : _p.tieSets)
        ties.insert(ties.end(), set.words().begin(), set.words().end());
      field("lexTies", ties);
    }
    // What an election reads, for a grammar some state of which has several
    // actions on a token.
    if (_splits) {
      std::vector<std::uint32_t> size;
      for (const Production& p : _cfg.productions)
        size.push_back(static_cast<std::uint32_t>(p.rhs.size()));
      field("productionSize", size);
      field("productionOrder", _p.order);
      if (_p.hops.size() > 1) {
        std::vector<std::uint32_t> number;
        field("unitWord", _p.unitWords(number));
        std::vector<std::vector<std::uint64_t>> rows(_p.states.size());
        for (std::uint32_t s = 0; s < rows.size(); ++s)
          for (const PackedAction& a : _p.states[s].actions)
            if (a.hops) rows[s].push_back(std::uint64_t{a.terminal} << 32 | number[a.hops]);
        std::vector<std::uint32_t> symbols;
        for (const Production& p : _cfg.productions)
          symbols.push_back(static_cast<std::uint32_t>(_cfg.terminals.size()) + p.lhs);
        for (std::size_t nt = 0; nt < _p.gotos.size(); ++nt)
          for (const PackedGoto& site : _p.gotos[nt])
            if (site.hops) rows[site.from].push_back((std::uint64_t{_cfg.terminals.size()} + nt) << 32 | number[site.hops]);
        field("productionSymbol", symbols);
        lists("derivation", rows, [](const auto& row) -> const auto& { return row; });
      }
      std::vector<std::uint32_t> turnOf, turnVia;
      for (const PackedState& state : _p.states) {
        turnOf.push_back(state.turn);
        turnVia.push_back(state.via);
      }
      field("turnOf", turnOf);
      field("turnVia", turnVia);
    }
    line("constexpr LexMode kModes[] = {");
    for (std::uint16_t mode = 0; mode < _p.modeCount; ++mode)
      line("  {", _read[mode] ? "full" + std::to_string(mode) + (_hides ? "<false>" : "<true>")
                              : "nullptr",
           ", {kLexWin, kWinBlocks + ", _p.winOf[mode], "}, kLexOnward + ", _p.onwardOf[mode], "},");
    line("};");
    line("");
    line("constexpr Tables make_tables() {");
    line("  Tables t;");
    _out += bindings;
    line("  t.drive = &drive;");
    if (predicts()) line("  t.predict = &predict;");
    line("  t.explore = &drive;");
    line("  t.repair = &drive;");
    line("  t.followWords = ", (_p.terminalElement.size() + 63) / 64, ";");
    line("  t.closeMost = ", _p.closeMost, "ull;");
    line("  t.modes = kModes;");
    line("  t.skipBytes = kSkip;");
    if (!_p.matcher.entries.empty()) line("  t.match = &match_text;");
    if (!_p.monitors.empty()) line("  t.monitors = kMonitors;");
    if (!_p.tieSets.empty()) line("  t.tieWords = ", _p.tieSets.front().words().size(), ";");
    line("  t.runScanners = kRunScanners;");
    line("  t.lexNext = {kLexNext, kNextBlocks};");
    line("  t.lexStates = kLexStates;");
    line("  t.resolutions = kResolutions;");
    line("  t.wordRanges = kWordRanges;");
    line("  t.wordRangeCount = ", _p.wordRanges.size(), ";");
    line("  t.wordInvalid = ", _p.wordInvalid ? "true" : "false", ";");
    line("  t.lexClassCount = ", _p.lexClassCount, ";");
    line("  t.terminalTrivia = kTerminalTrivia;");
    if (_cfg.indent != 0) line("  t.indentTerminal = ", _cfg.indent, ";");
    if (_cfg.dedent != 0) line("  t.dedentTerminal = ", _cfg.dedent, ";");
    line("  t.terminalCount = ", _p.terminalElement.size(), ";");
    line("  t.hides = ", _hides ? "true" : "false", ";");
    line("  return t;");
    line("}");
    line("");
  }

  // Scan an ASCII run one register at a time, then finish with its byte flags.
  void scanner(std::size_t r) {
    const ByteSet& set = _p.runBytes[r];
    // Use whichever of the set and its ASCII complement needs fewer ranges.
    std::vector<Lane> included, excluded;
    lanes_of(set, included);
    lanes_of(~set, excluded);
    const bool held = !included.empty() && included.size() < excluded.size();
    const auto& lanes = held ? included : excluded;
    const bool swar = lanes.size() <= 4;
    // Whether the lanes say all of the set: its flags then only end the text.
    bool whole = true;
    for (unsigned b = 0x80; b < 256; ++b) whole = whole && !set.has(b);
    // The lanes of a word that are out of the set, as an expression.
    std::string in;
    if (swar) {
      in = held ? "~(" : "(";
      for (std::size_t i = 0; i < lanes.size(); ++i)
        in += (i == 0 ? "lanes_in(w, " : " | lanes_in(w, ") + std::to_string(lanes[i].lo) +
              ", " + std::to_string(lanes[i].hi) +
              (lanes[i].mask != 0x7f ? ", " + std::to_string(lanes[i].mask) : "") + ")";
      in += lanes.empty() ? "LaneWord{})" : ")";
    }
    line("template <bool Watched>");
    line("CURSIV_ALWAYS_INLINE inline const char* run", r,
         "([[maybe_unused]] const ::std::uint8_t* in, const char* p, const char* end) {");
    if (swar) {
      line("  while (end - p >= static_cast<::std::ptrdiff_t>(sizeof(LaneWord))) {");
      line("    const LaneWord w = load_lanes(p);");
      line("    const unsigned n = first_lane(", in, " | w);");
      line("    p += n;");
      line("    if (n == sizeof(LaneWord)) continue;");
      if (whole) {
        line("    return p;");
      } else {
        line("    if (in[byte_at(p)] == 0) return p;");
        line("    ++p;");
      }
      line("  }");
      line("  if constexpr (Watched) return nullptr;");
    }
    line("  while (p < end && in[byte_at(p)] != 0) ++p;");
    line("  return Watched && p >= end ? nullptr : p;");
    line("}");
    line("");
  }

  // The matcher's automaton as code: a label per state, which takes the
  // exits of the state, then goes on by the byte at `p`.
  void matcher() {
    const LexMatcher& m = _p.matcher;
    if (m.entries.empty()) return;
    line("// The matcher's automaton: each thread of search `s` read from its entry at its");
    line("// place, its exits taken where its states stand, until no thread is left or a");
    line("// condition waits.");
    line("void match_text(Machine& m, Matching& s) {");
    line("  const char* const src = m.src;");
    line("  const char* const end = m.end;");
    line("  while (s.runs()) {");
    line("    const MatchThread th = s.take();");
    line("    const char* p = src + th.at;");
    line("    const char* limit = Matching::stride(m, p);");
    line("    switch (th.entry) {");
    for (std::size_t k = 0; k < m.entries.size(); ++k)
      if (m.entries[k] != 0) line("      case ", k, ": goto s", m.entries[k], ";");
    line("      default: continue;");
    line("    }");
    const std::size_t n = m.accepts.size();
    for (std::size_t state = 1; state < n; ++state) {
      line("  s", state, ":");
      for (const LexAccept& a : m.accepts[state]) {
        const std::string guard = a.guard == LexTest::eof     ? "m.observe_end(p == end)"
                                  : a.guard == LexTest::waits ? "m.settles(" + std::to_string(a.waits) + ", p)"
                                                              : "";
        line(guard.empty() ? "    {" : "    if (" + guard + ") {");
        line("      const auto at = static_cast<TextOffset>(p - src", a.back == 0 ? "" : " - " + std::to_string(a.back), ");");
        // A thread its next byte would end at once is not made.
        if (const std::string first = first_bytes(m.exits[a.term]); !first.empty()) {
          line("      const unsigned next = m.observe_end(src + at == end) ? 256u : byte_at(src + at);");
          line("      if (", first, ") {");
          exit_code(m.exits[a.term]);
          line("      }");
        } else {
          exit_code(m.exits[a.term]);
        }
        line("    }");
      }
      // By target, the bytes that lead there; the target most bytes lead to
      // is the default.
      std::map<std::uint16_t, std::vector<unsigned>> to;
      for (unsigned b = 0; b < 256; ++b)
        to[m.next[state * m.nclasses + m.classOf[b]]].push_back(b);
      if (to.size() == 1 && to.begin()->first == 0) {
        line("    continue;");
        continue;
      }
      auto most = to.begin();
      for (auto it = to.begin(); it != to.end(); ++it)
        if (it->second.size() > most->second.size()) most = it;
      // A run with no exit along it is scanned, as the lexer scans its own.
      if (const std::uint8_t run = _p.matchRun[state]; run != 0) {
        line("    if (const char* const r = run", run - 1u, "<true>(m.t.runSets + ", (run - 1u) * 256u,
             ", p, m.mark); r != nullptr)");
        line("      p = r;");
        line("    else");
        line("      p = m.run_on(", run - 1u, ", p);");
      }
      line("    if (p >= limit) [[unlikely]]");
      line("      if (m.observe_end(p == end) || !s.passes(m, th, ", state, ", p, limit)) continue;");
      line("    switch (byte_at(p++)) {");
      for (const auto& [target, bytes] : to) {
        if (target == most->first) continue;
        line(cases(bytes, "      "), target == 0 ? " continue;" : " goto s" + std::to_string(target) + ";");
      }
      line(most->first == 0 ? "      default: continue;"
                            : "      default: goto s" + std::to_string(most->first) + ";");
      line("    }");
    }
    line("  }");
    line("}");
    line("");
  }

  // The test that byte `next` can begin the thread exit `e` makes, where the
  // thread reads a byte first and a few ranges hold those bytes; none
  // otherwise.
  [[nodiscard]] std::string first_bytes(const MatchExit& e) const {
    const LexMatcher& m = _p.matcher;
    std::uint16_t from = 0;
    switch (e.action) {
      case MatchAction::open:
      case MatchAction::close:
      case MatchAction::ahead:
      case MatchAction::unless:
        from = m.entries[e.to];
        break;
      case MatchAction::jump:
        from = m.entries[e.arg];
        break;
      default:
        return {};
    }
    if (from == 0 || !m.accepts[from].empty()) return {};
    std::vector<std::pair<unsigned, unsigned>> ranges;
    for (unsigned b = 0; b < 256; ++b) {
      if (m.next[std::size_t{from} * m.nclasses + m.classOf[b]] == 0) continue;
      if (!ranges.empty() && ranges.back().second + 1 == b)
        ranges.back().second = b;
      else
        ranges.emplace_back(b, b);
    }
    if (ranges.empty() || ranges.size() > 3 || (ranges.size() == 1 && ranges[0] == std::pair(0u, 255u)))
      return {};
    std::string test;
    for (const auto& [lo, hi] : ranges) {
      if (!test.empty()) test += " || ";
      test += lo == hi   ? "next == " + std::to_string(lo)
              : lo == 0 ? "next <= " + std::to_string(hi)
                        : "(next >= " + std::to_string(lo) + " && next <= " + std::to_string(hi) + ")";
    }
    return test;
  }

  [[nodiscard]] bool returns(std::uint16_t state) const {
    const auto& m = _p.matcher;
    const auto& ends = m.accepts[state];
    return ends.size() == 1 && ends.front().back == 0 && ends.front().guard == LexTest::none &&
           m.exits[ends.front().term].action == MatchAction::end;
  }

  [[nodiscard]] auto transitions(std::uint16_t state) const {
    const auto& m = _p.matcher;
    return std::span(m.next).subspan(std::size_t{state} * m.nclasses, m.nclasses);
  }

  [[nodiscard]] bool just_returns(std::uint16_t state) const {
    return returns(state) && std::ranges::all_of(transitions(state), [](auto to) { return to == 0; });
  }

  // A continuation that immediately returns or consumes one byte and returns.
  std::string optional_return(std::uint16_t entry) const {
    const auto& m = _p.matcher;
    const auto state = m.entries[entry];
    if (!returns(state)) return {};
    const auto next = transitions(state);
    for (const auto to : next)
      if (to && !just_returns(to))
        return {};
    std::vector<unsigned> bytes;
    for (unsigned b = 0; b != 256; ++b)
      if (next[m.classOf[b]]) bytes.push_back(b);
    if (bytes.empty()) return {};
    if (bytes.size() == 1) return ", [](unsigned b) { return b == " + std::to_string(bytes[0]) + "; }";
    return ", [](unsigned b) { switch (b) { " + cases(bytes, "") + " return true; default: return false; } }";
  }

  // The exit `e` taken by thread `th` at `at`: the thread it goes on as.
  void exit_code(const MatchExit& e) {
    const std::string tag = std::to_string(e.arg);
    const std::string to = std::to_string(e.to);
    switch (e.action) {
      case MatchAction::end:
        line("      s.end(th.call, at);");
        return;
      case MatchAction::jump:
        line("      MatchThread n;");
        line("      n.entry = ", e.arg, ";");
        line("      n.at = at;");
        line("      n.call = th.call;");
        line("      s.go(n);");
        return;
      case MatchAction::open:
        line("      MatchThread n = th;");
        line("      n.entry = ", to, ";");
        line("      n.at = at;");
        line("      n.marks[", tag, "] = {at, kNoMark};");
        line("      s.go(n);");
        return;
      case MatchAction::close:
        line("      if (th.marks[", tag, "].end == kNoMark && th.marks[", tag, "].begin != kNoMark) {");
        line("        MatchThread n = th;");
        line("        n.entry = ", to, ";");
        line("        n.at = at;");
        line("        n.marks[", tag, "].end = at;");
        line("        s.go(n);");
        line("      }");
        return;
      case MatchAction::scan:
        line("      m.observe_end(true);");
        line("      const ::std::string_view input(src + at, end);");
        line("      const auto length = ::cursiv::detail::tl_at_t<", e.arg, ", ScanDependencies>::read(input);");
        line("      if (length && length <= input.size())");
        if (just_returns(_p.matcher.entries[e.to]))
          line("        s.end(th.call, at + static_cast<TextOffset>(length), true);");
        else
          line("        s.go({", to, ", at + static_cast<TextOffset>(length), th.call, th.marks});");
        return;
      case MatchAction::echo:
        line("      if (const MatchMark mark = th.marks[", tag, "]; Matching::echoes(m, mark, at))");
        line("        s.go({", to, ", at + (mark.end - mark.begin), th.call, th.marks});");
        return;
      case MatchAction::call:
      case MatchAction::ahead:
      case MatchAction::unless:
        line("      MatchThread n = th;");
        line("      n.entry = ", to, ";");
        line("      n.at = at;");
        if (e.action == MatchAction::call)
          line("      s.call(m, ", e.arg, ", n", optional_return(e.to), ");");
        else
          line("      s.ask(", e.arg, ", ", e.action == MatchAction::unless ? "true" : "false", ", n);");
        return;
    }
  }

  // What a lone child's entry becomes under each record closed around it.
  void fuses() {
    for (const std::uint32_t outer : _fuses) {
      const auto begin = _p.fused.lower_bound({outer, 0});
      const auto end = _p.fused.upper_bound({outer, kNone});
      std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
      for (auto it = begin; it != end; ++it)
        pairs.emplace_back(_p.element(it->first.second), _p.element(it->second));
      const auto name = "fuse" + std::to_string(_p.element(outer));
      const auto [lo, hi] = std::ranges::minmax_element(pairs, {}, &decltype(pairs)::value_type::first);
      const bool indexed = pairs.size() >= 4 && hi->first - lo->first < 8 * pairs.size();
      if (indexed) {
        std::vector<std::uint32_t> values(hi->first - lo->first + 1);
        for (auto [from, to] : pairs) values[from - lo->first] = to;
        array(std::ranges::max(values) <= 0xFFFF ? "::std::uint16_t" : "::std::uint32_t",
              name + "Map", values);
      }
      line("constexpr auto ", name, " = [](::std::uint32_t inner) -> ::std::uint32_t {");
      if (indexed) {
        line("  return ", name, "Map[inner - ", lo->first, "];");
      } else {
        line("  switch (inner) {");
        for (auto [from, to] : pairs) line("    case ", from, ": return ", to, ";");
        line("    default: __builtin_unreachable();");
        line("  }");
      }
      line("};");
    }
    line("");
  }

  // The record `production` closes with `entry` over what was written since
  // `open`, by what that can be.
  void close(std::uint32_t production, std::uint32_t entry, bool held, bool flushed,
             std::string_view indent) {
    if (entry == kNone) return;
    const unsigned shapes = _p.shapes[production];
    const std::uint32_t element = _p.element(entry);
    // The record of a production that reads nothing stands behind the trivia
    // kept before the token in hand: where that token begins.
    const bool empties = (shapes & kRecordEmpty) != 0;
    const std::string_view takes = empties ? "open = " : "";
    if (shapes == kRecordEmpty && !flushed) line(indent, "m.settle();\n", indent, "m.make_room();");
    const std::string_view at = _cfg.productions[production].rhs.empty() && held
                                    ? "m.lexing().src + tok.begin(m.lexing())"
                                    : "p";
    if ((shapes & kRecordLone) == 0) {
      line(indent, takes, "m.template close<", shapes, ">(open, ", element, ", ", at, ");");
      return;
    }
    _fuses.insert(entry);
    line(indent, takes, "m.template close<", shapes, ">(open, ", element, ", ", at, ", fuse",
         element, ");");
  }

  // Ignored tokens continue the reader. The full read records hidden trivia.
  void give(std::uint16_t terminal, std::string_view end, std::string_view indent) {
    const Trivia trivia = _p.terminalTrivia[terminal];
    if (_p.matcher.matched[terminal] || trivia == Trivia::hidden) line(indent, "return Token{p, kLexWalk};");
    else if (trivia == Trivia::ignored) line(indent, "p = ", end, ";\n", indent, "continue;");
    else line(indent, "return token(begin, ", end, ", ", terminal, ");");
  }

  // The tests that the text at `p` spells `text` past its first byte, which
  // the case read.
  static void spells(std::string_view text, bool folded, std::vector<std::string>& tests) {
    if (!folded && text.size() > 2) {
      tests.push_back("::std::memcmp(p + 1, " + cpp_literal(text.substr(1)) + ", " +
                      std::to_string(text.size() - 1) + ") == 0");
      return;
    }
    for (std::size_t i = 1; i < text.size(); ++i) {
      const unsigned c = static_cast<unsigned char>(text[i]);
      const bool letter = (c | 0x20u) >= 'a' && (c | 0x20u) <= 'z';
      const std::string at = "byte_at(p + " + std::to_string(i) + ")";
      tests.push_back(folded && letter
                          ? "(" + at + " | 0x20u) == " + std::to_string(c | 0x20u)
                          : at + " == " + std::to_string(c));
    }
  }

  // The test that no word goes on at `where`: by the byte there, or by its
  // scalar where the word class reaches past ASCII.
  std::string no_word_at(const std::string& where) const {
    if (!_p.wordRanges.empty() || _p.wordInvalid) return "!m.word_at(" + where + ")";
    return "(" + where + " == end || t.wordBytes[byte_at(" + where + ")] == 0)";
  }

  // A first byte then a run: what a text of `spelled` reads where the run
  // spells it, the pattern otherwise.
  void word(const WordPath& path) {
    // A run that holds every byte of a word it can stop before ends where no
    // word goes on; a wide one stops before no byte past ASCII.
    bool guarded = false;
    for (unsigned b = 0; b < (path.wide ? 0x80u : 256u); ++b)
      guarded = guarded || (_p.wordBytes[b] != 0 && !_p.runBytes[path.run].has(b));
    line("        const char* const e = run", path.run, "<true>(t.runSets + ",
         std::size_t{path.run} * 256, ", p + 1, stop);");
    line("        if (e == nullptr) return Token{p, kLexWalk};");
    if (path.wide) line("        if (e != end && byte_at(e) >= 0x80) return Token{p, kLexWalk};");
    for (const Spelling& spelling : path.spelled) {
      std::vector<std::string> tests{"e - p == " + std::to_string(spelling.text.size())};
      spells(spelling.text, spelling.folded, tests);
      if (spelling.terminal == kNoToken) {
        line("        if (", all_of(tests), ") return Token{p, kLexWalk};");
        continue;
      }
      if (guarded) tests.push_back(no_word_at("e"));
      line("        if (", all_of(tests), ") {");
      give(spelling.terminal, "e", "          ");
      line("        }");
    }
    give(path.terminal, "e", "        ");
  }

  // The longest of `list` the text spells; the lexer's walk reads what
  // spells none.
  void literals(const std::vector<std::uint16_t>& list) {
    for (const std::uint16_t literal : list) {
      const Terminal& term = _cfg.terminals[literal];
      const std::string length = std::to_string(term.text.size());
      std::vector<std::string> tests;
      if (term.text.size() > 1) tests.push_back("end - p >= " + length);
      spells(term.text, term.folded, tests);
      if (term.word) tests.push_back(no_word_at("p + " + length));
      if (tests.empty()) {
        give(literal, "p + " + length, "        ");
        return;
      }
      line("        if (", all_of(tests), ") {");
      give(literal, "p + " + length, "          ");
      line("        }");
    }
    line("        break;");
  }

  // How `state` reads in `mode`: what ends there, and whether a terminal of
  // the mode can end in it or past it.
  [[nodiscard]] std::uint16_t win(std::uint16_t mode, std::uint32_t state) const {
    return LexBlocks{_p.lexWin.data(), _p.winBlocks.data() + _p.winOf[mode]}.at(0, _p.lexAccept[state]);
  }
  [[nodiscard]] bool onward(std::uint16_t mode, std::uint32_t state) const {
    return ((_p.lexOnward[_p.onwardOf[mode] + (state >> 3)] >>
             (state & 7)) & 1) != 0;
  }
  // Whether the byte that leads to `state` is a token alone.
  [[nodiscard]] bool alone(std::uint16_t mode, std::uint32_t state) const {
    return win(mode, state) < kWinNone && !onward(mode, state);
  }
  // A mode's first bytes, as its lexer reads them: by the code of a word or
  // literal, by the state the byte leads to, and again from where the trivia
  // before them began.
  struct Firsts {
    std::map<std::uint32_t, std::vector<unsigned>> byCode;
    std::map<std::uint32_t, std::vector<unsigned>> byState;
    std::vector<unsigned> resumed;
  };
  [[nodiscard]] Firsts firsts(std::uint16_t mode) const {
    const std::uint32_t* const first = _p.lexFirst.data() + std::size_t{mode} * 256;
    // A mode that does not pass all the grammar drops reads them as tokens.
    const bool skips = _p.modeSkips[mode];
    Firsts out;
    for (unsigned b = 0; b < 256; ++b) {
      if (skips && _p.resumed.has(b)) {
        out.resumed.push_back(b);
      } else if (skips && _p.skipped.has(b)) {
        continue;
      } else if (first[b] != 0) {
        out.byCode[first[b]].push_back(b);
      } else if (const std::uint32_t to = _p.transition(1, b);
                 b < 0x80 && lives(mode, to)) {
        out.byState[to].push_back(b);
      }
    }
    return out;
  }
  [[nodiscard]] bool lives(std::uint16_t mode, std::uint32_t state) const {
    return state != 0 && (win(mode, state) != kWinNone || onward(mode, state));
  }

  // The terminal the matcher reads from `state` in `mode` where no other one
  // can end there or past it; none otherwise.
  [[nodiscard]] std::uint16_t matched_alone(std::uint16_t mode, std::uint32_t state) const {
    const std::uint16_t ends = win(mode, state);
    if (ends <= kWinNone || _p.resolutions[ends & ~kWinResolved].test != LexTest::match)
      return kNoToken;
    const std::uint16_t terminal = _p.resolutions[ends & ~kWinResolved].token;
    if (_p.mixed[terminal]) return kNoToken;
    const std::size_t words = (_p.terminalElement.size() + 63) / 64;
    const std::uint64_t* const reach = _p.terminalSets.data() + std::size_t{_p.lexReachOf[state]} * words;
    const auto& allowed = _p.modeTerminals[mode].words();
    for (std::size_t w = 0; w < words; ++w)
      if ((reach[w] & allowed[w]) != (w == terminal / 64u ? std::uint64_t{1} << (terminal % 64u) : 0))
        return kNoToken;
    return terminal;
  }

  // Scan the opening run and recognize a final successor before entering the DFA walk.
  void step(std::uint16_t mode, std::uint32_t state) {
    const auto ends = win(mode, state);
    const auto run = onward(mode, state) ? _p.lexRun[state] : 0;
    if (run) {
      line("  if (const char* r = run", run - 1, "<true>(m.t.runSets + ", (run - 1) * 256,
           ", e, m.mark)) e = r;");
      line("  else e = m.run_on(", run - 1, ", e);");
    }
    std::map<std::uint16_t, std::vector<unsigned>> next;
    if (ends <= kWinNone && onward(mode, state))
      for (unsigned b = 0; b < 256; ++b) {
        const auto to = _p.transition(state, b);
        if (run && to == state) continue;
        if (alone(mode, to)) next[win(mode, to)].push_back(b);
        else if (ends < kWinNone && lives(mode, to)) next[kNoToken].push_back(b);
      }
    if (!next.empty()) {
      line("  if (e != m.end) switch (byte_at(e)) {");
      for (const auto& [term, bytes] : next)
        if (term == kNoToken) line(cases(bytes, "    "), " return m.walk(p, mode, ", state, ", e);");
        else line(cases(bytes, "    "), " return token(begin, e + 1, ", term, ");");
      line("    default: break;");
      line("  }");
    }
    if (ends < kWinNone) line("  return token(begin, e, ", ends, ");");
    else {
      const bool match = ends != kWinNone && _p.resolutions[ends & ~kWinResolved].test == LexTest::match;
      if (const auto terminal = matched_alone(mode, state); terminal != kNoToken) {
        line("  if (const char* r = m.match(", _p.resolutions[ends & ~kWinResolved].waits,
             ", p); r && r != p) return token(begin, r, ", terminal, ");");
      }
      line("  return m.walk", match ? "<true>" : "", "(p, mode, ", state, ", e);");
    }
  }

  // The function that reads a token from its first byte by the automaton's
  // first steps, `state` being where that byte leads in `mode`. Modes that
  // read alike from there share it.
  std::size_t opening(std::uint16_t mode, std::uint32_t state) {
    std::string lexer = std::exchange(_out, {});
    step(mode, state);
    std::string body = std::exchange(_out, std::move(lexer));
    const auto [it, fresh] = _openings.try_emplace(std::move(body), _openings.size());
    if (fresh) _openingBodies.push_back(&it->first);
    return it->second;
  }

  void openings() {
    for (std::size_t id = 0; id < _openingBodies.size(); ++id) {
      // Out of the lexers, which stay small enough to stand in the states.
      line("CURSIV_NOINLINE inline Token open", id,
           "(Machine& m, const char* p, [[maybe_unused]] ::std::uint32_t mode) {");
      line("  [[maybe_unused]] const auto begin = static_cast<TextOffset>(p - m.src);");
      line("  const char* e = p + 1;");
      _out += *_openingBodies[id];
      line("}");
      line("");
    }
  }

  // Read visible tokens directly and leave hidden trivia and ambiguous openings to the walk.
  void lexer(std::uint16_t mode, unsigned readers) {
    const bool skips = _p.modeSkips[mode];
    const auto& [byCode, byState, resumed] = _firsts[mode];
    // The inline definition lets GCC omit unused readers at -O0. At call sites,
    // inline a shared reader while its extra copies add at most twelve dispatch arms.
    const bool inlineReader = readers <= 1 || byCode.size() + byState.size() <= 12 / (readers - 1);
    line(inlineReader ? "CURSIV_ALWAYS_INLINE" : "CURSIV_NOINLINE", " inline Token lex", mode,
         "(Machine& m, const char* p) {");
    line("  [[maybe_unused]] const Tables& t = m.t;");
    line("  [[maybe_unused]] const char* const end = m.end;");
    line("  for (;;) {");
    if (skips && _p.resumed.any()) line("    const char* const first = p;");
    line("    const char* const stop = m.mark;");
    if (skips && _p.skipped.any()) line("    while (p < stop && kSkip[byte_at(p)] != 0) ++p;");
    line("    if (p >= stop) return Token{p, kLexStop};");
    line("    [[maybe_unused]] const auto begin = static_cast<TextOffset>(p - m.src);");
    line("    switch (byte_at(p)) {");
    for (const auto& [code, bytes] : byCode) {
      line(cases(bytes, "      "), " {");
      const auto at = code & kFirstIndex;
      if (code < kFirstWord) literals(_p.literalPaths[at]);
      else if (_sharedWords[at]) line("        return word", at, "(m, p, begin, stop);");
      else word(_p.wordPaths[at]);
      line("      }");
    }
    // A byte that is a token alone; the others are read by `full`.
    for (const auto& [to, bytes] : byState) {
      line(cases(bytes, "      "), " {");
      if (alone(mode, to)) give(win(mode, to), "p + 1", "        ");
      else if (_p.lexTrivia[to]) line("        return Token{p, kLexWalk};");
      else line("        return open", opening(mode, to), "(m, p, ", mode, ");");
      line("      }");
    }
    if (!resumed.empty()) {
      line(cases(resumed, "      "));
      line("        p = first;");
      line("        break;");
    }
    line("      default: break;");
    line("    }");
    line("    return Token{p, kLexWalk};");
    line("  }");
    line("}");
    line("");
    line("template <bool Keep>");
    line("CURSIV_ALWAYS_INLINE inline Token full", mode, "(Machine& m, const char* p) {");
    line("  const Token x = lex", mode, "(m, p);");
    line("  if (x.terminal() >= kLexStop) [[unlikely]] return m.read_slow(x, ", mode, ", Keep, lex", mode, ");");
    line("  return x;");
    line("}");
    line("");
  }

  // What state `s` does with the token in hand.
  void dispatch(std::uint32_t s, std::span<const std::uint32_t> twins) {
    const PackedState& state = _p.states[s];
    const std::vector<PackedAction>& actions = state.actions;
    // A shared dispatch keeps each state's split points and stack-dependent
    // predictions. Its rank selects that state's contiguous arms in the cell.
    const auto split_at = [&](std::size_t begin, bool tied,
                              std::span<const PackedAction> cell = {}, bool consecutive = false, std::size_t rivals = 0, bool retryTie = false) {
      const std::size_t count = _arms.size() - begin;
      const std::size_t first = 2 * _p.states.size() + begin;
      _armTokens.resize(_arms.size(), tied ? ArmToken::tied : ArmToken::held);
      if (retryTie) _armTokens.back() = ArmToken::held;
      if (rivals) std::fill(_armTokens.end() - rivals - 1, _armTokens.end() - 1, ArmToken::rival);
      for (std::size_t rank = 0; rank < twins.size(); ++rank) {
        if (rank != 0)
          for (std::size_t i = 0; i < count; ++i) {
            _arms.push_back(_arms[begin + i]);
            _armTokens.push_back(_armTokens[begin + i]);
          }
        if (!cell.empty() && !rivals) {
          const auto point = static_cast<std::uint32_t>(first + rank * count);
          prefix_predictions(_prefixes, _cfg, _p, twins[rank], point, cell);
          guard(point, cell);
        }
      }
      std::string point = std::to_string(first);
      if (twins.size() > 1) point += " + kTwinRank[m.under()] * " + std::to_string(count) + "u";
      if (consecutive)
        point += " + (tok.terminal() - " + std::to_string(cell.front().terminal) + "u) * " +
                 std::to_string(twins.size() * count) + "u";
      const bool reread = retryTie || (!cell.empty() && cell.back().kind == PackedAction::Kind::reread);
      return "return m.split(tok, " + point + ", " + std::to_string(count) +
             (reread ? ", " + std::to_string(rivals + 1) : "") + ");";
    };
    // A retry uses only the ties of the mode that read its token. The action
    // switch is shared by all modes that reach this dispatch.
    bool retriesTie = false;
    std::map<std::vector<std::uint16_t>, std::string> branches;
    const auto branch = [&](const std::vector<std::uint16_t>& terminals) -> const std::string& {
      const auto [it, fresh] = branches.try_emplace(terminals);
      if (fresh) {
        const std::size_t first = _arms.size();
        for (const auto t : terminals) std::ranges::copy(on(state, t), std::back_inserter(_arms));
        const bool retry = std::ranges::any_of(terminals, [&](auto term) {
          const auto cell = std::ranges::equal_range(actions, term, {}, &PackedAction::terminal);
          return !cell.empty() && cell.back().kind == PackedAction::Kind::reread;
        });
        if (retry) {
          _arms.push_back({0, PackedAction::Kind::retryTie, s});
          retriesTie = true;
        }
        it->second = split_at(first, true, {}, false, 0, retry);
      }
      return it->second;
    };
    std::map<Ties, std::string> labels;
    const std::string entry = "D" + std::to_string(s);
    const std::string body = "J" + std::to_string(s);
    state.eachMode([&](std::uint16_t mode) {
      if (_retry.contains({s, mode})) return;
      const auto ties = ties_of(state, mode);
      const auto [it, fresh] = labels.try_emplace(
          ties, labels.empty() ? entry : entry + "m" + std::to_string(mode));
      _retry.emplace(std::pair{s, mode}, it->second);
      if (!fresh) return;
      if (labels.size() > 1) {
        line("  ", jump(body));
        line("}");
        open_function(it->second);
      }
      // Between brackets, the end of a line written in the text is a blank.
      if (_cfg.newline != 0)
        line("  if (tok.terminal() == ", _cfg.newline,
             " && m.indented.depth != 0 && tok.end != m.lexing().src + tok.begin(m.lexing())) [[unlikely]] "
             "CURSIV_MUSTTAIL return ", call("S" + std::to_string(_lexAs[s])),
             "(m, tok.end, open, tok.end, tok.head);");
      // The end of the text closes the levels still open.
      if (_cfg.dedent != 0 && std::ranges::any_of(actions, [&](const PackedAction& a) {
            return a.terminal == _cfg.dedent;
          }))
        line("  if (tok.terminal() == ", kEndOfInput, " && m.indented.level != 0) tok = token(tok.begin(m.lexing()), tok.end, ",
             _cfg.dedent, ");");
      // A token several terminals of the state read: the run stops, and goes
      // on from an action of any.
      for (const std::uint32_t tie : ties.dfa) {
        const Packed::Tie& tied = _p.ties[tie];
        std::string test;
        if (tied.row == kNone) {
          for (const auto t : std::span(tied.terminals).subspan(1))
            if (!on(state, t).empty()) test += (test.empty() ? "" : " || ") + reads_as(t);
          test = "(tok.head & kTold) == 0 && (" + test + ")";
        } else {
          test = "m.lexing().tied(tok, " + std::to_string(tied.row) + ")";
        }
        line("  if (tok.terminal() == ", tied.terminals.front(), " && ", test, ") ",
             branch(tied.terminals));
      }
      // Where the matcher read the token, the terminals the state acts on that
      // rank alike may read its text too: the run stops, and goes on from an
      // action of each that does.
      for (const auto& [rank, alike] : ties.matched) {
        std::string in;
        for (const std::uint16_t terminal : alike) {
          _matchTied.insert(terminal);
          in += (in.empty() ? "(tok.terminal() == " : " || tok.terminal() == ") + std::to_string(terminal);
        }
        const bool mixed = std::ranges::any_of(alike, [&](auto t) { return _p.mixed[t]; });
        line("  if (", in, ") && tok.hasGroup()",
             mixed ? " && m.lexing().rank(tok) == " + std::to_string(rank) : "", ") ", branch(alike));
      }
    });
    if (labels.size() > 1 || retriesTie) {
      line("  ", jump(body));
      line("}");
      open_function(body);
    }
    line("  switch (tok.terminal()) {");
    // The terminals that reduce one production share its jump.
    std::map<std::uint32_t, std::vector<std::uint16_t>> reduces;
    for (std::size_t i = 0; i < actions.size();) {
      std::size_t end = i + 1;
      while (end != actions.size() && actions[end].terminal == actions[i].terminal) ++end;
      if (end - i > 1) {
        // Several actions: the run stops, and goes on from the one chosen.
        const std::size_t width = end - i;
        while (!actions[end - 1].rivals && end + width <= actions.size() &&
               !actions[end + width - 1].rivals &&
               actions[end].terminal == actions[end - 1].terminal + 1 &&
               actions[end + width - 1].terminal == actions[end].terminal &&
               (end + width == actions.size() || actions[end + width].terminal != actions[end].terminal) &&
               (actions[end + width - 1].kind == PackedAction::Kind::reread) ==
                   (actions[i + width - 1].kind == PackedAction::Kind::reread))
          end += width;
        std::string call;
        for (std::size_t j = i; j < end; j += width) {
          const auto cell = std::span(actions).subspan(j, width);
          const std::size_t first = _arms.size();
          _arms.insert(_arms.end(), cell.begin(), cell.end() - 1);
          const auto before = _arms.size();
          for (const auto rival : _p.retryTerminals[cell.back().rivals])
            std::ranges::copy(on(state, rival), std::back_inserter(_arms));
          const auto rivals = _arms.size() - before;
          _arms.push_back(cell.back());
          const auto branch = split_at(first, false, cell, end - i > width, rivals);
          if (j == i) call = branch;
          line("    case ", cell.front().terminal, ":");
        }
        line("      ", call);
      } else if (actions[i].kind == PackedAction::Kind::reduce) {
        reduces[actions[i].value].push_back(actions[i].terminal);
      } else if (!_shiftRow.contains(s) || actions[i].kind != PackedAction::Kind::shift ||
                 !_shiftOf.contains(actions[i].key(_cfg))) {
        line("    case ", actions[i].terminal, ":");
        act(actions[i], "      ", "D" + std::to_string(s));
      }
      i = end;
    }
    auto common = reduces.end();
    for (auto it = reduces.begin(); it != reduces.end(); ++it)
      if (!_shiftRow.contains(s) && it->second.size() >= 8 && (common == reduces.end() || it->second.size() > common->second.size()))
        common = it;
    for (auto it = reduces.begin(); it != reduces.end(); ++it) {
      if (it != common) line(cases(it->second, "    "), " ", jump(reduce_label(it->first, true)));
      else if (_cfg.unread && std::ranges::binary_search(it->second, _cfg.unread))
        line("    case ", _cfg.unread, ": ", jump(reduce_label(it->first, true)));
    }
    // A token another lexer read where this one reads a trivia's text: the
    // text is read again here. A token with no width is never read again.
    if (!state.passes.empty()) {
      line(cases(state.passes, "    "));
      line("      if (tok.end != m.lexing().src + tok.begin(m.lexing())) {");
      line("        m.forget(p);");
      line("        CURSIV_MUSTTAIL return ", call("S" + std::to_string(_lexAs[s])),
           "(m, p, open, tok.end, tok.head);");
      line("      }");
      line("      [[fallthrough]];");
    }
    line("    default: break;");
    line("  }");
    if (common != reduces.end())
      line("  if (tok.terminal() < ", _cfg.terminals.size(), " && kTables.inSet(", _p.followOf[s],
           ", tok.terminal())) ", jump(reduce_label(common->first, true)));
    const auto row = _shiftRow.find(s);
    line("  ", row == _shiftRow.end() ? tail("refuse<M>") : jump(row->second));
  }

  // Where the tests of a reduction refuse the token in hand, the state on
  // top reads its text again without the terminals it reads only for such
  // reductions; elsewhere, or for a token with no width, it is refused.
  void again() {
    std::map<std::tuple<std::vector<std::uint16_t>, std::uint16_t, std::uint32_t>,
             std::vector<std::uint32_t>>
        groups;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s)
      if (const PackedState& state = _p.states[s];
          state.again != kNoMode && (_lexed[s] || _dispatched[s]))
        groups[{state.tested, state.again, _dispatchAs[s]}].push_back(s);
    open_function("again");
    line("  if (tok.end != m.lexing().src + tok.begin(m.lexing())) switch (m.under()) {");
    for (const auto& [key, states] : groups) {
      const auto& [terminals, mode, dispatch] = key;
      std::string in;
      for (const std::uint16_t t : terminals)
        in += (in.empty() ? "tok.terminal() == " : " || tok.terminal() == ") + std::to_string(t);
      line(cases(states, "    "));
      line("      if (", in, ") {");
      reread("        ", mode, dispatch);
      line("      }");
      line("      break;");
    }
    line("  }");
    line("  ", tail("refuse<M>"));
    line("}");
  }

  // The text of the token in hand read again from `p` in `mode`, and the token
  // read dispatched by `dispatch`.
  void reread(std::string_view indent, std::uint32_t mode, std::uint32_t dispatch) {
    line(indent, "m.forget(p);");
    line(indent, "tok = full", mode, "<!M::kSteps>(m.lexing(), p);");
    line(indent, jump(_retry.at({dispatch, mode})));
  }

  // Whether `p` holds before tokens it names: the token read among those
  // names is let go once it holds, and the state reached reads it again.
  static bool names(const Production& p) {
    return std::ranges::any_of(p.tests, [](const Test& test) {
      return !test.unless &&
             std::ranges::any_of(test.ahead, [](std::uint16_t t) { return t != kEndOfInput; });
    });
  }

  // An action whose whole body is a jump can also resume there directly.
  std::string destination(const PackedAction& action) {
    if (action.kind == PackedAction::Kind::reduce) return reduce_label(action.value, true);
    if (action.kind == PackedAction::Kind::accept) return "accepted";
    if (action.kind == PackedAction::Kind::shift)
      if (const auto at = _shiftOf.find(action.key(_cfg)); at != _shiftOf.end())
        return "T" + std::to_string(at->second);
    return {};
  }

  void act(const PackedAction& action, std::string_view indent, const std::string& label) {
    if (const auto to = destination(action); !to.empty()) {
      line(indent, jump(to));
    } else if (action.kind == PackedAction::Kind::retryTie) {
      line(indent, "tok = m.lexing().as_tied(tok, tok.terminal(), (tok.head & kLiteralBranch) != 0);");
      line(indent, jump("J" + std::to_string(action.value)));
    } else if (action.kind == PackedAction::Kind::reread) {
      line(indent, "if (tok.end == m.lexing().src + tok.begin(m.lexing())) ", tail("refuse<M>"));
      reread(indent, action.value, _dispatchAs[action.entry]);
    } else {
      shift(action, indent, label);
    }
  }

  // Share the shifts and the terminal-to-shift rows that recur across states.
  void shared_shifts() {
    std::map<PackedAction::Key, std::pair<std::size_t, PackedAction>> uses;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s) {
      if (_p.states[s].reduce != kNone || _dispatchAs[s] != s) continue;
      for (const PackedAction& action : _p.states[s].actions)
        if (action.kind == PackedAction::Kind::shift)
          ++uses.try_emplace(action.key(_cfg), 0, action).first->second.first;
    }
    for (const auto& [key, use] : uses)
      if (use.first > 1) {
        _shiftOf.emplace(key, _shifts.size());
        _shifts.push_back(use.second);
      }

    using Row = std::vector<std::pair<std::uint16_t, std::size_t>>;
    std::map<Row, std::vector<std::uint32_t>> rows;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s) {
      if (_dispatchAs[s] != s || _p.states[s].reduce != kNone) continue;
      Row row;
      const auto& actions = _p.states[s].actions;
      for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& a = actions[i];
        if (a.kind != PackedAction::Kind::shift || (i && actions[i - 1].terminal == a.terminal) ||
            (i + 1 < actions.size() && actions[i + 1].terminal == a.terminal)) continue;
        if (const auto it = _shiftOf.find(a.key(_cfg)); it != _shiftOf.end())
          row.emplace_back(a.terminal, it->second);
      }
      if (row.size() >= 6) rows[std::move(row)].push_back(s);
    }
    for (const auto& [row, states] : rows) {
      if (states.size() < 2) continue;
      const std::string label = "H" + std::to_string(states.front());
      for (const auto s : states) _shiftRow[s] = label;
      open_function(label);
      line("  switch (tok.terminal()) {");
      for (const auto& [term, shift] : row)
        line("    case ", term, ": ", jump("T" + std::to_string(shift)));
      line("    default: ", tail("refuse<M>"));
      line("  }");
      line("}");
    }
  }

  // The token in hand shifted, and its target entered; room is made for it
  // before `label` is taken again.
  void shift(const PackedAction& action, std::string_view indent, const std::string& label) {
    if (action.gap == kNoGap)
      line(indent, "if (p != m.lexing().src + tok.begin(m.lexing())) ", tail("refuse<M>"));
    else if (action.gap != kAnyGap)
      line(indent, "if (p != m.lexing().src + tok.begin(m.lexing()) && full", action.gap,
           _hides ? "<false>" : "<true>", "(m.lexing(), p).begin(m.lexing()) != tok.begin(m.lexing())) ",
           tail("refuse<M>"));
    const auto reduction = _p.states[action.value].reduce;
    const auto popped = reduction == kNone || _p.shapes[reduction] == kRecordEmpty
                            ? 0 : _cfg.productions[reduction].rhs.size();
    line(indent, room(popped > 1 ? "!m.roomy(M::kSteps, true)" : "!m.roomy(true, true)", _hides, label));
    const std::string args = "(" + std::to_string(action.value) + ", " +
                             std::to_string(_p.element(action.entry)) + ", tok";
    if (popped != 0) _afterShift.insert(reduction);
    line(indent, "if constexpr (M::kSteps) m.shift", args, ");");
    line(indent, "else ", popped != 0 ? "open = " : "", "m.template shift<", popped,
         _p.matcher.entries.empty() ? ", false>" : ">", args, ");");
    line(indent, "p = tok.end;");
    switch (_cfg.nesting(action.terminal)) {
      case Nesting::indent: line(indent, "m.lexing().indent(m.indented, tok);"); break;
      case Nesting::dedent: line(indent, "m.lexing().dedent(m.indented);"); break;
      case Nesting::open: line(indent, "++m.indented.depth;"); break;
      case Nesting::close: line(indent, "if (m.indented.depth != 0) --m.indented.depth;"); break;
      case Nesting::none: break;
    }
    line(indent, "if constexpr (M::kSteps) return m.shifted();");
    const auto next = enter(action.value, false);
    line(indent, jump(popped != 0 ? "B" + std::to_string(reduction) : next));
  }

  // Whether one of the tests of `prod` holds for the token `tok` in hand, the
  // text read up to `p`; empty where they ask nothing the state's actions do
  // not. A test that only names tokens, alone, is in the actions already.
  std::string holds_of(const Production& prod) const {
    std::string holds;
    const bool ahead = std::ranges::any_of(prod.tests, [](const Test& t) {
      return t.condition != 0 || !t.shadows.empty();
    });
    const std::string m = ahead ? "m" : "m.lexing()";
    for (const Test& test : prod.tests) {
      std::vector<std::string> parts;
      const std::string from = m + ".src + tok.begin(m.lexing())";
      if (prod.readsText(test)) {
        // The token is one the test names, or reads as one over its whole
        // text: where a token that may comes, the text is read again with
        // the test's alphabet, which skips the test's trivia.
        std::string in, shadow, read;
        for (const std::uint16_t t : test.ahead)
          in += (in.empty() ? "tok.terminal() == " : " || tok.terminal() == ") + std::to_string(t);
        if (!test.shadows.empty()) {
          for (const std::uint16_t t : test.shadows)
            shadow += (shadow.empty() ? "tok.terminal() == " : " || tok.terminal() == ") +
                      std::to_string(t);
          for (const std::uint16_t t : test.ahead)
            read += (read.empty() ? "a.terminal() == " : " || a.terminal() == ") + std::to_string(t);
          in += " || ((" + shadow + ") && [&] { const Token a = m.observed(full" +
                std::to_string(test.alphabet) + (_hides ? "<false>" : "<true>") +
                "(" + m + ", p)); return (" + read +
                ") && (a.begin(m.lexing()) != tok.begin(m.lexing()) || a.end == tok.end); }())";
        }
        parts.push_back(std::string(test.unless ? "!(" : "(") + in + ")");
      }
      if (test.condition != 0)
        parts.push_back(std::string(test.unless ? "!" : "") + m + ".reads(" +
                        std::to_string(_p.conditions[test.condition - 1]) + ", p, tok, indent)");
      if (test.gap != Test::Gap::any) {
        const std::string gap =
            test.gap == Test::Gap::empty  ? "p == " + from
            : test.gap == Test::Gap::line ? m + ".line_between(p, " + from + ")"
                                          : m + ".gap_reads(" +
                                                std::to_string(test.gapMonitor) + ", p, " + from +
                                                ", " + (test.gap == Test::Gap::is ? "true" : "false") +
                                                ")";
        parts.push_back(std::string(test.gapUnless ? "!(" : "(") + gap + ")");
      }
      if (parts.empty()) {
        // It asks nothing more than the state's actions already do.
        if (prod.tests.size() == 1) break;
        parts.push_back("true");
      }
      holds += (holds.empty() ? "(" : " || (") + all_of(parts) + ")";
    }
    if (ahead && !holds.empty())
      holds = "m.lexing().check([](Machine& m, const char* p, Token tok, "
             "[[maybe_unused]] Indented indent) { return " +
             holds + "; }, p, tok, m.indented)";
    if (!prod.opening) return holds;
    for (const auto& [mark, depth] : _cfg.openings[prod.opening - 1]) {
      const auto test = "guard" + std::to_string(mark) + "(m.under(" +
                        std::to_string(depth + prod.rhs.size()) + "))";
      holds = holds.empty() ? test : "(" + holds + ") && " + test;
    }
    return holds;
  }

  // One production reduced, a token in hand or not: its symbols popped, its
  // record closed as the call site below reads it, and that site's target
  // entered.
  void reduce(std::uint32_t production, bool held) {
    const Production& prod = _cfg.productions[production];
    const std::string label = reduce_label(production, held);
    open_function(label, held);
    if (prod.rhs.empty()) {
      line("  if constexpr (::std::is_same_v<M, Explorer>)");
      line("    if (m.options.closure && m.visited(", production, ", p, ", held ? "tok" : "Token{nullptr, 0}",
           ")) return Exit::joined;");
    }
    const std::string holds = holds_of(prod);
    if (!holds.empty() && held) {
      _refutes = true;
      line("  if (!(", holds, ")) ", jump("again"));
    }
    // Once a test that names tokens holds, the token goes back to the text,
    // with the trivia before it: the state reached reads it again.
    const bool gives = held && names(prod);
    // Call sites that close the same record and continue in the same state.
    using Arrival = std::array<std::uint32_t, 2>;
    std::map<Arrival, std::vector<std::uint32_t>> arrivals;
    for (const PackedGoto& site : _p.gotos[prod.lhs])
      arrivals[{_p.closed(prod.entry, site.entry), site.target}]
          .push_back(site.from);
    // Room for the record it may write, and for the frame where it pops none.
    // A record that is always empty stands behind the trivia read, written
    // first.
    const unsigned shapes = _p.shapes[production];
    const bool records = (shapes & (kRecordEmpty | kRecordSeveral)) != 0 &&
                         std::ranges::any_of(arrivals, [](const auto& a) { return a.first[0] != kNone; });
    const bool flushes =
        shapes == kRecordEmpty &&
        std::ranges::none_of(arrivals, [](const auto& a) { return a.first[0] == kNone; });
    const bool afterShift = !held && _afterShift.contains(production);
    if (afterShift) {
      const std::string body = "B" + std::to_string(production);
      line("  open = m.pop(", prod.rhs.size(), ", ", production, ");");
      line("  ", jump(body));
      line("}");
      open_function(body, false);
    }
    if (prod.rhs.empty() || records)
      line("  ", room(std::string("!m.roomy(") + (prod.rhs.empty() ? "true" : "false") + ", " +
                          (records ? "true" : "false") + ")",
                      flushes, _caller));
    if (prod.rhs.empty())
      line("  open = m.here(", production, ");");
    else if (!afterShift)
      line("  open = m.pop(", prod.rhs.size(), ", ", production, ");");
    // One record whatever the site: it is closed before they are told apart.
    const bool oneRecord =
        !arrivals.empty() && arrivals.begin()->first[0] == arrivals.rbegin()->first[0];
    if (oneRecord) close(production, arrivals.begin()->first[0], held, flushes, "  ");
    const auto arrive = [&](const Arrival& arrival, std::string_view indent) {
      if (!oneRecord) close(production, arrival[0], held, flushes, indent);
      const auto go = [&](std::string_view at, bool keeps) {
        line(at, "m.push(", arrival[1], ", open", _splits && keeps ? ", tok" : "", ");");
        if (held && !keeps) line(at, "m.forget(p);");
        line(at, jump(enter(arrival[1], keeps)));
      };
      if (gives) {
        go(indent, false);
        return;
      }
      // A repaired token keeps its assigned terminal across the reduction.
      if (held && _p.states[arrival[1]].rereads()) {
        line(indent, "if (tok.terminal() != ", kEndOfInput, " && (!M::kSteps || !(tok.head & kReplaced))) {");
        go(std::string(indent) + "  ", false);
        line(indent, "}");
      }
      go(indent, held);
    };
    if (arrivals.size() == 1) {
      arrive(arrivals.begin()->first, "  ");
      line("}");
      return;
    }
    // The sites told apart, written once for the productions that tell them
    // apart alike: the calls of a switch written before are dropped.
    std::string before = take();
    line("  switch (m.under()) {");
    for (const auto& [arrival, from] : arrivals) {
      line(cases(from, "    "));
      arrive(arrival, "      ");
    }
    line("    default: __builtin_unreachable();");
    line("  }");
    std::string told = take();
    _out = std::move(before);
    const auto [known, fresh] = _arrivals.try_emplace(std::move(told), _arrivals.size());
    const std::string arrival = "A" + std::to_string(known->second);
    line("  ", jump(arrival));
    line("}");
    if (fresh) {
      open_function(arrival);
      _out += known->first;
      line("}");
    }
  }

  // Where some arms of a cell reduce productions with tests, those whose
  // tests fail are left out: the one arm left is taken.
  void guard(std::uint32_t cell, std::span<const PackedAction> arms) {
    if (arms.size() > 32) return;
    std::string tested;
    for (std::size_t a = 0; a < arms.size(); ++a)
      if (arms[a].kind == PackedAction::Kind::reduce)
        if (const std::string holds = holds_of(_cfg.productions[arms[a].value]); !holds.empty())
          tested += "      if (!(" + holds + ")) alive &= ~(1u << " + std::to_string(a) + ");\n";
    if (tested.empty()) return;
    _guards.emplace(cell, "      {\n"
                          "      [[maybe_unused]] const Token tok = m.tok;\n"
                          "      [[maybe_unused]] const char* const p = m.at;\n"
                          "      ::std::uint32_t alive = " +
                              std::to_string((std::uint64_t{1} << arms.size()) - 1) + "u;\n" +
                              tested +
                              "      if (::std::has_single_bit(alive)) "
                              "return {m.arms + ::std::countr_zero(alive)};\n"
                              "      }\n");
  }

  // Whether a mode reads in a section.
  bool closes() const {
    return std::ranges::any_of(_p.modeClose, [](std::uint16_t t) { return t != kNoToken; });
  }

  // Whether the drive predicts any cell's arm.
  bool predicts() const { return !_prefixes.cells.empty() || !_guards.empty(); }

  // The proved prefixes, with equal cost rows and arm lists written once.
  void prefixes() {
    if (!predicts()) return;
    for (const auto& [row, number] : _prefixes.shifts)
      array("::std::uint8_t", "kPrefixShifts" + std::to_string(number), row);
    for (const auto& [arms, number] : _prefixes.plans) {
      std::vector<std::string> values;
      for (const auto& arm : arms)
        values.push_back("{full" + std::to_string(arm[0]) + (_hides ? "<false>" : "<true>") +
                         ", kPrefixShifts" + std::to_string(arm[1]) + "}");
      array("::cursiv::engine::PrefixArm", "kPrefix" + std::to_string(number), values);
    }
    for (const auto& [stack, number] : _prefixes.stacks)
      array("::std::uint32_t", "kStack" + std::to_string(number), stack);
    std::vector<std::string> paths(_prefixes.paths.size());
    for (const auto& [path, number] : _prefixes.paths) {
      std::vector<std::string> values;
      for (const auto& [mode, terminal] : path.tokens)
        values.push_back("{" + std::to_string(mode) + ", " + std::to_string(terminal) + "}");
      const auto name = std::to_string(number);
      array("::std::array<::std::uint16_t, 2>", "kThrough" + name, values);
      line("constexpr ::cursiv::engine::PrefixPath kPath", name, "{kStack", path.stack, ", kThrough", name, "};");
      paths[number] = std::to_string(path.plan) + ", &kPath" + name;
    }
    line("::cursiv::engine::Prediction predict(Machine& m) {");
    line("  if (m.ended != kEndOfInput || m.tok.hasGroup() || m.tok.end <= m.at ||");
    line("      m.tok.end <= m.src + m.tok.begin(m)) return {};");
    line("  switch (m.arms) {");
    const auto predicted = [&](std::uint32_t plan) {
      if (plan == kNone) return std::string("{}");
      if ((plan & PrefixPredictions::kArm) != 0)
        return "{m.arms + " + std::to_string(plan & ~PrefixPredictions::kArm) + "}";
      const auto args = (plan & PrefixPredictions::kPath) ? paths[plan & ~PrefixPredictions::kPath] : std::to_string(plan);
      return "::cursiv::engine::predict_prefix(m, kPrefix" + args + ")";
    };
    // Cells decided alike share their code.
    using Way = std::tuple<std::string, std::uint32_t, std::uint32_t, std::map<std::uint32_t, std::uint32_t>>;
    std::map<std::uint32_t, Way> ways;
    for (const auto& [cell, guarded] : _guards) ways[cell] = {guarded, 0, kNone, {}};
    for (const auto& [cell, decision] : _prefixes.cells) {
      Way& way = ways[cell.first];
      std::get<1>(way) = cell.second;
      std::get<2>(way) = decision.fallback;
      std::get<3>(way) = decision.contexts;
    }
    std::map<Way, std::vector<std::uint32_t>> alike;
    for (auto& [cell, way] : ways) alike[std::move(way)].push_back(cell);
    for (const auto& [decision, cells] : alike) {
      const auto& [guarded, depth, fallback, contexts] = decision;
      line(cases(cells, "    "));
      _out += guarded;
      if (depth != 0) line("      if (m.top - m.frames <= ", depth, ") return {};");
      if (contexts.empty()) {
        line("      return ", predicted(fallback), ";");
        continue;
      }
      line("      switch (m.top[-", depth + 1, "].state) {");
      std::map<std::uint32_t, std::vector<std::uint32_t>> together;
      for (const auto& [previous, plan] : contexts) together[plan].push_back(previous);
      for (const auto& [plan, previous] : together)
        line(cases(previous, "        "), " return ", predicted(plan), ";");
      line("        default: return ", predicted(fallback), ";");
      line("      }");
    }
    line("    default: return {};");
    line("  }");
    line("}");
    line("");
  }

  const Cfg& _cfg;
  const Packed& _p;
  PrefixPredictions _prefixes;
  // Per cell, the code that leaves out the arms whose tests fail.
  std::map<std::uint32_t, std::string> _guards;
  std::string _out;
  // The labels asked for: per state its lexer and its dispatch, and the
  // reductions in the order they were first reached.
  std::vector<bool> _lexed;
  std::vector<bool> _dispatched;
  // Per state, its shared dispatch and lexer, and its rank in a shared split.
  std::vector<std::uint32_t> _dispatchAs;
  std::vector<std::uint32_t> _lexAs;
  std::vector<std::uint32_t> _twinRank;
  std::map<std::pair<std::uint32_t, std::uint16_t>, std::string> _retry;
  // The terminals of the arms that read a token the matcher read alike.
  std::set<std::uint16_t> _matchTied;
  // Reductions also entered with their final shift already popped.
  std::set<std::uint32_t> _afterShift;
  std::set<std::pair<std::uint32_t, bool>> _reduced;
  std::vector<std::pair<std::uint32_t, bool>> _reduces;
  // The shifts several dispatches take, by target, entry, gap and
  // nesting: the number of each one's label.
  std::map<PackedAction::Key, std::size_t> _shiftOf;
  std::vector<PackedAction> _shifts;
  std::map<std::uint32_t, std::string> _shiftRow;
  // The entries closed over a lone child.
  std::set<std::uint32_t> _fuses;
  // Whether the language hides trivia: a shift then writes what was read.
  bool _hides = false;
  // Whether the dispatch of a state splits a run: the readings that decide it
  // are then told what they derive.
  bool _splits = false;
  // Whether a reduction's tests may refuse the token in hand: `again` is
  // written.
  bool _refutes = false;
  // The actions of the cells with several, in the order of their resume
  // points, and per arm whether its split is of tied terminals.
  std::vector<PackedAction> _arms;
  enum class ArmToken { held, tied, rival };
  std::vector<ArmToken> _armTokens;
  // The switches written that tell the sites of a reduction apart, each
  // with its label's number.
  std::map<std::string, std::size_t> _arrivals;
  // The modes a lexer is written for.
  std::vector<bool> _read;
  // Generated functions and the label to retry after growing the stack.
  std::vector<std::string> _functions;
  std::string _caller;
  std::map<std::string, unsigned> _uses;
  // Per mode, its first bytes as its lexer reads them.
  std::vector<Firsts> _firsts;
  std::vector<unsigned> _sharedWords;
  // The functions that read a token by the automaton's first steps: each
  // body once, numbered in the order met.
  std::map<std::string, std::size_t> _openings;
  std::vector<const std::string*> _openingBodies;
};

}  // namespace

void emit_drive(const Cfg& cfg, const Packed& packed, const LanguageNames& names, WriteSource write) {
  Emitter(cfg, packed).run(names, write);
}

}  // namespace cursiv::engine::gen
