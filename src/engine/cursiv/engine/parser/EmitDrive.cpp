#include "cursiv/engine/parser/EmitDrive.hpp"
#include "cursiv/engine/generator/Generate.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
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

// A cell as the emitter makes it: an `ActionCell` with its two short
// operands, the language's `operands` at its place.
struct Cell {
  std::uint32_t value = 0;
  std::uint16_t terminal = kNoToken;
  std::uint16_t step = 0;
  std::uint16_t element = 0;
  std::uint16_t extra = 0;
  [[nodiscard]] std::uint32_t wide() const { return element | std::uint32_t{extra} << 16; }
};

class Emitter {
 public:
  Emitter(const Cfg& cfg, const Packed& packed) : _cfg(cfg), _p(packed) {
    for (const Trivia trivia : packed.terminalTrivia)
      _hides = _hides || trivia == Trivia::hidden;
    _splits = std::ranges::any_of(packed.states, [&](const PackedState& state) {
      return state.reduce == kNone && has_arms(state);
    });
    _preds.resize(packed.states.size());
    for (std::uint32_t s = 0; s < packed.states.size(); ++s)
      for (const PackedAction& action : packed.states[s].actions)
        if (action.kind == PackedAction::Kind::shift) _preds[action.value].push_back(s);
    for (const std::vector<PackedGoto>& gotos : packed.gotos)
      for (const PackedGoto& site : gotos) _preds[site.target].push_back(site.from);
    for (std::vector<std::uint32_t>& from : _preds) {
      std::ranges::sort(from);
      from.erase(std::ranges::unique(from).begin(), from.end());
    }
    for (const std::vector<PackedGoto>& gotos : packed.gotos)
      _single.push_back(std::ranges::all_of(gotos, [&](const PackedGoto& site) {
        return site.target == gotos.front().target && site.entry == gotos.front().entry;
      }));
    for (std::uint32_t s = 0; s < packed.states.size(); ++s) _tails.push_back(tail_of(s));
    // Per nonterminal, the turns its sites end: one for all of them where they
    // lead to one state, none of the lists' states reading a held token again.
    _ends.resize(packed.gotos.size());
    for (std::uint32_t nt = 0; nt < packed.gotos.size(); ++nt) {
      Ends& ends = _ends[nt];
      bool all = !packed.gotos[nt].empty() && _single[nt];
      bool rereads = false;
      for (const PackedGoto& site : packed.gotos[nt]) {
        const Turn turn = turn_at(site.from, site.target);
        if (turn.list == kNone) {
          all = false;
          continue;
        }
        ends.some = true;
        rereads = rereads || packed.states[turn.list].rereads();
        if (ends.all.list == kNone) ends.all = turn;
        else if (ends.all.list != turn.list) ends.all.list = kSomeList;
      }
      if (!all || (ends.all.list == kSomeList && rereads)) ends.all = {};
    }
  }

  void run(const LanguageNames& names, WriteSource write) {
    const auto ns = names.ns;
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
    const std::string guards = take();
    _read.resize(_p.modeCount);
    // The steps of every language, those that ask of the token first; then
    // the cell that refuses.
    for (const std::string_view fixed : {"newline", "dedent_at_end", "tie", "refuse", "accepted",
                                         "split", "pass", "reread", "retry_tie", "tied_arm",
                                         "rival_arm"})
      step_of("&Drive<L>::template " + std::string(fixed) + "<M>");
    loose({.step = kRefuse});
    // The rows of the states, then the reductions they reach: each asks for
    // more steps.
    _rows.resize(_p.states.size());
    _enters.resize(_p.states.size());
    // The machine's reads and shifts are written per state where the cells
    // of all states fit in a budget.
    constexpr std::size_t kStateCells = std::size_t{1} << 15;
    std::size_t cells = 0;
    for (const PackedState& state : _p.states)
      if (state.reduce == kNone) cells += state.actions.size() + 1;
    _perState = cells <= kStateCells;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s) rows_of(s);
    _prefixes.cache = {};
    lone_states();
    pack();
    // The step that enters each state that reads a token.
    for (std::uint32_t s = 0; s < _p.states.size(); ++s)
      if (_p.states[s].reduce == kNone) _enters[s] = step_of(entered(s), std::string(kRead));
    // The modes some text is read again in: the gaps of shifts, the alphabets
    // of tests, the fallbacks of cells.
    for (const PackedState& state : _p.states) {
      for (const PackedAction& action : state.actions) {
        if (action.gap < kNoGap) _read[action.gap] = true;
        if (action.kind == PackedAction::Kind::reduce) {
          for (const Test& test : _cfg.productions[action.value].tests)
            if (!test.shadows.empty()) _read[test.alphabet] = true;
        } else if (action.kind == PackedAction::Kind::reread) {
          _read[action.value] = true;
          for (const auto rival : _p.retryTerminals[action.rivals]) _read[_p.solo[rival]] = true;
        }
      }
      if (state.reduce == kNone && _refutes && state.again != kNoMode) _read[state.again] = true;
    }
    lexer();
    const std::string lexers = take();
    transitions();
    matcher();
    prefixes();
    tables();
    const std::string bound = take();

    line("// GENERATED by the parser generator -- do not edit.");
    line("#pragma once");
    line("#include <cstdint>");
    line("#include \"cursiv/engine/parser/Language.hpp\"");
    line("");
    line("namespace ", ns, " {");
    line("extern const ::cursiv::engine::Tables kTables;");
    line("extern const ::cursiv::engine::AstTables kAst;");
    line("extern const ::cursiv::engine::GrammarFacts kGrammar;");
    line("}  // namespace ", ns);
    write("Table.hpp", {take()});

    line("// GENERATED by the parser generator -- do not edit.");
    line("#pragma once");
    line("#include <bit>");
    line("#include <cstdint>");
    line("#include <cstring>");
    line("");
    line("#include \"Table.hpp\"");
    line("#include \"cursiv/engine/parser/Drive.hpp\"");
    line("");
    line("// The steps hand the token on by value: no address of theirs outlives a");
    line("// tail call, which GCC cannot tell unoptimised.");
    line("#if defined(__GNUC__) && !defined(__clang__)");
    line("#pragma GCC diagnostic ignored \"-Wmaybe-musttail-local-addr\"");
    line("#endif");
    line("");
    line("namespace ", ns, " {");
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility push(hidden)");
    line("#endif");
    _out += _declared;
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility pop");
    line("#endif");
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
    openings();
    _out += lexers;
    _out += guards;
    // The tests of the productions that have some.
    for (const auto& [production, holds] : _holds) {
      if (holds.empty()) continue;
      line("template <class M>");
      line("CURSIV_ALWAYS_INLINE inline bool holds", production, "([[maybe_unused]] M& m, ",
           "[[maybe_unused]] const char* p, [[maybe_unused]] Token tok) {");
      line("  return ", holds, ";");
      line("}");
    }
    line("// The language as its steps read it.");
    line("struct L {");
    line("  static constexpr ::std::uint32_t states = ", _p.states.size(), ";");
    line("  static constexpr ::std::uint32_t arms = ", _armsAt, ";");
    line("  static constexpr bool splits = ", _arms.empty() ? "false" : "true", ";");
    line("  static constexpr ::std::uint16_t dedent = ", _cfg.dedent, ";");
    line("  static constexpr bool indents = ", _cfg.indent != 0 || _cfg.dedent != 0 ? "true" : "false", ";");
    line("  static constexpr bool hides = ", _hides ? "true" : "false", ";");
    line("  static constexpr bool matching = ", _p.matcher.entries.empty() ? "false" : "true", ";");
    line("  static constexpr ActionCells cells{kLrCells};");
    line("  static constexpr const ::std::uint32_t* operands = kLrOperands;");
    line("  static constexpr const ::std::uint32_t* rows = kLrRows;");
    line("  static constexpr const ::std::uint16_t* enters = kLrEnters;");
    line("  static constexpr ::std::uint32_t terminals = ", _p.terminalElement.size(), ";");
    line("  static constexpr const ::std::uint64_t* terminalSets = kTerminalSets;");
    line("  static constexpr const ::std::uint16_t* followOf = kFollowOf;");
    line("  static constexpr ::std::uint32_t followWords = ", (_p.terminalElement.size() + 63) / 64, ";");
    line("  static constexpr const ::std::uint32_t* gotoBase = kLrGotoBase;");
    line("  static constexpr GotoCells gotos{kLrGotos};");
    line("  static constexpr const TieCheck* ties = kLrTieChecks;");
    line("  static constexpr const ReadAgain* agains = kLrAgains;");
    line("  static constexpr ::std::uint32_t againCount = ", _agains.size(), ";");
    line("  static constexpr const Reduction* reductions = kLrReductions;");
    line("  static constexpr const ::std::uint64_t* records = kLrRecords;");
    line("  static constexpr const ::std::uint32_t* fuseMap = kFuseMap;");
    line("  static constexpr Reader keep = full<true>;");
    line("  template <class M>");
    line("  static constexpr State<M>* const steps[] = {");
    for (const auto& expression : _stepping) line("      ", expression, ",");
    line("  };");
    line("};");
    line("using D = Drive<L>;");
    line("");
    line("template <class M>");
    line("CURSIV_ALWAYS_INLINE inline Exit run_states(M& m) { return D::run(m); }");
    line("");
    line("}  // namespace");
    line("");
    line("}  // namespace ", ns);
    write("ParserStates.hpp", {take()});

    // The machine's steps, in its unit alone, with the reductions they are
    // written for.
    line("namespace {");
    line("using M = Machine;");
    for (std::size_t step = 0; step < _stepReduces.size(); ++step)
      if (_stepReduces[step] != kNone)
        line("constexpr Reduction kR", step, " = ", _reductions[step], ";");
    line("template <>");
    line("constexpr State<M>* const L::steps<M>[] = {");
    for (const auto& expression : _steps) line("    ", entering(expression), ",");
    line("};");
    line("}  // namespace");
    const std::string code = take();

    for (const auto& [type, file] :
         {std::pair{"Machine", "Parser.cpp"}, {"Explorer", "Explorer.cpp"}, {"Repairer", "Repairer.cpp"}}) {
      const bool machine = std::string_view(type) == "Machine";
      line("// GENERATED by the parser generator -- do not edit.");
      line("#include \"ParserStates.hpp\"");
      if (machine && predicts()) line("#include \"cursiv/engine/parser/Split.hpp\"");
      line("");
      const bool scans = machine && std::ranges::any_of(_p.matcher.exits, [](const MatchExit& exit) {
        return exit.action == MatchAction::scan;
      });
      if (scans) {
        line("#include <", names.header, ">");
        line("#include \"cursiv/engine/grammar/Facts.hpp\"");
      }
      line("namespace ", ns, " {");
      if (machine) _out += code;
      if (scans) line("using ScanDependencies = ::cursiv::engine::reached_all_t<::cursiv::engine::Facts<::",
                      names.grammar, ">::stage>;");
      line("::cursiv::engine::Exit drive(::cursiv::engine::", type, "& m) { return run_states(m); }");
      if (machine) {
        line("::cursiv::engine::Exit drive(::cursiv::engine::Explorer&);");
        line("::cursiv::engine::Exit drive(::cursiv::engine::Repairer&);");
        line("namespace {");
        _out += bound;
        line("}  // namespace");
        line("constinit const ::cursiv::engine::Tables kTables = make_tables();");
      }
      line("}  // namespace ", ns);
      write(file, {take()});
    }

    line("// GENERATED by the parser generator -- do not edit.");
    line("#include \"cursiv/engine/parser/Tables.hpp\"");
    line("");
    line("namespace ", ns, " {");
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility push(hidden)");
    line("#endif");
    _out += _declared;
    line("#if defined(__ELF__) || defined(__APPLE__)");
    line("#pragma GCC visibility pop");
    line("#endif");
    const std::string head = take();
    line("}  // namespace ", ns);
    // The tables, most of this unit, are written where they are.
    write("Transitions.cpp", {head, _tables, take()});
  }

 private:
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

  // A reduction of `production` is reached, a token in hand or not.
  void reached(std::uint32_t production, bool held) {
    if (_reduced.emplace(production, held).second) _reduces.emplace_back(production, held);
  }

  template <class Values>
  void array(std::string_view type, std::string_view name, const Values& values,
             std::size_t perLine = 24, std::string_view filler = "0") {
    emit_array(_out, type, name, values, perLine, filler);
  }
  // A name of the engine as the unit of the tables writes it.
  static std::string engine(std::string_view name) { return "::cursiv::engine::" + std::string(name); }
  // An array of plain values: the unit of the tables defines it, the code
  // reads its declaration.
  template <class Values>
  void table(std::string_view type, std::string_view name, const Values& values,
             std::size_t perLine = 24, std::string_view filler = "0") {
    emit_array(_tables, type, name, values, perLine, filler);
    _declared += "extern const " + std::string(type) + " " + std::string(name) + "[" +
                 std::to_string(std::max<std::size_t>(std::ranges::size(values), 1)) + "];\n";
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
      table("::std::uint" + std::to_string(sizeof(T) * 8) + "_t", label, values);
      bindings += "  t." + name + " = " + label + ";\n";
    };
    field("byteClass", _p.byteClass);
    table("::std::uint16_t", "kLexNext", _p.lexNext);
    table("::std::uint32_t", "kNextBlocks", _p.nextBlocks);
    std::vector<std::string> values;
    for (std::size_t s = 0; s < _p.nextOf.size(); ++s)
      values.push_back("{" + std::to_string(_p.nextOf[s]) + "," + std::to_string(_p.lexAccept[s]) +
                       "," + std::to_string(_p.lexReachOf[s]) + "}");
    table(engine("LexState"), "kLexStates", values, 8, "{}");
    field("lexRun", _p.lexRun);
    field("runSets", _p.runSets);
    table("::std::uint16_t", "kLexWin", _p.lexWin);
    table("::std::uint32_t", "kWinBlocks", _p.winBlocks);
    table("::std::uint8_t", "kLexOnward", _p.lexOnward);
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
    values.clear();
    for (const LexResolution& r : _p.resolutions)
      values.push_back("{" + std::to_string(r.token) + "," + std::to_string(r.otherwise) + "," +
                       std::to_string(r.back) + "," + engine("LexTest::") +
                       std::string(kTests[static_cast<unsigned>(r.test)]) +
                       (r.keyword ? ",true," : ",false,") + (r.named ? "true," : "false,") +
                       (r.literal ? "true," : "false,") + std::to_string(r.waits) + "}");
    table(engine("LexResolution"), "kResolutions", values, 4, "{}");
    std::string text;
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
        table("::std::uint8_t", name + "Class", m.classOf);
        table("::std::uint16_t", name + "Next", m.next);
        table("::std::uint8_t", name + "Flags", m.flags);
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
      line("  {", _read[mode] ? (_hides ? "full<false>" : "full<true>") : "nullptr",
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
    line("  t.tied = kLrTied;");
    line("  t.tiedWords = kLrWords;");
    if (!_p.matcher.entries.empty()) line("  t.match = &kMatch;");
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

  // The matcher's automaton as the runtime reads it.
  void matcher() {
    const LexMatcher& m = _p.matcher;
    if (m.entries.empty()) return;
    const auto row = [&](std::size_t state) {
      return std::span(m.next).subspan(state * m.nclasses, m.nclasses);
    };
    const auto dead = [&](std::size_t state) {
      return std::ranges::all_of(row(state), [](std::uint16_t to) { return to == 0; });
    };
    // Whether a thread at `state` ends, and whether it does nothing else.
    const auto returns = [&](std::size_t state) {
      const auto& ends = m.accepts[state];
      return ends.size() == 1 && ends.front().back == 0 && ends.front().guard == LexTest::none &&
             m.exits[ends.front().term].action == MatchAction::end;
    };
    const auto just_returns = [&](std::size_t state) { return returns(state) && dead(state); };
    std::map<ByteSet, std::uint16_t> known;
    std::vector<std::uint64_t> sets;
    const auto set_of = [&](const ByteSet& bytes) {
      const auto [it, fresh] = known.try_emplace(bytes, static_cast<std::uint16_t>(known.size() + 1));
      if (fresh) sets.insert(sets.end(), bytes.words.begin(), bytes.words.end());
      return it->second;
    };
    // The bytes that lead out of `state`.
    const auto leaving = [&](std::size_t state) {
      ByteSet bytes;
      for (unsigned b = 0; b < 256; ++b)
        if (row(state)[m.classOf[b]] != 0) bytes.add(b);
      return bytes;
    };
    // The bytes the thread an exit makes can begin with, where it reads a
    // byte first and a few ranges hold them.
    const auto first = [&](const MatchExit& e) -> std::uint16_t {
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
          return 0;
      }
      if (from == 0 || !m.accepts[from].empty()) return 0;
      const ByteSet bytes = leaving(from);
      unsigned ranges = 0;
      for (unsigned b = 0; b < 256; ++b) ranges += bytes.has(b) && (b == 0 || !bytes.has(b - 1));
      return ranges == 0 || ranges > 3 || bytes == ~ByteSet{} ? 0 : set_of(bytes);
    };
    // The byte a call may read past its return: its continuation ends, or
    // reads one byte and ends.
    const auto suffix = [&](const MatchExit& e) -> std::uint16_t {
      if (e.action != MatchAction::call) return 0;
      const auto state = m.entries[e.to];
      if (!returns(state)) return 0;
      for (const auto to : row(state))
        if (to != 0 && !just_returns(to)) return 0;
      const ByteSet bytes = leaving(state);
      return bytes.any() ? set_of(bytes) : 0;
    };
    static constexpr std::string_view kActions[] = {"end",   "call",  "jump",   "open", "close",
                                                    "echo",  "ahead", "unless", "scan"};
    static constexpr std::string_view kGuards[] = {"none", "match", "word", "eof", "waits"};
    // The scanners in use, numbered in their order.
    std::map<std::uint32_t, std::uint32_t> scans;
    for (const MatchExit& e : m.exits)
      if (e.action == MatchAction::scan) scans.try_emplace(e.arg, 0);
    std::vector<std::string> values;
    for (auto& [type, number] : scans) {
      number = static_cast<std::uint32_t>(values.size());
      values.push_back("scan" + std::to_string(type));
      line("::std::size_t ", values.back(), "(::std::string_view in) {");
      line("  return ::cursiv::detail::tl_at_t<", type, ", ScanDependencies>::read(in);");
      line("}");
    }
    array("MatchScan", "kMatchScans", values, 8, "nullptr");
    values.clear();
    for (const MatchExit& e : m.exits) {
      const bool scan = e.action == MatchAction::scan;
      values.push_back("{" + engine("MatchAction::") + std::string(kActions[static_cast<unsigned>(e.action)]) +
                       ", " + std::to_string(e.to) + ", " +
                       std::to_string(scan ? scans.at(e.arg) : e.arg) + ", " +
                       std::to_string(first(e)) + ", " + std::to_string(suffix(e)) + ", " +
                       (scan && just_returns(m.entries[e.to]) ? "true}" : "false}"));
    }
    table(engine("MatchExit"), "kMatchExits", values, 4, "{}");
    values.clear();
    std::vector<std::string> states;
    for (std::size_t state = 0; state <= m.accepts.size(); ++state) {
      const bool last = state == m.accepts.size();
      states.push_back("{" + (last || dead(state) ? engine("kNoRow") : std::to_string(_p.matchOf[state])) +
                       ", " + std::to_string(values.size()) + ", " +
                       std::to_string(last ? 0 : _p.matchRun[state]) + "}");
      if (last) break;
      for (const LexAccept& a : m.accepts[state])
        values.push_back("{" + std::to_string(a.term) + ", " + std::to_string(a.waits) + ", " +
                         std::to_string(a.back) + ", " + engine("LexTest::") +
                         std::string(kGuards[static_cast<unsigned>(a.guard)]) + "}");
    }
    table(engine("MatchEnd"), "kMatchEnds", values, 4, "{}");
    table(engine("MatchState"), "kMatchStates", states, 6, "{}");
    table("::std::uint8_t", "kMatchClass", m.classOf);
    table("::std::uint16_t", "kMatchNext", _p.matchNext);
    table("::std::uint32_t", "kMatchBlocks", _p.matchBlocks);
    table("::std::uint16_t", "kMatchEntries", m.entries);
    table("::std::uint64_t", "kMatchSets", sets);
    line("constexpr MatchTables kMatch{kMatchClass, {kMatchNext, kMatchBlocks}, kMatchStates, kMatchEnds,");
    line("                             kMatchExits, kMatchEntries, kMatchSets, kMatchScans};");
    line("");
  }

  // The number of the fuse of a lone child under a record of `entry`.
  std::uint16_t fuse_of(std::uint32_t entry) {
    return _fuses.try_emplace(entry, static_cast<std::uint16_t>(_fuses.size() + 1)).first->second;
  }

  // Ignored tokens continue the reader. The full read records hidden trivia.
  void give(std::uint16_t terminal, std::string_view end, std::string_view indent) {
    const Trivia trivia = _p.terminalTrivia[terminal];
    if (_p.matcher.matched[terminal] || trivia == Trivia::hidden) line(indent, "return Token{p, kLexWalk};");
    else if (trivia != Trivia::ignored) line(indent, "return token(begin, ", end, ", ", terminal, ");");
    // A dropped token: the lexer reads on, from a word of its own by a tail call.
    else if (_ownWord) line(indent, "CURSIV_READ_ON(", end, ");");
    else line(indent, "p = ", end, ";\n", indent, "continue;");
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

  // The test that no word goes on at `where`, by the byte there.
  static std::string no_word_at(const std::string& where) {
    return "(" + where + " == end || t.wordBytes[byte_at(" + where + ")] == 0)";
  }
  // Where the word class reaches past ASCII, a byte past it at `where` is a
  // scalar's: the walk reads the token `tests` have read so far.
  void walk_before_wide(std::vector<std::string> tests, const std::string& where) {
    if (_p.wordRanges.empty() && !_p.wordInvalid) return;
    tests.push_back(where + " != end");
    tests.push_back("byte_at(" + where + ") >= 0x80");
    line("        if (", all_of(tests), ") return Token{p, kLexWalk};");
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
      if (guarded) {
        walk_before_wide(tests, "e");
        tests.push_back(no_word_at("e"));
      }
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
      if (term.word) {
        walk_before_wide(tests, "p + " + length);
        tests.push_back(no_word_at("p + " + length));
      }
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
  // literal, and by the state the byte leads to.
  struct Firsts {
    std::map<std::uint32_t, std::vector<unsigned>> byCode;
    std::map<std::uint32_t, std::vector<unsigned>> byState;
  };
  [[nodiscard]] Firsts firsts(std::uint16_t mode) const {
    const std::uint32_t* const first = _p.lexFirst.data() + std::size_t{mode} * 256;
    // A mode that does not pass all the grammar drops reads them as tokens.
    const bool skips = _p.modeSkips[mode];
    Firsts out;
    for (unsigned b = 0; b < 256; ++b) {
      if (skips && _p.skipped.has(b)) {
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
      // Out of the lexer, which stays small enough to stand in the steps.
      line("CURSIV_NOINLINE inline Token open", id,
           "(Machine& m, const char* p, [[maybe_unused]] ::std::uint32_t mode) {");
      line("  [[maybe_unused]] const auto begin = static_cast<TextOffset>(p - m.src);");
      line("  const char* e = p + 1;");
      _out += *_openingBodies[id];
      line("}");
      line("");
    }
  }

  // The lexer of every mode: one switch over the arm of the first byte, read
  // in the mode's row of `kLexArms`. Arm 0 leaves the token to the walk, arm
  // 1 drops the byte.
  void lexer() {
    static constexpr std::string_view kWalk = "        return Token{p, kLexWalk};\n";
    // What an arm reads: its code, or the function it goes on in.
    struct Arm {
      std::string callee, code;
    };
    // Each arm once, by what it reads: a first-byte code, a token of one
    // byte, or an opening. One that walks is arm 0.
    std::map<std::uint64_t, std::uint16_t> armOf;
    std::vector<Arm> bodies;
    const auto arm = [&](std::uint64_t key, auto write) -> std::uint16_t {
      const auto [it, fresh] = armOf.try_emplace(key, 0);
      if (!fresh) return it->second;
      std::string outer = std::exchange(_out, {});
      Arm made{.callee = write(), .code = std::exchange(_out, std::move(outer))};
      if (made.code != kWalk) {
        if (bodies.size() + 2 == 0xFFFF) throw Refused("the terminals start in too many ways for one lexer");
        it->second = static_cast<std::uint16_t>(bodies.size() + 2);
        bodies.push_back(std::move(made));
      }
      return it->second;
    };
    std::vector<std::uint32_t> wordOf;
    std::map<std::vector<std::uint16_t>, std::uint32_t> rowOf;
    std::vector<std::uint16_t> arms;
    std::vector<std::uint32_t> rowAt(_p.modeCount);
    bool skips = false;
    for (std::uint16_t mode = 0; mode < _p.modeCount; ++mode) {
      if (!_read[mode]) continue;
      std::vector<std::uint16_t> row(256);
      if (_p.modeSkips[mode])
        for (unsigned b = 0; b < 256; ++b) row[b] = _p.skipped.has(b);
      skips = skips || std::ranges::find(row, 1) != row.end();
      const auto [byCode, byState] = firsts(mode);
      for (const auto& [code, bytes] : byCode) {
        const auto at = code & kFirstIndex;
        const auto id = arm(code, [&] {
          if (code < kFirstWord) literals(_p.literalPaths[at]);
          return std::string();
        });
        if (code >= kFirstWord) {
          wordOf.resize(bodies.size(), kNone);
          wordOf[id - 2] = at;
        }
        for (const unsigned b : bytes) row[b] = id;
      }
      // A byte that is a token alone; the others are read by `full`.
      for (const auto& [to, bytes] : byState) {
        std::uint16_t id = 0;
        if (alone(mode, to)) {
          const auto terminal = win(mode, to);
          id = arm(std::uint64_t{1} << 32 | terminal, [&] {
            give(terminal, "p + 1", "        ");
            return std::string();
          });
        } else if (!_p.lexTrivia[to]) {
          const auto at = opening(mode, to);
          id = arm(std::uint64_t{2} << 32 | at, [&] { return "open" + std::to_string(at); });
        }
        for (const unsigned b : bytes) row[b] = id;
      }
      const auto [it, fresh] = rowOf.try_emplace(std::move(row), static_cast<std::uint32_t>(arms.size()));
      if (fresh) arms.insert(arms.end(), it->first.begin(), it->first.end());
      rowAt[mode] = it->second;
    }
    if (skips && _p.resumed.any()) {
      std::array<std::uint8_t, 256> resumed;
      for (unsigned b = 0; b < resumed.size(); ++b) resumed[b] = _p.resumed.has(b);
      array("::std::uint8_t", "kResumed", resumed);
    }
    if (bodies.size() + 2 > 0x100) table("::std::uint16_t", "kLexArms", arms);
    else table("::std::uint8_t", "kLexArms", std::vector<std::uint8_t>(arms.begin(), arms.end()));
    array("::std::uint32_t", "kLexRow", rowAt);
    // The lexer stands in each step that reads a token while its copies hold
    // at most `kInlineArms` arms; it is called otherwise.
    constexpr std::size_t kInlineArms = 1024;
    std::set<std::uint16_t> reading;
    for (std::uint32_t s = 0; s < _p.states.size(); ++s)
      if (_p.states[s].reduce == kNone) reading.insert(_enters[s]);
    const std::size_t readers = reading.size();
    const bool inlined = readers != 0 && (bodies.size() + 2) * readers <= kInlineArms;
    // A word is read out of the switch; where the switch stands in the
    // steps, one that spells nothing or drops its token stays in it.
    wordOf.resize(bodies.size(), kNone);
    const auto ignored = [&](auto t) { return t != kNoToken && _p.terminalTrivia[t] == Trivia::ignored; };
    if (!inlined) line("Token lex(Machine& m, const char* p, ::std::uint32_t mode);");
    _ownWord = !inlined;
    for (std::size_t id = 0; id < bodies.size(); ++id) {
      if (wordOf[id] == kNone) continue;
      const WordPath& path = _p.wordPaths[wordOf[id]];
      if (inlined && (path.spelled.empty() || ignored(path.terminal) ||
                      std::ranges::any_of(path.spelled, [&](const auto& x) { return ignored(x.terminal); }))) {
        std::string outer = std::exchange(_out, {});
        word(path);
        bodies[id].code = std::exchange(_out, std::move(outer));
        continue;
      }
      bodies[id].callee = "word" + std::to_string(wordOf[id]);
      line("CURSIV_NOINLINE inline Token word", wordOf[id],
           "(Machine& m, const char* p, [[maybe_unused]] ::std::uint32_t mode) {");
      line("  const Tables& t = m.t;");
      line("  [[maybe_unused]] const char* const end = m.end;");
      line("  const char* const stop = m.mark;");
      line("  [[maybe_unused]] const auto begin = static_cast<TextOffset>(p - m.src);");
      word(path);
      line("}");
    }
    _ownWord = false;
    line(inlined ? "CURSIV_ALWAYS_INLINE" : "CURSIV_NOINLINE",
         " inline Token lex(Machine& m, const char* p, ::std::uint32_t mode) {");
    line("  [[maybe_unused]] const Tables& t = m.t;");
    line("  [[maybe_unused]] const char* const end = m.end;");
    line("  const auto* const arms = kLexArms + kLexRow[mode];");
    line("  for (;;) {");
    line("    [[maybe_unused]] const char* const first = p;");
    line("    const char* const stop = m.mark;");
    if (skips) line("    while (p < stop && arms[byte_at(p)] == 1) ++p;");
    line("    if (p >= stop) return Token{p, kLexStop};");
    if (skips && _p.resumed.any())
      line("    if (p != first && kResumed[byte_at(p)]) return Token{first, kLexWalk};");
    line("    [[maybe_unused]] const auto begin = static_cast<TextOffset>(p - m.src);");
    line("    switch (arms[byte_at(p)]) {");
    for (std::size_t id = 0; id < bodies.size(); ++id) {
      line("      case ", id + 2, ": {");
      // A called lexer goes on by a tail call: it keeps nothing across it.
      if (bodies[id].callee.empty()) _out += bodies[id].code;
      else if (inlined) line("        return ", bodies[id].callee, "(m, p, mode);");
      else line("        CURSIV_READ_WORD(", bodies[id].callee, ");");
      line("      }");
    }
    line("      case 0: break;");
    line("      default: __builtin_unreachable();");
    line("    }");
    line("    return Token{p, kLexWalk};");
    line("  }");
    line("}");
    line("");
    line("template <bool Keep>");
    line("CURSIV_ALWAYS_INLINE inline Token full(Machine& m, const char* p, ::std::uint32_t mode) {");
    line("  const Token x = lex(m, p, mode);");
    line("  if (x.terminal() >= kLexStop) [[unlikely]] return m.read_slow(x, mode, Keep, lex);");
    line("  return x;");
    line("}");
    line("");
  }

  // The step an expression names, a function over machine `M`; `stepping`
  // names the one a machine that steps takes, where it is another, and
  // `reduces` the production it reduces: a step reduces one production.
  std::uint16_t step_of(const std::string& expression, const std::string& stepping = {},
                        std::uint32_t reduces = kNone) {
    const auto [at, fresh] =
        _stepOf.try_emplace({expression, reduces}, static_cast<std::uint16_t>(_steps.size()));
    if (fresh) {
      if (_steps.size() == 0xFFFF) throw Refused("the grammar takes too many kinds of steps");
      // An expression names its own step by `@`.
      std::string written = expression;
      const std::string index = std::to_string(_steps.size());
      for (auto at = written.find('@'); at != std::string::npos; at = written.find('@'))
        written.replace(at, 1, index);
      _stepping.push_back(stepping.empty() ? written : stepping);
      _steps.push_back(std::move(written));
      _stepReduces.push_back(reduces);
    }
    return at->second;
  }
  // The steps every language has, in the order `run` names them.
  static constexpr std::uint16_t kNewline = 0, kDedentAtEnd = 1, kTie = 2, kRefuse = 3,
                                 kAccepted = 4, kSplit = 5, kPass = 6, kReread = 7,
                                 kRetryTie = 8, kTiedArm = 9, kRivalArm = 10;

  // The steps that reduce a production: over the machine, one written for its
  // reduction, which `@` numbers; over a machine that steps, the general one
  // for its form, with its tests.
  std::pair<std::string, std::string> reductions_of(std::uint32_t production, bool held) {
    const Production& prod = _cfg.productions[production];
    const std::vector<PackedGoto>& gotos = _p.gotos[prod.lhs];
    unsigned form = 0;
    if (prod.rhs.empty()) form |= kReadsNothing;
    if (!gotos.empty() && _single[prod.lhs]) {
      form |= kOneSite;
      if (held && _p.states[gotos.front().target].reduce != kNone) form |= kThenReduces;
    }
    if (held && names(prod)) form |= kGivesBack;
    if (!prod.takes.empty()) form |= kTakes;
    const auto step = [&](unsigned as) {
      return "&Drive<L>::template reduce<M, " + std::string(held ? "true, " : "false, ") +
             std::to_string(as) + ", " + std::to_string(_p.shapes[production]);
    };
    const bool holds = held && tested(production);
    std::string tests;
    if (holds) {
      _refutes = true;
      tests = ", &holds" + std::to_string(production) + "<M>";
    }
    reached(production, held);
    // The machine's form where sites end a turn of a list: all, or some.
    unsigned machine = form;
    if (_ends[prod.lhs].all.list != kNone) machine = (form & ~unsigned{kThenReduces}) | kStays;
    else if ((form & kOneSite) == 0 && _ends[prod.lhs].some) machine |= kMayStay;
    return {step(machine) + tests + (holds ? ", &kR@>" : ", nullptr, &kR@>"),
            step(form) + tests + ">"};
  }
  // The turn of a list that a transition from `from` to `tail` ends: the
  // list's state and the `drops` frames over it, where that state is one.
  struct Turn {
    std::uint32_t list = kNone;
    std::uint32_t drops = 0;
  };
  // As the list of a turn: the stack tells it.
  static constexpr std::uint32_t kSomeList = kNone - 1;
  Turn turn_at(std::uint32_t from, std::uint32_t tail) {
    if (_tails[tail] == 0) return {};
    const auto [known, fresh] = _turns.try_emplace({from, tail});
    if (!fresh) return known->second;
    constexpr std::size_t kFewStates = 16;
    const std::uint32_t drops = _tails[tail] - 1;
    std::vector<std::uint32_t> under{from}, below;
    for (std::uint32_t k = 0; k < drops && under.size() <= kFewStates; ++k) {
      below.clear();
      for (const std::uint32_t s : under)
        below.insert(below.end(), _preds[s].begin(), _preds[s].end());
      std::ranges::sort(below);
      below.erase(std::ranges::unique(below).begin(), below.end());
      under.swap(below);
    }
    if (under.size() == 1 && _p.states[under.front()].reduce == kNone)
      known->second = {under.front(), drops};
    return known->second;
  }
  // Where state `s` only reduces a turn of a list, `L -> L ..`, with no
  // record, no test and no entry given: the frames it drops plus one.
  std::uint32_t tail_of(std::uint32_t s) const {
    const PackedState& state = _p.states[s];
    if (state.reduce == kNone || state.rereads() || s >= 1u << 24) return 0;
    const Production& prod = _cfg.productions[state.reduce];
    if (prod.rhs.size() < 2 || prod.rhs.size() > 0x100 || prod.rhs[0] != nonterminal(prod.lhs) ||
        prod.tested() || prod.implied || prod.start || !prod.takes.empty() ||
        !std::ranges::all_of(_p.gotos[prod.lhs], [&](const PackedGoto& site) {
          return _p.closed(prod.entry, site.entry) == kNone;
        }))
      return 0;
    return static_cast<std::uint32_t>(prod.rhs.size() - 1);
  }
  std::uint16_t reduce_step(std::uint32_t production, bool held) {
    const auto [machine, stepping] = reductions_of(production, held);
    return step_of(machine, stepping, production);
  }

  // A step's expression, each step it names by the state it enters, `$state;`,
  // given its number.
  std::string entering(std::string expression) const {
    for (auto at = expression.find('$'); at != std::string::npos; at = expression.find('$', at)) {
      const auto end = expression.find(';', at);
      const auto state = std::stoul(expression.substr(at + 1, end - at - 1));
      expression.replace(at, end - at + 1, std::to_string(_enters[state]));
    }
    return expression;
  }

  // The step that enters state `s` with no token: over the machine, a read
  // written for its mode, per state for its row, and for the cells of a row
  // of few.
  static constexpr std::string_view kRead = "&Drive<L>::template read<M>";
  std::string entered(std::uint32_t s) {
    constexpr std::size_t kFewCells = 3;
    _read[_p.states[s].mode] = true;
    std::string read = "&Drive<L>::template read<M, " + std::to_string(_p.states[s].mode);
    if (_perState) {
      const Row& row = _rows[s];
      read += ", " + std::to_string(_bases[s]);
      if (row.cells.size() <= kFewCells) {
        read += ", " + std::to_string(_loose[row.otherwise].step);
        for (const Cell& cell : row.cells)
          read += ", " + std::to_string(cell.terminal) + ", " + std::to_string(cell.step);
      }
    }
    return read + ">";
  }

  // A cell no row holds, written once: its place among the cells.
  std::uint32_t loose(Cell cell) {
    cell.terminal = kNoToken;
    const auto [at, fresh] = _looseOf.try_emplace(
        std::tuple(cell.value, cell.step, cell.element, cell.extra),
        static_cast<std::uint32_t>(_loose.size()));
    if (fresh) _loose.push_back(cell);
    return at->second;
  }

  // The cell that shifts the token in hand: the machine's shift goes on to
  // the step that enters the state reached, `$state;`, and per state knows
  // that state and its element. A turn of a list ended, its state stays.
  Cell shift_cell(std::uint32_t s, const PackedAction& action) {
    const std::string shift = "&Drive<L>::template shift<M" + shift_how(action);
    std::string machine = shift + ", ";
    if (const Turn turn = turn_at(s, action.value); turn.list != kNone) {
      machine += (_perState ? "$" + std::to_string(turn.list) + ";"
                            : std::to_string(step_of(entered(turn.list), std::string(kRead)))) +
                 ", " + engine("kInPlace") + ", " +
                 (_perState ? std::to_string(_p.element(action.entry)) : engine("kNoStack")) +
                 ", " + std::to_string(turn.drops);
    } else if (_perState) {
      machine += "$" + std::to_string(action.value) + ";, " + std::to_string(action.value) + ", " +
                 std::to_string(_p.element(action.entry));
    } else {
      const std::uint32_t lone = _p.states[action.value].reduce;
      machine += std::to_string(lone == kNone ? step_of(entered(action.value), std::string(kRead))
                                              : reduce_step(lone, false));
    }
    return {.value = action.value,
            .terminal = action.terminal,
            .step = step_of(machine + ">", shift + ">"),
            .element = static_cast<std::uint16_t>(_p.element(action.entry)),
            .extra = action.gap};
  }
  std::string shift_how(const PackedAction& action) const {
    static constexpr std::string_view kNestings[] = {"none", "indent", "dedent", "open", "close"};
    return std::string(action.gap != kAnyGap ? ", true" : ", false") + ", Nesting::" +
           std::string(kNestings[static_cast<unsigned>(_cfg.nesting(action.terminal))]);
  }
  // The cell of one action of state `s`; `rows` names its row per mode.
  Cell action_cell(std::uint32_t s, const PackedAction& action,
                         const std::map<std::uint16_t, std::uint32_t>& rows) {
    switch (action.kind) {
      case PackedAction::Kind::shift:
        return shift_cell(s, action);
      case PackedAction::Kind::reduce:
        return {.terminal = action.terminal, .step = reduce_step(action.value, true)};
      case PackedAction::Kind::accept:
        return {.terminal = action.terminal, .step = kAccepted};
      case PackedAction::Kind::reread:
        return {.value = rows.at(static_cast<std::uint16_t>(action.value)),
                .terminal = action.terminal,
                .step = kReread,
                .element = static_cast<std::uint16_t>(action.value)};
      case PackedAction::Kind::retryTie:
        break;
    }
    return {.value = s, .step = kRetryTie};
  }

  // An arm a run goes on from: `action` on the token in hand, read first as
  // the action's terminal where the arm is of tied or left-out terminals.
  enum class ArmToken { held, tied, rival };
  void arm(ArmToken source, const Cell& action) {
    if (source == ArmToken::held)
      _arms.push_back(action);
    else
      _arms.push_back({.value = loose(action),
                       .terminal = action.terminal,
                       .step = source == ArmToken::tied ? kTiedArm : kRivalArm});
  }
  // The cell that stops a run on the arms from `first` on.
  Cell split_cell(std::uint16_t terminal, std::size_t first, std::size_t rereads) const {
    const std::size_t count = _arms.size() - first;
    if (count > 0xFFFF) throw Refused("a state has too many actions on one token");
    return {.value = static_cast<std::uint32_t>(2 * _p.states.size() + first),
            .terminal = terminal,
            .step = kSplit,
            .element = static_cast<std::uint16_t>(count),
            .extra = static_cast<std::uint16_t>(rereads)};
  }

  // The rows of state `s`: what it does with the token in hand, by the ties
  // of the mode that read it.
  void rows_of(std::uint32_t s) {
    const PackedState& state = _p.states[s];
    if (state.reduce != kNone) return;
    const std::vector<PackedAction>& actions = state.actions;
    // A row per set of ties its modes tell apart, its own mode's first.
    std::vector<std::pair<Ties, std::uint32_t>> variants;
    std::map<std::uint16_t, std::uint32_t> rowOf;
    state.eachMode([&](std::uint16_t mode) {
      if (rowOf.contains(mode)) return;
      Ties ties = ties_of(state, mode);
      auto known = std::ranges::find(variants, ties, [](const auto& v) -> const Ties& { return v.first; });
      if (known == variants.end()) {
        std::uint32_t row = s;
        if (!variants.empty()) {
          row = static_cast<std::uint32_t>(_rows.size());
          _rows.emplace_back();
        }
        variants.emplace_back(std::move(ties), row);
        known = variants.end() - 1;
      }
      rowOf.emplace(mode, known->second);
    });
    // The cells of the tokens it acts on. A cell of several actions stops
    // the run on arms: its actions, those of the terminals its fallback
    // lexer left out, then its fallback.
    std::map<std::uint16_t, Cell> plain;
    std::map<std::uint32_t, std::vector<std::uint16_t>> reduces;
    for (std::size_t i = 0; i < actions.size();) {
      std::size_t end = i + 1;
      while (end != actions.size() && actions[end].terminal == actions[i].terminal) ++end;
      const std::uint16_t terminal = actions[i].terminal;
      if (end - i > 1) {
        const auto cell = std::span(actions).subspan(i, end - i);
        const std::size_t first = _arms.size();
        for (const PackedAction& a : cell.first(cell.size() - 1))
          arm(ArmToken::held, action_cell(s, a, rowOf));
        const std::size_t before = _arms.size();
        for (const auto rival : _p.retryTerminals[cell.back().rivals])
          for (const PackedAction& a : on(state, rival)) arm(ArmToken::rival, action_cell(s, a, rowOf));
        const std::size_t rivals = _arms.size() - before;
        arm(ArmToken::held, action_cell(s, cell.back(), rowOf));
        const bool rereads = cell.back().kind == PackedAction::Kind::reread;
        plain[terminal] = split_cell(terminal, first, rereads ? rivals + 1 : 0);
        if (rivals == 0) {
          prefix_predictions(_prefixes, _cfg, _p, s, plain[terminal].value, cell, false);
          guard(plain[terminal].value, cell);
        }
      } else if (actions[i].kind == PackedAction::Kind::reduce) {
        reduces[actions[i].value].push_back(terminal);
      } else {
        plain[terminal] = action_cell(s, actions[i], rowOf);
      }
      i = end;
    }
    // The reduction most tokens take is the row's own: it has no cell, and
    // is taken where the state acts on the token.
    auto common = reduces.end();
    for (auto it = reduces.begin(); it != reduces.end(); ++it)
      if (it->second.size() >= 8 &&
          (common == reduces.end() || it->second.size() > common->second.size()))
        common = it;
    std::uint32_t otherwise = 0;
    for (auto it = reduces.begin(); it != reduces.end(); ++it) {
      const auto [machine, stepping] = reductions_of(it->first, true);
      if (it == common) {
        const auto followed = [](const std::string& next) {
          return "&Drive<L>::template followed<M, " + next + ">";
        };
        otherwise = loose({.step = step_of(followed(machine), followed(stepping), it->first)});
      }
      for (const std::uint16_t terminal : it->second)
        if (it != common || (_cfg.unread != 0 && terminal == _cfg.unread))
          plain[terminal] = {.terminal = terminal, .step = step_of(machine, stepping, it->first)};
    }
    for (const std::uint16_t terminal : state.passes)
      plain[terminal] = {.value = otherwise, .terminal = terminal, .step = kPass};
    // The arms of a token several terminals read, any of their actions; the
    // last keeps the token as read where a cell of theirs reads it again.
    std::map<std::vector<std::uint16_t>, TieCheck> branches;
    const auto branch = [&](const std::vector<std::uint16_t>& terminals) {
      const auto [it, fresh] = branches.try_emplace(terminals);
      if (fresh) {
        const std::size_t first = _arms.size();
        for (const auto t : terminals)
          for (const PackedAction& a : on(state, t)) arm(ArmToken::tied, action_cell(s, a, rowOf));
        const bool retry = std::ranges::any_of(terminals, [&](auto term) {
          const auto cell = std::ranges::equal_range(actions, term, {}, &PackedAction::terminal);
          return !cell.empty() && cell.back().kind == PackedAction::Kind::reread;
        });
        if (retry) arm(ArmToken::held, {.value = s, .step = kRetryTie});
        const Cell split = split_cell(0, first, retry ? 1 : 0);
        if (!retry) {
          std::vector<PackedAction> taken;
          for (const auto t : terminals) std::ranges::copy(on(state, t), std::back_inserter(taken));
          prefix_predictions(_prefixes, _cfg, _p, s, split.value, taken, true);
        }
        it->second = {split.value, split.element, split.extra, 0, TieCheck::row, 0};
      }
      return it->second;
    };
    const bool dedents = _cfg.dedent != 0 && std::ranges::any_of(actions, [&](const PackedAction& a) {
      return a.terminal == _cfg.dedent;
    });
    for (const auto& [ties, row] : variants) {
      std::map<std::uint16_t, Cell> cells = plain;
      // A cell that asks of the token stands before what its terminal held.
      const auto ask = [&](std::uint16_t terminal, std::uint16_t asking, std::size_t operand) {
        const auto held = cells.find(terminal);
        cells[terminal] = {.value = held == cells.end() ? otherwise : loose(held->second),
                           .terminal = terminal,
                           .step = asking,
                           .element = static_cast<std::uint16_t>(operand),
                           .extra = static_cast<std::uint16_t>(operand >> 16)};
      };
      const auto check = [&](TieCheck made) {
        _ties.push_back(made);
        return _ties.size() - 1;
      };
      // The last asked comes first here. Where the matcher read the token,
      // the terminals the state acts on that rank alike may read its text too.
      for (auto group = ties.matched.rbegin(); group != ties.matched.rend(); ++group) {
        const auto& [rank, alike] = *group;
        TieCheck made = branch(alike);
        made.kind = TieCheck::grouped;
        if (std::ranges::any_of(alike, [&](auto t) { return _p.mixed[t]; })) {
          made.kind = TieCheck::ranked;
          made.test = rank;
        }
        const auto at = check(made);
        for (const std::uint16_t terminal : alike) {
          _matchTied.insert(terminal);
          ask(terminal, kTie, at);
        }
      }
      for (auto tie = ties.dfa.rbegin(); tie != ties.dfa.rend(); ++tie) {
        const Packed::Tie& tied = _p.ties[*tie];
        TieCheck made = branch(tied.terminals);
        made.test = tied.row;
        if (tied.row == kNone) {
          made.kind = TieCheck::alike;
          made.test = static_cast<std::uint32_t>(_words.size());
          for (const auto t : std::span(tied.terminals).subspan(1))
            if (!on(state, t).empty()) {
              _words.push_back(t);
              ++made.n;
            }
        }
        ask(tied.terminals.front(), kTie, check(made));
      }
      if (dedents) ask(kEndOfInput, kDedentAtEnd, row);
      if (_cfg.newline != 0) ask(_cfg.newline, kNewline, 0);
      Row& made = _rows[row];
      made.otherwise = otherwise;
      for (const auto& [terminal, cell] : cells) made.cells.push_back(cell);
    }
    if (state.again != kNoMode) {
      _agains.push_back({s, rowOf.at(state.again), static_cast<std::uint32_t>(_words.size()),
                         static_cast<std::uint16_t>(state.tested.size()), state.again});
      _words.insert(_words.end(), state.tested.begin(), state.tested.end());
    }
  }

  // A state that only reduces is entered by its reduction: with no token
  // where a shift or a reduction made with none leads to it, with one in
  // hand where a reduction made with one does. Entered otherwise, it refuses.
  void lone_states() {
    const auto enter = [&](std::uint32_t state, bool held) {
      if (const std::uint32_t lone = _p.states[state].reduce; lone != kNone) reached(lone, held);
    };
    enter(0, false);
    for (const std::uint32_t reads : _p.conditions) enter(reads, false);
    for (const PackedState& state : _p.states)
      for (const PackedAction& action : state.actions)
        if (action.kind == PackedAction::Kind::shift) enter(action.value, false);
    for (std::size_t r = 0; r < _reduces.size(); ++r) {
      const auto [production, held] = _reduces[r];
      const Production& prod = _cfg.productions[production];
      const bool keeps = held && !names(prod);
      for (const PackedGoto& site : _p.gotos[prod.lhs]) {
        if (!keeps || _p.states[site.target].rereads()) enter(site.target, false);
        if (keeps) enter(site.target, true);
      }
    }
    for (std::uint32_t s = 0; s < _p.states.size(); ++s) {
      const std::uint32_t lone = _p.states[s].reduce;
      if (lone == kNone) continue;
      _enters[s] = kRefuse;
      if (_reduced.contains({lone, false})) _enters[s] = reduce_step(lone, false);
      if (_reduced.contains({lone, true})) _rows[s].otherwise = loose({.step = reduce_step(lone, true)});
    }
  }

  // The entries the call sites of nonterminal `nt` add, each once, ascending:
  // a site is told by the place of its entry.
  const std::vector<std::uint32_t>& sites_of(std::uint32_t nt) {
    const auto [it, fresh] = _sites.try_emplace(nt);
    if (fresh) {
      for (const PackedGoto& site : _p.gotos[nt]) it->second.push_back(site.entry);
      std::ranges::sort(it->second);
      it->second.erase(std::unique(it->second.begin(), it->second.end()), it->second.end());
      if (it->second.size() >= GotoCell::kRereads)
        throw Refused("a rule is called from too many kinds of places");
    }
    return it->second;
  }

  // Rows of ascending columns laid over one array, no two on one slot: per
  // row its offset, and in `width` the slots used. A row of no column stands
  // past the last slot.
  static std::vector<std::uint32_t> comb(const std::vector<std::vector<std::uint32_t>>& rows,
                                         std::size_t& width) {
    std::vector<std::uint32_t> order;
    for (std::uint32_t r = 0; r < rows.size(); ++r)
      if (!rows[r].empty()) order.push_back(r);
    std::ranges::stable_sort(order, std::greater{}, [&](std::uint32_t r) { return rows[r].size(); });
    // The slots used, a bit each.
    std::vector<std::uint64_t> used;
    const auto test = [](const std::vector<std::uint64_t>& bits, std::size_t at) {
      return at / 64 < bits.size() && ((bits[at / 64] >> (at % 64)) & 1) != 0;
    };
    const auto set = [](std::vector<std::uint64_t>& bits, std::size_t at) {
      if (at / 64 >= bits.size()) bits.resize(at / 64 + 1);
      bits[at / 64] |= std::uint64_t{1} << (at % 64);
    };
    // The 64 slots from `at` on.
    const auto window = [&](std::size_t at) {
      const std::size_t word = at / 64, shift = at % 64;
      std::uint64_t bits = word < used.size() ? used[word] >> shift : 0;
      if (shift != 0 && word + 1 < used.size()) bits |= used[word + 1] << (64 - shift);
      return bits;
    };
    // Rows of the same columns go where the last of them could not: slots
    // are only ever taken.
    std::map<std::vector<std::uint32_t>, std::size_t> next;
    std::vector<std::uint32_t> base(rows.size(), 0);
    std::vector<std::uint64_t> mask;
    std::size_t lowest = 0;
    width = 0;
    for (const std::uint32_t r : order) {
      const std::vector<std::uint32_t>& columns = rows[r];
      const std::uint32_t first = columns.front();
      mask.assign((columns.back() - first) / 64 + 1, 0);
      for (const std::uint32_t c : columns) mask[(c - first) / 64] |= std::uint64_t{1} << ((c - first) % 64);
      std::size_t& from = next[columns];
      std::size_t at = std::max(from, lowest > first ? lowest - first : 0);
      const auto fits = [&] {
        for (std::size_t w = 0; w < mask.size(); ++w)
          if ((window(at + first + 64 * w) & mask[w]) != 0) return false;
        return true;
      };
      while (!fits()) ++at;
      for (const std::uint32_t c : columns) set(used, at + c);
      from = at + 1;
      base[r] = static_cast<std::uint32_t>(at);
      width = std::max(width, at + columns.back() + 1);
      while (test(used, lowest)) ++lowest;
    }
    for (std::uint32_t r = 0; r < rows.size(); ++r)
      if (rows[r].empty()) base[r] = static_cast<std::uint32_t>(width);
    return base;
  }

  // The cells, loose ones first, then the rows laid over a comb, then the
  // arms. A row holds its `otherwise` at its base and the cell of terminal
  // `t` after `t` more; rows that act alike share their slots.
  void pack() {
    const std::size_t nterms = _cfg.terminals.size();
    std::map<std::vector<std::array<std::uint32_t, 3>>, std::uint32_t> known;
    std::vector<std::uint32_t> same(_rows.size());
    std::vector<std::vector<std::uint32_t>> columns(_rows.size());
    for (std::uint32_t r = 0; r < _rows.size(); ++r) {
      std::vector<std::array<std::uint32_t, 3>> key{{_rows[r].otherwise, 0, 0}};
      for (const Cell& cell : _rows[r].cells)
        key.push_back({cell.value, std::uint32_t{cell.terminal} << 16 | cell.step, cell.wide()});
      same[r] = known.try_emplace(std::move(key), r).first->second;
      if (same[r] != r) continue;
      columns[r].push_back(0);
      for (const Cell& cell : _rows[r].cells) columns[r].push_back(cell.terminal + 1u);
    }
    std::size_t width = 0;
    const std::vector<std::uint32_t> bases = comb(columns, width);
    _cells = _loose;
    const std::size_t rowsAt = _cells.size();
    _cells.resize(rowsAt + width + nterms + 2);
    for (std::uint32_t r = 0; r < _rows.size(); ++r) {
      _bases.push_back(static_cast<std::uint32_t>(rowsAt + bases[same[r]]));
      if (same[r] != r) continue;
      _cells[_bases[r]] = _loose[_rows[r].otherwise];
      for (const Cell& cell : _rows[r].cells) _cells[_bases[r] + 1 + cell.terminal] = cell;
    }
    _armsAt = _cells.size();
    _cells.insert(_cells.end(), _arms.begin(), _arms.end());
  }

  // A tie check as the code and the tables write it.
  static std::string tie_check(const TieCheck& check) {
    static constexpr std::string_view kKinds[] = {"row", "alike", "grouped", "ranked"};
    return engine("TieCheck{") + std::to_string(check.first) + "," +
           std::to_string(check.count) + "," + std::to_string(check.rereads) + "," +
           std::to_string(check.test) + "," + engine("TieCheck::") +
           std::string(kKinds[check.kind]) + "," + std::to_string(check.n) + "}";
  }

  // A cell of three fields as the word a table holds.
  static std::uint64_t cell_word(std::uint32_t low, std::uint16_t mid, std::uint16_t high) {
    return low | std::uint64_t{mid} << 32 | std::uint64_t{high} << 48;
  }

  // The tables of the steps.
  void transitions() {
    const std::size_t nterms = _cfg.terminals.size();
    std::vector<std::uint64_t> words;
    std::vector<std::uint32_t> operands;
    for (const Cell& cell : _cells) {
      words.push_back(cell_word(cell.value, cell.terminal, cell.step));
      operands.push_back(cell.wide());
    }
    table("::std::uint64_t", "kLrCells", words, 12);
    table("::std::uint32_t", "kLrOperands", operands);
    std::vector<std::string> values;
    table("::std::uint32_t", "kLrRows", _bases);
    table("::std::uint16_t", "kLrEnters", _enters);

    // Per state, where each nonterminal reduced over it leads.
    std::vector<std::vector<std::pair<std::uint32_t, std::array<std::uint64_t, 2>>>> leads(_p.states.size());
    for (std::uint32_t nt = 0; nt < _p.gotos.size(); ++nt) {
      const std::vector<std::uint32_t>& sites = sites_of(nt);
      for (const PackedGoto& site : _p.gotos[nt]) {
        const auto id = static_cast<std::uint16_t>(std::ranges::lower_bound(sites, site.entry) - sites.begin());
        const Turn turn = turn_at(site.from, site.target);
        const std::uint32_t to = turn.list != kNone ? turn.list : site.target;
        const std::uint64_t tail = turn.list != kNone ? site.target << 8 | (turn.drops + 1) : 0;
        leads[site.from].emplace_back(
            nt, std::array<std::uint64_t, 2>{
                    cell_word(to, _enters[to],
                              id | (_p.states[to].rereads() ? GotoCell::kRereads : 0)),
                    _bases[to] | tail << 32});
      }
    }
    std::map<std::vector<std::pair<std::uint32_t, std::array<std::uint64_t, 2>>>, std::uint32_t> led;
    std::vector<std::uint32_t> like(leads.size());
    std::vector<std::vector<std::uint32_t>> columns(leads.size());
    std::size_t width = 0;
    for (std::uint32_t s = 0; s < leads.size(); ++s) {
      std::ranges::sort(leads[s]);
      like[s] = led.try_emplace(leads[s], s).first->second;
      if (like[s] == s)
        for (const auto& [nt, cell] : leads[s]) columns[s].push_back(nt);
    }
    const std::vector<std::uint32_t> gotoBases = comb(columns, width);
    words.assign(2 * std::max<std::size_t>(width, 1), 0);
    for (std::uint32_t s = 0; s < leads.size(); ++s)
      if (like[s] == s)
        for (const auto& [nt, cell] : leads[s])
          std::ranges::copy(cell, words.begin() + 2 * (gotoBases[s] + nt));
    table("::std::uint64_t", "kLrGotos", words, 12);
    std::vector<std::uint32_t> gotoBase;
    for (std::uint32_t s = 0; s < leads.size(); ++s) gotoBase.push_back(gotoBases[like[s]]);
    table("::std::uint32_t", "kLrGotoBase", gotoBase);

    values.clear();
    for (const TieCheck& check : _ties) values.push_back(tie_check(check));
    table(engine("TieCheck"), "kLrTieChecks", values, 4, "{}");
    // Per terminal, how an arm reads a token as it.
    std::map<std::uint16_t, std::vector<std::uint32_t>> tiesOf;
    for (std::uint32_t tie = 0; tie < _p.ties.size(); ++tie)
      for (const std::uint16_t terminal : _p.ties[tie].terminals)
        if (_matchTied.contains(terminal)) tiesOf[terminal].push_back(tie);
    values.clear();
    for (std::uint16_t terminal = 0; terminal < nterms; ++terminal) {
      const std::size_t at = _words.size();
      if (const auto of = tiesOf.find(terminal); of != tiesOf.end())
        for (const std::uint32_t tie : of->second) {
          const Packed::Tie& tied = _p.ties[tie];
          if (tied.row == kNone || tied.terminals.front() == terminal) continue;
          if (tied.row > 0xFFFF) throw Refused("the grammar has too many ties of terminals");
          _words.push_back(tied.terminals.front());
          _words.push_back(static_cast<std::uint16_t>(tied.row));
        }
      values.push_back("{" + std::to_string(_p.solo[terminal]) + "," +
                       (_matchTied.contains(terminal) ? "true," : "false,") +
                       (_p.mixed[terminal] ? "true," : "false,") +
                       std::to_string((_words.size() - at) / 2) + "," + std::to_string(at) + "}");
    }
    table(engine("TiedTerminal"), "kLrTied", values, 6);
    table("::std::uint16_t", "kLrWords", _words);
    values.clear();
    for (const ReadAgain& again : _agains)
      values.push_back("{" + std::to_string(again.state) + "," + std::to_string(again.row) + "," +
                       std::to_string(again.tested) + "," + std::to_string(again.n) + "," +
                       std::to_string(again.mode) + "}");
    table(engine("ReadAgain"), "kLrAgains", values, 6, "{}");

    // Per step, the production it reduces. Its record at each call site,
    // with the number of the fuse under it.
    std::vector<std::vector<std::array<std::uint32_t, 2>>> closes(_stepReduces.size());
    for (std::size_t step = 0; step < _stepReduces.size(); ++step) {
      const std::uint32_t production = _stepReduces[step];
      if (production == kNone) continue;
      const Production& prod = _cfg.productions[production];
      for (const std::uint32_t site : sites_of(prod.lhs)) {
        const std::uint32_t entry = _p.closed(prod.entry, site);
        const bool lone = entry != kNone && (_p.shapes[production] & kRecordLone) != 0;
        closes[step].push_back({entry == kNone ? std::uint32_t{kNoElement} : _p.element(entry),
                                lone ? std::uint32_t{fuse_of(entry)} : 0});
      }
      for (const std::uint32_t take : prod.takes)
        if (take != kNone) fuse_of(take);
    }
    // The fuses by number, laid over a comb: where the row of each begins,
    // and at an inner element's place in it the fused one.
    std::vector<std::vector<std::uint32_t>> inners(_fuses.size() + 1);
    for (const auto& [outer, number] : _fuses) {
      const auto begin = _p.fused.lower_bound({outer, 0});
      const auto end = _p.fused.upper_bound({outer, kNone});
      for (auto it = begin; it != end; ++it) inners[number].push_back(_p.element(it->first.second));
      std::ranges::sort(inners[number]);
    }
    std::size_t slots = 0;
    const std::vector<std::uint32_t> fuseAt = comb(inners, slots);
    std::vector<std::uint32_t> map(std::max<std::size_t>(slots, 1));
    for (const auto& [outer, number] : _fuses) {
      const auto begin = _p.fused.lower_bound({outer, 0});
      const auto end = _p.fused.upper_bound({outer, kNone});
      for (auto it = begin; it != end; ++it)
        map[fuseAt[number] + _p.element(it->first.second)] = _p.element(it->second);
    }
    // The records, one per call site where they differ, each run written
    // once: a record, then the row of its fuse.
    std::map<std::vector<std::uint64_t>, std::uint32_t> runs;
    std::vector<std::uint64_t> records;
    values.clear();
    for (std::size_t step = 0; step < _stepReduces.size(); ++step) {
      const std::uint32_t production = _stepReduces[step];
      if (production == kNone) {
        values.emplace_back("{}");
        continue;
      }
      const Production& prod = _cfg.productions[production];
      std::vector<std::uint64_t> run;
      for (const auto& [element, fuse] : closes[step])
        run.push_back(element | std::uint64_t{fuse != 0 ? fuseAt[fuse] : 0} << 32);
      const bool bySite = std::ranges::adjacent_find(run, std::not_equal_to{}) != run.end();
      if (!bySite) run.assign(1, run.empty() ? std::uint64_t{kNoElement} : run.front());
      std::uint64_t record = run.front();
      if (bySite) {
        const auto [at, fresh] = runs.try_emplace(run, static_cast<std::uint32_t>(records.size()));
        if (fresh) records.insert(records.end(), run.begin(), run.end());
        record = at->second;
      }
      // Where every site leads to one state, its goto, and the cell a token in
      // hand takes there where that state only reduces.
      const std::vector<PackedGoto>& gotos = _p.gotos[prod.lhs];
      const bool one = !gotos.empty() && _single[prod.lhs];
      const std::uint32_t target = one ? gotos.front().target : 0;
      const Turn turn = _ends[prod.lhs].all;
      const bool told = turn.list == kSomeList;
      const std::uint32_t to = turn.list != kNone && !told ? turn.list : target;
      const std::uint32_t next =
          one && _p.states[target].reduce != kNone ? _rows[target].otherwise : 0;
      // The records its symbols give an entry to.
      std::string takes;
      std::size_t taken = 0;
      for (std::size_t at = 0; at < prod.takes.size(); ++at)
        if (prod.takes[at] != kNone) {
          takes += (taken++ == 0 ? "{" : ",{") + std::to_string(at) + "," +
                   std::to_string(fuseAt[fuse_of(prod.takes[at])]) + "}";
        }
      values.push_back(
          "{" + std::to_string(production) + "," + std::to_string(prod.lhs) + "," +
          std::to_string(static_cast<std::uint32_t>(record)) + "," + std::to_string(record >> 32) +
          "," + std::to_string(next) + ",{" + (told ? engine("kNoStack") : std::to_string(to)) +
          "," + std::to_string(one && !told ? _enters[to] : 0) + "," +
          std::to_string(one && !told && _p.states[to].rereads() ? GotoCell::kRereads : 0) + "," +
          std::to_string(one && !told ? _bases[to] : 0) +
          (turn.list != kNone ? "," + std::to_string(target) + "," + std::to_string(turn.drops + 1)
                              : std::string()) +
          "}," + std::to_string(prod.rhs.size()) + "," +
          std::to_string(next != 0 ? _cells[next].step : 0) + (bySite ? ",true" : ",false") +
          (taken != 0 ? "," + std::to_string(taken) + ",{" + takes + "}" : std::string()) + "}");
    }
    table(engine("Reduction"), "kLrReductions", values, 4, "{}");
    table("::std::uint64_t", "kLrRecords", records);
    _reductions = std::move(values);
    table("::std::uint32_t", "kFuseMap", map);
  }

  // Whether `p` holds before tokens it names: the token read among those
  // names is let go once it holds, and the state reached reads it again.
  static bool names(const Production& p) {
    return std::ranges::any_of(p.tests, [](const Test& test) {
      return !test.unless &&
             std::ranges::any_of(test.ahead, [](std::uint16_t t) { return t != kEndOfInput; });
    });
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
          const std::string alphabet = std::to_string(test.alphabet);
        in += " || ((" + shadow + ") && [&] { const Token a = m.observed(" + m + ".t.modes[" +
                alphabet + "].read(" + m + ", p, " + alphabet + ")); return (" + read +
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

  // Whether `production` has tests a token in hand must hold: function
  // `holds` of its number, written once.
  bool tested(std::uint32_t production) {
    const auto [at, fresh] = _holds.try_emplace(production);
    if (fresh) at->second = holds_of(_cfg.productions[production]);
    return !at->second.empty();
  }

  // Where some arms of a cell reduce productions with tests, those whose
  // tests fail are left out: the one arm left is taken.
  void guard(std::uint32_t cell, std::span<const PackedAction> arms) {
    if (arms.size() > 32) return;
    std::string tests;
    for (std::size_t a = 0; a < arms.size(); ++a)
      if (arms[a].kind == PackedAction::Kind::reduce && tested(arms[a].value))
        tests += "      if (!holds" + std::to_string(arms[a].value) + "(m, p, tok)) alive &= ~(1u << " +
                 std::to_string(a) + ");\n";
    if (tests.empty()) return;
    _guards["      {\n"
            "      [[maybe_unused]] const Token tok = m.tok;\n"
            "      [[maybe_unused]] const char* const p = m.at;\n"
            "      ::std::uint32_t alive = " +
            std::to_string((std::uint64_t{1} << arms.size()) - 1) + "u;\n" + tests +
            "      if (::std::has_single_bit(alive)) "
            "return {m.arms + ::std::countr_zero(alive)};\n"
            "      }\n"]
        .push_back(cell);
  }

  // Whether a mode reads in a section.
  bool closes() const {
    return std::ranges::any_of(_p.modeClose, [](std::uint16_t t) { return t != kNoToken; });
  }

  // Whether the drive predicts any cell's arm.
  bool predicts() const { return !_prefixes.cells.empty() || !_guards.empty(); }

  // The proved prefixes as tables, and the guards of the cells as code.
  void prefixes() {
    if (!predicts()) return;
    const auto plan_of = [](std::uint32_t plan) {
      return plan == kNone ? engine("kNoPlan") : std::to_string(plan);
    };
    // The rows of shifts one after another, and the arms of the plans.
    std::vector<const std::vector<std::uint8_t>*> rows(_prefixes.shifts.size());
    for (const auto& [row, number] : _prefixes.shifts) rows[number] = &row;
    std::vector<std::size_t> rowAt;
    std::vector<std::uint8_t> shifts;
    for (const auto* row : rows) {
      rowAt.push_back(shifts.size());
      shifts.insert(shifts.end(), row->begin(), row->end());
    }
    table("::std::uint8_t", "kPrefixShifts", shifts);
    std::vector<const std::vector<std::array<std::uint32_t, 3>>*> plans(_prefixes.plans.size());
    for (const auto& [arms, number] : _prefixes.plans) plans[number] = &arms;
    std::vector<std::uint32_t> planAt{0};
    std::vector<std::string> values;
    for (const auto* arms : plans) {
      for (const auto& arm : *arms)
        values.push_back("{" + std::to_string(arm[0]) + ", kPrefixShifts + " +
                         std::to_string(rowAt[arm[1]]) + ", " + std::to_string(arm[2]) + "}");
      planAt.push_back(static_cast<std::uint32_t>(values.size()));
    }
    table(engine("PrefixArm"), "kPrefixArms", values, 4, "{}");
    table("::std::uint32_t", "kPrefixPlanAt", planAt);
    for (const auto& [stack, number] : _prefixes.stacks)
      array("::std::uint32_t", "kStack" + std::to_string(number), stack);
    std::vector<std::string> routes(_prefixes.paths.size());
    for (const auto& [path, number] : _prefixes.paths) {
      values.clear();
      for (const auto& [mode, terminal] : path.tokens)
        values.push_back("{" + std::to_string(mode) + ", " + std::to_string(terminal) + "}");
      const auto name = std::to_string(number);
      array("::std::array<::std::uint16_t, 2>", "kThrough" + name, values);
      line("constexpr PrefixPath kPath", name, "{kStack", path.stack, ", kThrough", name, "};");
      routes[number] = "{" + std::to_string(path.plan) + ", &kPath" + name + "}";
    }
    array("PrefixRoute", "kPrefixRoutes", routes, 8, "{}");
    // Cells decided alike share their way.
    using Way = std::tuple<std::uint32_t, std::uint32_t, std::map<std::uint32_t, std::uint32_t>>;
    std::map<std::uint32_t, Way> wayAt;
    for (const auto& [cell, decision] : _prefixes.cells)
      wayAt[cell.first] = {cell.second, decision.fallback, decision.contexts};
    const std::uint32_t first = wayAt.empty() ? 0 : wayAt.begin()->first;
    std::vector<std::uint32_t> wayOf(wayAt.empty() ? 0 : wayAt.rbegin()->first - first + 1);
    std::map<Way, std::uint32_t> known;
    std::vector<std::uint32_t> states;
    std::vector<std::string> contexts;
    values.clear();
    for (const auto& [cell, way] : wayAt) {
      const auto [at, fresh] = known.try_emplace(way, static_cast<std::uint32_t>(known.size() + 1));
      wayOf[cell - first] = at->second;
      if (!fresh) continue;
      const auto& [depth, fallback, byState] = way;
      values.push_back("{" + std::to_string(depth) + ", " + plan_of(fallback) + ", " +
                       std::to_string(contexts.size()) + ", " + std::to_string(byState.size()) + "}");
      for (const auto& [state, plan] : byState) {
        states.push_back(state);
        contexts.push_back(plan_of(plan));
      }
    }
    table("::std::uint32_t", "kPrefixWayOf", wayOf);
    table(engine("PrefixWay"), "kPrefixWays", values, 8, "{}");
    table("::std::uint32_t", "kPrefixStates", states);
    table("::std::uint32_t", "kPrefixPlans", contexts, 24, "0");
    line("constexpr PrefixTable kPrefixes{", first, ", kPrefixWayOf, kPrefixWays, kPrefixStates, kPrefixPlans,");
    line("                                kPrefixPlanAt, kPrefixArms, kPrefixRoutes};");
    line("::cursiv::engine::Prediction predict(Machine& m) {");
    line("  if (m.ended != kEndOfInput || m.tok.end <= m.at ||");
    line("      m.tok.end <= m.src + m.tok.begin(m)) return {};");
    if (!_guards.empty()) {
      line("  if (!m.tok.hasGroup()) switch (m.arms) {");
      for (const auto& [guarded, cells] : _guards) {
        line(cases(cells, "    "));
        _out += guarded;
        line("      break;");
      }
      line("    default: break;");
      line("  }");
    }
    line("  return predict_cell(m, kPrefixes);");
    line("}");
    line("");
  }

  const Cfg& _cfg;
  const Packed& _p;
  PrefixPredictions _prefixes;
  // The code that leaves out the arms whose tests fail, and the cells it
  // guards, ascending: cells guarded alike share it.
  std::map<std::string, std::vector<std::uint32_t>> _guards;
  // Per production asked, its tests as function `holds` writes them: empty
  // for none.
  std::map<std::uint32_t, std::string> _holds;
  std::string _out;
  // The plain tables, defined in a unit of their own, and their declarations.
  std::string _tables;
  std::string _declared;
  // Per step, its reduction as the tables write it.
  std::vector<std::string> _reductions;
  // The steps asked for, each an expression of a function over the machine
  // and over a machine that steps, with the production it reduces; and the
  // reductions in the order they were first reached.
  std::vector<std::string> _steps;
  std::vector<std::string> _stepping;
  std::vector<std::uint32_t> _stepReduces;
  std::map<std::pair<std::string, std::uint32_t>, std::uint16_t> _stepOf;
  std::set<std::pair<std::uint32_t, bool>> _reduced;
  std::vector<std::pair<std::uint32_t, bool>> _reduces;
  // The rows of cells, the states' first; the cells no row holds; the arms
  // of the cells with several actions, in the order of their resume points.
  struct Row {
    std::vector<Cell> cells;
    std::uint32_t otherwise = 0;
  };
  std::vector<Row> _rows;
  std::vector<Cell> _loose;
  std::map<std::tuple<std::uint32_t, std::uint16_t, std::uint16_t, std::uint16_t>, std::uint32_t>
      _looseOf;
  std::vector<Cell> _arms;
  // The cells as packed: per row its base, and where the arms begin.
  std::vector<Cell> _cells;
  std::vector<std::uint32_t> _bases;
  std::size_t _armsAt = 0;
  // Per state, the step that enters it with no token; whether the machine's
  // reads and shifts are written per state.
  std::vector<std::uint16_t> _enters;
  bool _perState = false;
  std::vector<TieCheck> _ties;
  std::vector<std::uint16_t> _words;
  std::vector<ReadAgain> _agains;
  std::map<std::uint32_t, std::vector<std::uint32_t>> _sites;
  // The terminals of the arms that read a token the matcher read alike.
  std::set<std::uint16_t> _matchTied;
  // The entries closed over a lone child, each with its number from 1.
  std::map<std::uint32_t, std::uint16_t> _fuses;
  // Per nonterminal, whether every site of it leads to one state and adds
  // one entry.
  std::vector<bool> _single;
  // Per state, the states a shift or a goto leads to it from, and the frames
  // it drops plus one where it only ends a turn of a list; the turns told so
  // far; per nonterminal, the turns its sites end: one for all, or for some.
  std::vector<std::vector<std::uint32_t>> _preds;
  std::vector<std::uint32_t> _tails;
  std::map<std::pair<std::uint32_t, std::uint32_t>, Turn> _turns;
  struct Ends {
    Turn all;
    bool some = false;
  };
  std::vector<Ends> _ends;
  // Whether the language hides trivia: a shift then writes what was read.
  bool _hides = false;
  // Whether the word being written is a function of its own under a called
  // lexer: a token it drops goes back to the lexer.
  bool _ownWord = false;
  // Whether the dispatch of a state splits a run: the readings that decide it
  // are then told what they derive.
  bool _splits = false;
  // Whether a reduction's tests may refuse the token in hand: `again` is
  // written.
  bool _refutes = false;
  // The modes a lexer is written for.
  std::vector<bool> _read;
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
