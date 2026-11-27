#include "cursiv/engine/grammar/Lower.hpp"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/engine/lexer/LexAutomaton.hpp"
#include "cursiv/utils/Descent.hpp"

namespace cursiv::engine::gen {

using detail::Descent;

namespace {

constexpr auto kUnboundedCount = static_cast<std::uint64_t>(detail::kUnbounded);

class Lowering {
 public:
  Lowering(GrammarImage& image, const LowerDesc& desc)
      : _image(image), _desc(desc) {
    if (desc.rows.size() > kActionLimit)
      throw Refused("the grammar publishes too many actions");
    for (std::size_t i = 0; i < desc.rows.size(); ++i)
      _action.emplace(desc.rows[i], static_cast<std::uint16_t>(i));
    _cfg.rows.assign(desc.rows.begin(), desc.rows.end());
  }

  Cfg run() {
    add_terminal({.name = "end of input"});
    _cfg.nonterminals = 1;
    _region = grammar_region(_image.trivia, kEndOfInput, kNone);
    brackets(_image.indentOpen, _cfg.opens);
    brackets(_image.indentBlock, _cfg.blocks);
    brackets(_image.indentClose, _cfg.closes);
    // A bracket opens, opens a block or closes: one of the three.
    std::vector<std::uint16_t> named(_cfg.opens);
    named.insert(named.end(), _cfg.blocks.begin(), _cfg.blocks.end());
    named.insert(named.end(), _cfg.closes.begin(), _cfg.closes.end());
    std::ranges::sort(named);
    if (std::ranges::adjacent_find(named) != named.end())
      throw Refused("a bracket is named by more than one of indent_open, indent_block and "
                    "indent_close");
    if (_image.indentNewline != kNoImage)
      _cfg.lines.assign(1, _cfg.newline = terminal_of(_image.indentNewline));
    inspect_rules(_image.rule(*_desc.entry));
    const Symbol entry = rule_symbol(_image.rule(*_desc.entry));
    if (is_terminal(entry)) throw Refused("the entry is a terminal");
    _cfg.productions.emplace_back().rhs = {entry};
    // Rules are lowered as they are met: a rule's productions follow those of
    // the rules met before it. A node rule's record hands back the node its
    // body built, a data rule's converts what it read.
    for (std::size_t at = 0; at < _cfg.rules.size(); ++at) {
      const auto [rule, nt, region, conditional] = _cfg.rules[at];
      _region = region;
      _conditional = conditional;
      _rule = rule;
      const ImageStageDesc& stage = _image.stage(rule);
      if (stage.kind == ImageRuleKind::infix) {
        infix(rule, nt, stage);
        continue;
      }
      const void* const closing =
          stage.kind == ImageRuleKind::node ? stage.closeRow : stage.convertRow;
      // No action is published for a data rule only conditions read: its
      // record writes nothing.
      const auto action = _action.find(closing);
      const bool silent = stage.kind == ImageRuleKind::data && action == _action.end();
      if (stage.kind == ImageRuleKind::data && !silent)
        _cfg.dataRules.emplace_back(action->second, nt);
      detail::descend(alternatives(body_of(rule), nt, silent ? kNone : entry_of(closing)));
    }
    // A section's text ends at its close, which its grammar reads nowhere, and
    // no line of it is measured as the document's.
    if (!_sections.empty()) {
      if (_cfg.indent != 0 || _cfg.dedent != 0)
        throw Refused("the grammar uses indentation and embeds a grammar with `section<>`");
      for (const Production& p : _cfg.productions)
        if (const std::uint16_t end = _cfg.regions[p.region].end;
            end != kEndOfInput && !std::ranges::binary_search(_sections, p.lhs) &&
            std::ranges::find(p.rhs, Symbol{end}) != p.rhs.end())
          throw Refused("the grammar closes a `section<>` on a token that the embedded grammar "
                        "also reads, or that starts one of its tokens");
    }
    _cfg.productionsOf.resize(_cfg.nonterminals);
    for (std::uint32_t p = 0; p < _cfg.productions.size(); ++p)
      _cfg.productionsOf[_cfg.productions[p].lhs].push_back(p);
    operator_copies();
    newline_trivia();
    return std::move(_cfg);
  }

 private:
  // A token that opens operators of several levels of an infix rule that
  // stand at one place, before an operand or behind one, is read as a copy
  // of its own per level, spelled the same: the token after it tells which
  // reads on. A nonterminal such levels begin with is copied the same way.
  // What levels of several places begin with keeps its tokens. A node a
  // tighter operator takes a token from past a test of the gap keeps it
  // where that test fails.
  void operator_copies() {
    if (_cfg.infixes.empty()) return;
    // The nonterminals that may read nothing.
    std::vector<bool> blank = _cfg.nullable();
    // The symbols production `p` may begin with, each with its place in it.
    const auto leading = [&](std::uint32_t p, auto&& each) {
      for (std::size_t i = 0; i < _cfg.productions[p].rhs.size(); ++i) {
        const Symbol s = _cfg.productions[p].rhs[i];
        each(i, s);
        if (is_terminal(s) || !blank[index_of(s)]) break;
      }
    };
    // Per level of every table, numbered across them: its place and the
    // nonterminal of its operators. Per place and symbol an operator may
    // begin with, the levels it begins; per nonterminal, the one level that
    // reaches it, `kSeveral` where more do.
    constexpr std::uint32_t kSeveral = kNone - 1;
    std::map<std::pair<std::uint32_t, Symbol>, std::vector<std::uint32_t>> opens;
    std::vector<std::uint32_t> work, seen(_cfg.nonterminals, kNone), reached(_cfg.nonterminals, kNone);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> levels;
    for (std::uint32_t x = 0; x < _cfg.infixes.size(); ++x)
      for (const Infix::Level& told : _cfg.infixes[x].levels) {
        const auto level = static_cast<std::uint32_t>(levels.size());
        const std::uint32_t place = x << 1 | (told.group == Association::Prefix ? 1u : 0u);
        levels.emplace_back(place, told.operators);
        work.assign(1, told.operators);
        seen[told.operators] = level;
        while (!work.empty()) {
          const std::uint32_t at = work.back();
          work.pop_back();
          reached[at] = reached[at] == kNone ? level : kSeveral;
          for (const std::uint32_t p : _cfg.productionsOf[at])
            leading(p, [&](std::size_t, Symbol s) {
              std::vector<std::uint32_t>& begun = opens[{place, s}];
              if (begun.empty() || begun.back() != level) begun.push_back(level);
              if (is_terminal(s) || seen[index_of(s)] == level) return;
              seen[index_of(s)] = level;
              work.push_back(index_of(s));
            });
        }
      }
    // Each level rewrites what it alone reaches, and its copies, and reads on
    // through what levels share without rewriting it. Per level and token it
    // opens with, the gap tests read just before, as a list of `tested`;
    // kNone for none, or where walks read different ones.
    struct Walk {
      std::uint32_t nt, guard;
      bool own;
    };
    std::vector<std::vector<std::uint32_t>> tested;
    std::map<std::vector<std::uint32_t>, std::uint32_t> listed;
    const auto and_then = [&](std::uint32_t guard, std::span<const std::uint32_t> more) {
      std::vector<std::uint32_t> list = guard == kNone ? std::vector<std::uint32_t>{} : tested[guard];
      for (const std::uint32_t g : more)
        if (const auto at = std::ranges::lower_bound(list, g); at == list.end() || *at != g)
          list.insert(at, g);
      const auto [known, fresh] = listed.try_emplace(list, static_cast<std::uint32_t>(tested.size()));
      if (fresh) tested.push_back(std::move(list));
      return known->second;
    };
    // Per nonterminal that reads nothing but tests of the gap, as `!gap::line`
    // lowers or a rule of such tests does, those tests as a list of `tested`,
    // where alternatives of them are one: a condition that reads them; kSeveral
    // for any other. Each is told once, after what it reads, one met again while
    // open reads more.
    constexpr std::uint32_t kUntold = kNone - 2, kOpen = kNone - 3;
    std::vector<std::uint32_t> gaps, ways, entered(_cfg.nonterminals, kNone);
    // The conditions made so, by the production that reads them.
    std::map<std::uint32_t, std::uint32_t> asks;
    const auto gaps_of = [&](std::uint32_t nt) {
      gaps.resize(_cfg.nonterminals, kUntold);
      for (std::vector<std::uint32_t> open{nt}; !open.empty();) {
        const std::uint32_t at = open.back();
        if (gaps[at] != kUntold && gaps[at] != kOpen) {
          open.pop_back();
          continue;
        }
        gaps[at] = kOpen;
        const std::size_t waits = open.size();
        ways.clear();
        for (const std::uint32_t p : _cfg.productionsOf[at]) {
          const Production& q = _cfg.productions[p];
          std::uint32_t list = kSeveral;
          if (q.rhs.empty() && q.tests.size() == 1 && q.tests.front().ahead.empty() &&
              q.tests.front().condition == 0 && q.tests.front().gap != Test::Gap::any) {
            list = and_then(kNone, std::span(&p, 1));
          } else if (!q.rhs.empty() && q.tests.empty() &&
                     std::ranges::all_of(q.rhs, [&](Symbol s) {
                       return !is_terminal(s) && blank[index_of(s)];
                     })) {
            list = kNone;
            for (const Symbol s : q.rhs) {
              const std::uint32_t read = gaps[index_of(s)];
              if (read == kUntold) open.push_back(index_of(s));
              else if (list != kSeveral) list = read >= kOpen ? kSeveral : and_then(list, tested[read]);
            }
          }
          ways.push_back(list);
        }
        if (open.size() != waits) continue;
        std::uint32_t list = ways.size() == 1 ? ways.front() : kSeveral;
        if (ways.size() > 1 && std::ranges::none_of(ways, [&](std::uint32_t w) { return w == kSeveral; })) {
          // The copy that ends the operand where none holds negates it whole.
          const auto read = static_cast<std::uint32_t>(_cfg.productions.size());
          Production asked;
          asked.lhs = _cfg.nonterminals++;
          asked.rule = _cfg.productions[_cfg.productionsOf[at].front()].rule;
          asked.region = _cfg.productions[_cfg.productionsOf[at].front()].region;
          asked.rhs = {nonterminal(at)};
          asked.start = true;
          _cfg.productions.push_back(std::move(asked));
          _cfg.productionsOf.emplace_back(1, read);
          _cfg.conditions.push_back(read);
          blank.push_back(false);
          seen.push_back(kNone);
          entered.push_back(kNone);
          asks.emplace(read, static_cast<std::uint32_t>(_cfg.conditions.size()));
          list = and_then(kNone, std::span(&read, 1));
        }
        gaps[at] = list;
        open.pop_back();
      }
      return gaps[nt];
    };
    std::vector<Walk> walk;
    std::map<std::pair<Symbol, std::uint32_t>, Symbol> copies;
    std::map<std::pair<std::uint32_t, Symbol>, std::uint32_t> guards;
    std::ranges::fill(seen, kNone);
    for (std::uint32_t level = 0; level < levels.size(); ++level) {
      const std::uint32_t place = levels[level].first;
      walk.assign(1, {levels[level].second, kNone, true});
      seen[levels[level].second] = level;
      entered[levels[level].second] = kNone;
      while (!walk.empty()) {
        const Walk from = walk.back();
        walk.pop_back();
        for (std::size_t k = 0; k < _cfg.productionsOf[from.nt].size(); ++k) {
          const std::uint32_t p = _cfg.productionsOf[from.nt][k];
          std::uint32_t guard = from.guard;
          leading(p, [&](std::size_t i, Symbol s) {
            if (const std::uint32_t g = is_terminal(s) ? kSeveral : gaps_of(index_of(s)); g != kSeveral) {
              guard = and_then(guard, tested[g]);
              return;
            }
            Symbol mine = s;
            const auto begun = opens.find({place, s});
            const bool copied =
                from.own && begun != opens.end() && begun->second.size() > 1 &&
                (is_terminal(s) ||
                 std::ranges::any_of(_cfg.productionsOf[index_of(s)], [&](std::uint32_t q) {
                   return !_cfg.productions[q].rhs.empty();
                 }));
            if (copied) {
              const auto [copy, made] = copies.try_emplace({s, level});
              if (made) {
                copy->second = is_terminal(s) ? Symbol{add_terminal(_cfg.terminals[s])}
                                              : nonterminal(copy_of(index_of(s)));
                if (!is_terminal(s)) {
                  blank.push_back(blank[index_of(s)]);
                  seen.push_back(kNone);
                  entered.push_back(kNone);
                } else {
                  // A copy of a bracket or of the line's terminal holds its role too:
                  // the newest terminal, it ends each list.
                  for (auto* named : {&_cfg.opens, &_cfg.blocks, &_cfg.closes, &_cfg.lines})
                    if (std::ranges::binary_search(*named, s))
                      named->push_back(static_cast<std::uint16_t>(copy->second));
                }
              }
              mine = copy->second;
              _cfg.productions[p].rhs[i] = mine;
            }
            if (is_terminal(mine)) {
              const auto [known, fresh] = guards.try_emplace({level, mine}, guard);
              if (!fresh && known->second != guard) known->second = kNone;
              return;
            }
            const std::uint32_t n = index_of(mine);
            const bool own = from.own && (copied || n >= reached.size() || reached[n] != kSeveral);
            if (seen[n] != level) {
              seen[n] = level;
              entered[n] = guard;
              walk.push_back({n, guard, own});
            } else if (entered[n] != guard && entered[n] != kNone) {
              entered[n] = kNone;
              walk.push_back({n, kNone, own});
            }
            guard = kNone;
          });
        }
      }
    }
    // Per table and token behind an operand, its levels and the gap tests
    // read before it at all of them. A node they take it from is reduced on
    // it by a copy that holds where one of those tests fails.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> at;
    for (std::uint32_t x = 0; x < _cfg.infixes.size(); ++x)
      for (std::uint32_t l = 0; l < _cfg.infixes[x].levels.size(); ++l) at.emplace_back(x, l);
    std::map<std::pair<std::uint32_t, Symbol>, std::pair<std::vector<std::uint32_t>, std::uint32_t>> read;
    for (const auto& [opened, guard] : guards) {
      const auto [level, t] = opened;
      if ((levels[level].first & 1) != 0) continue;
      const auto [entry, fresh] = read.try_emplace({at[level].first, t});
      entry->second.first.push_back(at[level].second);
      if (fresh) entry->second.second = guard;
      else if (entry->second.second != guard) entry->second.second = kNone;
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> past;
    for (const auto& [where, opening] : read) {
      const auto& [ms, guard] = opening;
      if (guard == kNone) continue;
      Infix& table = _cfg.infixes[where.first];
      for (std::uint32_t l = 0; l < table.levels.size(); ++l) {
        const auto [before, coming] = table.order(l, ms);
        if (before || !coming) continue;
        const auto [copy, made] = past.try_emplace({table.levels[l].node, guard});
        if (made) {
          Production node = _cfg.productions[table.levels[l].node];
          node.tests.clear();
          for (const std::uint32_t g : tested[guard]) {
            const auto asked = asks.find(g);
            Test& fails = node.tests.emplace_back(asked == asks.end() ? _cfg.productions[g].tests.front() : Test{});
            if (asked != asks.end()) fails.condition = asked->second;
            if (fails.condition != 0) fails.unless = !fails.unless;
            else fails.gapUnless = !fails.gapUnless;
          }
          node.copyOf = table.levels[l].node;
          copy->second = static_cast<std::uint32_t>(_cfg.productions.size());
          _cfg.productions.push_back(std::move(node));
        }
        table.levels[l].past.emplace_back(static_cast<std::uint16_t>(where.second), copy->second);
      }
    }
    for (Infix& table : _cfg.infixes)
      for (Infix::Level& level : table.levels) std::ranges::sort(level.past);
  }

  // A nonterminal that reads what `nt` reads: its productions over again.
  std::uint32_t copy_of(std::uint32_t nt) {
    const std::uint32_t made = _cfg.nonterminals++;
    _cfg.productionsOf.emplace_back();
    for (std::size_t k = 0; k < _cfg.productionsOf[nt].size(); ++k) {
      Production copy = _cfg.productions[_cfg.productionsOf[nt][k]];
      copy.lhs = made;
      _cfg.productionsOf[made].push_back(static_cast<std::uint32_t>(_cfg.productions.size()));
      _cfg.productions.push_back(std::move(copy));
    }
    return made;
  }

  std::uint16_t add_terminal(Terminal terminal) {
    const auto id = _cfg.terminals.size();
    if (id + 1 >= kWinNone) throw Refused("the grammar has too many terminals");
    _cfg.terminals.push_back(std::move(terminal));
    return static_cast<std::uint16_t>(id);
  }

  // One alternative being written: its symbols, and per symbol the scope of
  // the gap before it, the scope written in where none is told.
  struct Alternative {
    std::vector<Symbol> symbols;
    std::vector<std::uint16_t> scopes;
    std::uint16_t opening = kNoToken;
    Alternative() = default;
    Alternative(std::initializer_list<Symbol> list) : symbols(list) {}
    [[nodiscard]] std::size_t size() const { return symbols.size(); }
    [[nodiscard]] Symbol back() const { return symbols.back(); }
  };

  // Appends `s` to `out`, after a gap of the scope written in; the first
  // symbol of a scope written where it stands after a gap of its caller's.
  void put(Alternative& out, Symbol s) {
    if (out.size() >= kProductionLimit)
      throw Refused("a rule repeats what it reads too many times to be written out");
    if (_conditional) s = unread(s);
    const auto opening = std::exchange(out.opening, kNoToken);
    const auto gap = opening == kNoToken ? _region : opening;
    if (!out.scopes.empty() || gap != _region) {
      out.scopes.resize(out.symbols.size(), gap);
      out.scopes.push_back(gap);
    }
    out.symbols.push_back(s);
  }

  // A token a condition names: in a section, its close, or a literal spelled
  // alike, stands for a token no text reads, the text ending there.
  Symbol unread(Symbol s) {
    const std::uint16_t end = _cfg.regions[_region].end;
    if (end == kEndOfInput || !is_terminal(s) ||
        (s != end && (!_cfg.terminals[s].literal() || _cfg.terminals[s].text != _cfg.terminals[end].text)))
      return s;
    if (_cfg.unread == 0) {
      _cfg.unread = add_terminal({.name = "past the section"});
    }
    return _cfg.unread;
  }

  [[noreturn]] void unsupported(std::string_view what) const {
    throw Refused("unsupported grammar expression: " + std::string(what));
  }

  // Collect documented spellings and flag operands from the reachable rules.
  void inspect_rules(std::uint32_t entry) {
    std::vector<std::uint32_t> rules;
    std::vector<bool> seen, visited;
    const auto reach = [&](std::uint32_t rule) {
      rule = _image.entry_rule(rule);
      if (rule >= seen.size()) seen.resize(rule + 1, false);
      if (seen[rule]) return;
      seen[rule] = true;
      rules.push_back(rule);
    };
    const auto document = [&](const GrammarWord& word) {
      if (word.doc.data() == nullptr) return;
      auto& group = _texts[word.text];
      group.all = GrammarWord::shared_doc(group.all, word.doc);
      auto& docs = word.folded ? group.folded : _exactDocs[word.text];
      docs = GrammarWord::shared_doc(docs, word.doc);
    };
    reach(entry);
    for (std::size_t at = 0; at < rules.size(); ++at) {
      const std::uint32_t rule = rules[at];
      const ImageStageDesc& stage = _image.stage(rule);
      if (stage.primary != nullptr) reach(_image.rule(*stage.primary));
      if (stage.kind == ImageRuleKind::terminal) {
        Terminal terminal;
        terminal.body = body_of(rule);
        const auto kind = _image.nodes[terminal.body].kind;
        if (kind == ImageKind::choice || kind == ImageKind::doc) {
          spelled(terminal);
          terminal.each_word(document);
        }
        continue;
      }
      std::vector<ImageId> open;
      if (stage.body != nullptr) open.push_back(body_of(rule));
      for (const auto& level : std::span(stage.levels, stage.levelCount))
        for (const auto& op : std::span(level.ops, level.count))
          open.push_back(_image.node(*op.op, _image.folds(rule)));
      while (!open.empty()) {
        const ImageId id = open.back();
        open.pop_back();
        if (id >= visited.size()) visited.resize(_image.nodes.size());
        if (visited[id]) continue;
        visited[id] = true;
        const ImageNode n = _image.nodes[id];
        if (n.kind == ImageKind::ref || n.kind == ImageKind::embed) reach(n.a);
        if (n.kind == ImageKind::flag) _cfg.flags.emplace_back(id, rule);
        if (n.kind == ImageKind::doc) {
          const auto& word = _image.nodes[_image.children[n.first]];
          document({word.text, (word.flags & kLitFolded) != 0, n.text});
        }
        for (std::uint32_t i = 0; i < n.count; ++i) open.push_back(_image.children[n.first + i]);
      }
    }
  }

  // Intern a record marker with no AST action.
  std::uint32_t marked(WriteKind kind, std::string_view text = {}, std::uint16_t fold = 0) {
    const auto [known, fresh] = _marks.try_emplace({kind, text, fold}, 0);
    if (fresh) {
      if (_cfg.rows.size() >= kActionLimit) throw Refused("the grammar publishes too many actions");
      _cfg.texts.push_back(std::make_unique<const RowDesc>(
          RowDesc{.kind = kind, .name = text, .fold = fold}));
      _cfg.rows.push_back(_cfg.texts.back().get());
      known->second = static_cast<std::uint16_t>(_cfg.rows.size() - 1);
    }
    return _cfg.extended(kNone, known->second);
  }

  // The entry of a record that applies the published action `row`; none for
  // no action.
  std::uint32_t entry_of(const void* row) {
    if (row == nullptr) return kNone;
    const auto at = _action.find(row);
    if (at == _action.end())
      throw Refused("a rule asks for an action its grammar does not publish");
    return _cfg.extended(kNone, at->second);
  }

  ImageId body_of(std::uint32_t rule) {
    return _image.node(*_image.stage(rule).body, _image.folds(rule));
  }

  std::uint16_t grammar_region(const ImageTrivia& trivia, std::uint16_t end, std::uint32_t grammar) {
    for (auto rule : trivia.ignored) _cfg.terminals[terminal_of(_image.rule(*rule))].trivia = Trivia::ignored;
    for (auto rule : trivia.hidden) _cfg.terminals[terminal_of(_image.rule(*rule))].trivia = Trivia::hidden;
    return region_of(trivia, end, grammar);
  }

  void produce(std::uint32_t lhs, Alternative&& rhs, std::uint32_t entry = kNone,
               std::uint32_t rank = 0) {
    Production& made = _cfg.productions.emplace_back();
    made.lhs = lhs;
    made.rule = _rule;
    made.alternative = _alternative;
    made.rhs = std::move(rhs.symbols);
    if (_unordered != kNone && !made.rhs.empty() && !is_terminal(made.rhs.front())) {
      const auto lead = index_of(made.rhs.front());
      if (lead < _prefixes.size() && _prefixes[lead] == _unordered) {
        made.prefixTurn = true;
        slot(_prefixes, lhs) = _unordered;
      }
    }
    if (!rhs.scopes.empty()) {
      rhs.scopes.resize(made.rhs.size(), _region);
      if (std::ranges::any_of(rhs.scopes, [&](std::uint16_t s) { return s != _region; }))
        made.scopes = std::move(rhs.scopes);
    }
    made.entry = entry;
    made.region = _region;
    made.rank = rank;
  }

  // The symbol a call to `rule` reads. A rule met for the first time waits
  // its turn to be lowered: no call nests another's lowering. A terminal is
  // read as a token, which the scope under way must not pass as trivia.
  Symbol rule_symbol(std::uint32_t rule) {
    // A grammar is read in its section, from its entry.
    rule = _image.entry_rule(rule);
    const ImageStageDesc& stage = _image.stage(rule);
    if (stage.kind == ImageRuleKind::terminal) {
      const std::uint16_t t = terminal_of(rule);
      const Region& scope = _cfg.regions[_region];
      const auto passed = [&](const auto& tokens, Trivia role) {
        if (tokens.empty()) return false;
        if (std::ranges::binary_search(tokens, t)) return true;
        const auto copy = _twins.find({t, role});
        return copy != _twins.end() && std::ranges::binary_search(tokens, copy->second);
      };
      if (passed(scope.ignored, Trivia::ignored) || passed(scope.hidden, Trivia::hidden))
        throw Refused("a rule reads " + _cfg.terminals[t].name +
                      ", which its trivia scope passes: it never comes as a token there");
      return twin(t, Trivia::none);
    }
    // A rule is lowered once a scope. A condition reads a copy of its own in a
    // section only, where it names the close its own way.
    const bool apart = _conditional && _cfg.regions[_region].end != kEndOfInput;
    Symbol& known = slot(apart ? scope().conditionalRules : scope().rules, rule);
    if (known == kNone) {
      const std::uint32_t nt = _cfg.nonterminals++;
      _cfg.rules.push_back({rule, nt, _region, apart});
      known = nonterminal(nt);
    }
    return known;
  }

  // The terminal of terminal rule `rule`, as the grammar passes it.
  std::uint16_t terminal_of(std::uint32_t rule) {
    const ImageStageDesc* const stage = _image.rules[rule]->stage;
    if (stage == nullptr || stage->kind != ImageRuleKind::terminal)
      throw Refused("a trivia rule is not a terminal");
    Symbol& known = slot(_scopes[0].rules, rule);
    if (known != kNone) return static_cast<std::uint16_t>(known);
    // A terminal that is indentation is made by the parse, not read.
    const ImageId body = body_of(rule);
    const ImageKind kind = _image.nodes[body].kind;
    const bool made = kind == ImageKind::indent || kind == ImageKind::dedent;
    known = made ? indentation(kind == ImageKind::indent) : add_terminal({});
    Terminal& t = _cfg.terminals[known];
    if (t.named)
      throw Refused("terminal " + std::string(_image.rules[rule]->name) +
                    " is a second indent or dedent terminal");
    if (!made) t.body = body;
    std::vector<FoldedWord> folded;
    const bool allWords = made || spelled(t, &folded);
    t.name = std::string(_image.rules[rule]->name);
    t.named = true;
    // A named terminal's leaf carries its conversion, an indentation token's
    // only where it converts. No action is published for one that only
    // conditions name: it writes no leaf.
    if (const auto action = _action.find(stage->convertRow);
        action != _action.end() && (!made || _cfg.rows[action->second]->converts != Converts::nothing))
      t.entry = entry_of(stage->convertRow);
    if (t.folded || !folded.empty()) {
      std::uint16_t fold = 1;
      if (!allWords || folded.size() != t.words.size()) {
        fold = static_cast<std::uint16_t>(_cfg.foldedSets.size() + 2);
        std::ranges::sort(folded, {}, [](const auto& w) { return GrammarWord::key(w.text); });
        _cfg.foldedSets.push_back(std::move(folded));
      }
      t.entry = _cfg.fuse(marked(WriteKind::fold, {}, fold), t.entry);
    }
    return static_cast<std::uint16_t>(known);
  }

  // The terminal of an indent, or of a dedent, made where it is new.
  std::uint16_t indentation(bool indent) {
    std::uint16_t& made = indent ? _cfg.indent : _cfg.dedent;
    if (made == 0) made = add_terminal({.name = indent ? "indent" : "dedent"});
    return made;
  }

  // The trivia `indent_newline_in` names, as every scope passes them: each
  // a terminal, trivia in one scope at least.
  void newline_trivia() {
    if (_image.indentNewlineIn == kNoImage) return;
    if (_cfg.newline == 0)
      throw Refused("indent_newline_in names trivia, but indent_newline names no line terminal");
    std::vector<std::uint16_t> named;
    for (std::vector<ImageId> open{_image.indentNewlineIn}; !open.empty();) {
      const ImageNode n = _image.nodes[open.back()];
      open.pop_back();
      if (n.kind == ImageKind::choice) {
        for (std::uint32_t i = 0; i < n.count; ++i) open.push_back(_image.children[n.first + i]);
        continue;
      }
      if (n.kind != ImageKind::ref || _image.stage(n.a).kind != ImageRuleKind::terminal)
        throw Refused("indent_newline_in names a token that is not trivia");
      named.push_back(terminal_of(n.a));
    }
    std::ranges::sort(named);
    for (std::uint16_t t = 1; t < _cfg.terminals.size(); ++t) {
      const Terminal& x = _cfg.terminals[t];
      if (x.trivia != Trivia::none &&
          (std::ranges::binary_search(named, t) || std::ranges::binary_search(named, x.twin)))
        _cfg.newlineIn.push_back(t);
    }
    // Each named token is trivia itself or through a twin some scope passes.
    for (const std::uint16_t n : named)
      if (_cfg.terminals[n].trivia == Trivia::none && !_twins.contains({n, Trivia::ignored}) &&
          !_twins.contains({n, Trivia::hidden}))
        throw Refused("indent_newline_in names a token that is not trivia");
  }

  // The tokens `id` names as brackets: each one token.
  void brackets(ImageId id, std::vector<std::uint16_t>& out) {
    if (id == kNoImage) return;
    if (!named(id, out)) throw Refused("an indentation bracket is not one token");
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
  }

  // Terminal `t` as a scope passes it with `role`: a twin of it where the
  // grammar passes it otherwise.
  std::uint16_t twin(std::uint16_t t, Trivia role) {
    if (_cfg.terminals[t].trivia == role) return t;
    const auto [at, fresh] = _twins.try_emplace({t, role});
    if (fresh) {
      Terminal copy = _cfg.terminals[t];
      copy.trivia = role;
      copy.twin = t;
      at->second = add_terminal(std::move(copy));
    }
    return at->second;
  }

  // An infix rule reads its primary, two of itself around an operator of a
  // binary level, or itself next to an operator of a prefix or postfix level:
  // the node's production gives each operand's record its entry. The states
  // settle which operator an operand belongs to, from the table. A prefix
  // operator is preferred to a primary that reads the same text.
  void infix(std::uint32_t rule, std::uint32_t nt, const ImageStageDesc& stage) {
    const bool prefixed =
        std::any_of(stage.levels, stage.levels + stage.levelCount, [](const ImageStageLevelDesc& level) {
          return level.group == Association::Prefix;
        });
    produce(nt, {rule_symbol(_image.rule(*stage.primary))}, kNone, prefixed ? 1 : 0);
    Infix table;
    for (std::uint32_t l = 0; l < stage.levelCount; ++l) {
      const ImageStageLevelDesc& level = stage.levels[l];
      Infix::Level& told = table.levels.emplace_back();
      told.group = level.group;
      told.operators = _cfg.nonterminals++;
      for (std::uint32_t k = 0; k < level.count; ++k) {
        Alternative read;
        detail::descend(emit(_image.node(*level.ops[k].op, _image.folds(rule)), read));
        produce(told.operators, std::move(read), entry_of(level.ops[k].row));
      }
      told.node = static_cast<std::uint32_t>(_cfg.productions.size());
      const Symbol self = nonterminal(nt), ops = nonterminal(told.operators);
      switch (level.group) {
        case Association::Prefix:
          produce(nt, {ops, self}, entry_of(level.climbRow));
          _cfg.productions.back().takes = {kNone, entry_of(level.operandRow)};
          break;
        case Association::Postfix:
          produce(nt, {self, ops}, entry_of(level.climbRow));
          _cfg.productions.back().takes = {entry_of(level.operandRow), kNone};
          break;
        default:
          produce(nt, {self, ops, self}, entry_of(stage.climbRow));
          _cfg.productions.back().takes = {entry_of(stage.leftRow), kNone, entry_of(stage.rightRow)};
      }
    }
    _cfg.infixes.push_back(std::move(table));
  }

  // The terminal of a token written in a rule: a literal, a class, a
  // delimited run or a scan.
  Symbol token(ImageId id) {
    Symbol& known = slot(_scopes[0].nodes, id);
    if (known == kNone) {
      known = add_terminal({});
      Terminal& t = _cfg.terminals[known];
      t.body = id;
      spelled(t);
      // The leaf of a word read in any letter case says so.
      if (t.folded) t.entry = marked(WriteKind::fold, {}, 1);
    }
    return known;
  }

  // What `t` is said to be from its body, past its comments and the terminals
  // it calls: a literal where that is one spelling.
  bool spelled(Terminal& t, std::vector<FoldedWord>* folded = nullptr) const {
    ImageId id = t.body;
    if (_image.nodes[id].kind == ImageKind::doc) {
      t.doc = _image.nodes[id].text;
      id = _image.children[_image.nodes[id].first];
    }
    id = _image.bare(id);
    const ImageNode& n = _image.nodes[id];
    if (n.kind != ImageKind::lit) {
      t.name = "a token";
      return n.kind == ImageKind::choice && words(id, t, folded);
    }
    t.name = "'" + std::string(n.text) + "'";
    t.text = n.text;
    t.folded = (n.flags & kLitFolded) != 0;
    t.keyword = (n.flags & kLitKeyword) != 0;
    t.word = word_guarded(_image, n);
    return true;
  }

  // Only an exhaustive choice publishes spellings. Its folded alternatives also
  // name the case-insensitive inputs of an ordinary terminal.
  bool words(ImageId id, Terminal& terminal, std::vector<FoldedWord>* folded) const {
    bool exhaustive = true;
    // A choice several calls reach gives its words once.
    std::set<ImageId> opened;
    std::vector<ImageId> todo{id};
    while (!todo.empty()) {
      ImageId child = todo.back();
      todo.pop_back();
      const ImageNode outer = _image.nodes[child];
      if (outer.kind == ImageKind::doc) child = _image.children[outer.first];
      child = _image.bare(child);
      const ImageNode& n = _image.nodes[child];
      if (n.kind == ImageKind::choice) {
        if (opened.insert(child).second)
          for (std::uint32_t k = n.count; k-- > 0;) todo.push_back(_image.children[n.first + k]);
        continue;
      }
      if (folded && n.kind == ImageKind::lit && (n.flags & kLitFolded)) {
        const auto guard = n.flags & kLitGuardExpr ? child :
                           word_guarded(_image, n) ? FoldedWord::kWord : FoldedWord::kPlain;
        folded->push_back({n.text, guard});
      }
      if (n.kind != ImageKind::lit || (n.flags & (kLitGuardExpr | kLitKeyword)) == kLitGuardExpr ||
          n.text.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos) {
        exhaustive = false;
        if (!folded) break;
        continue;
      }
      if (exhaustive)
        terminal.words.push_back({n.text, (n.flags & kLitFolded) != 0,
                                  outer.kind == ImageKind::doc ? outer.text : terminal.doc});
    }
    if (!exhaustive) terminal.words.clear();
    return exhaustive;
  }

  // The nonterminal kept in `symbols` for `id`, and whether this call made
  // it: its maker then writes its productions.
  std::pair<std::uint32_t, bool> auxiliary(std::vector<Symbol>& symbols, ImageId id) {
    if (const Symbol known = slot(symbols, id); known != kNone)
      return {index_of(known), false};
    const std::uint32_t nt = _cfg.nonterminals++;
    slot(symbols, id) = nonterminal(nt);
    return {nt, true};
  }

  // One production per alternative of `id`, each record carrying `closing`.
  Descent<void> alternatives(ImageId id, std::uint32_t lhs, std::uint32_t closing) {
    const ImageNode n = _image.nodes[id];
    if (n.kind != ImageKind::choice) {
      Alternative alt;
      co_await emit(id, alt);
      produce(lhs, std::move(alt), closing);
      co_return;
    }
    for (std::uint32_t i = 0; i < n.count; ++i) {
      const auto outer = std::exchange(_alternative, i + 1);
      Alternative alt;
      co_await emit(_image.children[n.first + i], alt);
      produce(lhs, std::move(alt), closing, i);
      _alternative = outer;
    }
  }

  // Appends what `id` reads to `out`. The walk keeps its calls on the heap: a
  // grammar's depth costs no native stack.
  Descent<void> emit(ImageId id, Alternative& out) {
    const ImageNode n = _image.nodes[id];
    switch (n.kind) {
      case ImageKind::eof: {
        // What is read so far stands under a nonterminal of its own, which
        // is whole only where its text ends: the input's end, or its
        // section's close.
        const std::uint32_t read = _cfg.nonterminals++;
        produce(read, std::move(out));
        _cfg.productions.back().tests.emplace_back().ahead = {_cfg.regions[_region].end};
        out = {nonterminal(read)};
        co_return;
      }
      case ImageKind::skip:
      case ImageKind::embed: {
        const auto outer = _region;
        const ImageId body = _image.children[n.first];
        if (n.kind == ImageKind::embed) {
          const ImageNode read = _image.nodes[body];
          Alternative closing;
          co_await emit(_image.children[read.first + read.count - 1], closing);
          if (closing.size() != 1 || !is_terminal(closing.back()))
            throw Refused("a section closes on more than one token");
          _region = grammar_region(_image.rules[n.a]->section->trivia,
                                   static_cast<std::uint16_t>(closing.back()), n.a);
        } else {
          _region = region_of(*n.trivia, _cfg.regions[outer].end, _cfg.regions[outer].grammar);
          if (wraps(body)) {
            // A nest in a scope wraps the preceding symbols in the caller's scope.
            const auto opening = out.opening;
            if (outer != _region) out.scopes.resize(out.size(), outer);
            if (opening == kNoToken) out.opening = outer;
            co_await emit(body, out);
            if (out.opening != kNoToken) out.opening = opening;
            _region = outer;
            co_return;
          }
        }
        const auto [nt, made] = auxiliary(nodes(), id);
        if (made) {
          if (n.kind == ImageKind::embed) _sections.push_back(nt);
          Alternative scoped;
          co_await emit(body, scoped);
          produce(nt, std::move(scoped));
        }
        _region = outer;
        put(out, nonterminal(nt));
        co_return;
      }
      case ImageKind::unordered:
        co_await unordered(id, n, out);
        co_return;
      case ImageKind::gapHas:
      case ImageKind::gapIs:
        gap_test(id, id, false, out);
        co_return;
      case ImageKind::notOf:
      case ImageKind::andOf: {
        if (const ImageId tested = _image.children[n.first];
            _image.nodes[tested].kind == ImageKind::gapHas ||
            _image.nodes[tested].kind == ImageKind::gapIs) {
          gap_test(id, tested, n.kind == ImageKind::notOf, out);
          co_return;
        }
        // A condition reads nothing: a nonterminal of its own is whole only
        // where the token that comes is, or is not, one the condition names.
        const auto [nt, made] = auxiliary(nodes(), id);
        put(out, nonterminal(nt));
        if (!made) co_return;
        Test test = co_await condition(n);
        produce(nt, {});
        _cfg.productions.back().tests.push_back(std::move(test));
        co_return;
      }
      case ImageKind::implied: {
        // The token where it is written; where it is not, nothing, if what
        // its condition asks holds and the state can do nothing else.
        const auto [nt, made] = auxiliary(nodes(), id);
        put(out, nonterminal(nt));
        if (!made) co_return;
        Alternative read;
        co_await emit(_image.children[n.first], read);
        // A bracket changes how the lines after it read: the text spells it.
        const auto spelled = [&](std::uint16_t b, Symbol s) {
          const Terminal& x = _cfg.terminals[b];
          const Terminal& y = _cfg.terminals[s];
          return b == s || (x.literal() && x.text == y.text);
        };
        for (const Symbol s : read.symbols)
          for (const auto* named : {&_cfg.opens, &_cfg.blocks, &_cfg.closes})
            if (is_terminal(s) && std::ranges::any_of(*named, [&](std::uint16_t b) { return spelled(b, s); }))
              throw Refused("an implied token cannot be a bracket named by indent_open, "
                            "indent_block or indent_close");
        produce(nt, std::move(read));
        std::vector<Test> tests = co_await asked(_image.children[n.first + 1]);
        produce(nt, {}, kNone, 1);
        _cfg.productions.back().tests = std::move(tests);
        _cfg.productions.back().implied = true;
        co_return;
      }
      case ImageKind::epsilon:
        co_return;
      case ImageKind::indent:
      case ImageKind::dedent:
        put(out, indentation(n.kind == ImageKind::indent));
        co_return;
      case ImageKind::seq:
        for (std::uint32_t i = 0; i < n.count; ++i)
          co_await emit(_image.children[n.first + i], out);
        co_return;
      case ImageKind::choice: {
        if (wraps(id)) {
          // Each alternative reads what stands before the choice too: a nest
          // in it wraps that.
          const std::uint32_t nt = _cfg.nonterminals++;
          for (std::uint32_t i = 0; i < n.count; ++i) {
            const auto outer = std::exchange(_alternative, i + 1);
            Alternative alt = out;
            co_await emit(_image.children[n.first + i], alt);
            produce(nt, std::move(alt), kNone, i);
            _alternative = outer;
          }
          out = {nonterminal(nt)};
          co_return;
        }
        const auto [nt, made] = auxiliary(nodes(), id);
        if (made) co_await alternatives(id, nt, kNone);
        put(out, nonterminal(nt));
        co_return;
      }
      case ImageKind::repeat:
        if (wraps(id))
          co_await nesting(n, out);
        else
          co_await repeat(id, n, out);
        co_return;
      case ImageKind::nest:
        co_await built(n, out);
        co_return;
      case ImageKind::ref:
        put(out, rule_symbol(n.a));
        co_return;
      case ImageKind::doc: {
        const std::size_t before = out.size();
        const ImageId word = _image.children[n.first];
        co_await emit(word, out);
        if (out.size() != before + 1 || !is_terminal(out.back())) co_return;
        Terminal& terminal = _cfg.terminals[out.back()];
        terminal.doc = GrammarWord::shared_doc(terminal.doc, n.text);
        const auto group = _texts.find(terminal.text);
        if (_conditional || group == _texts.end()) co_return;
        const auto docs = terminal.folded ? group->second.all
            : GrammarWord::shared_doc(_exactDocs[terminal.text], group->second.folded);
        if (!docs.empty()) co_return;
        const auto [nt, made] = auxiliary(nodes(), id);
        if (made) produce(nt, {out.back()}, marked(WriteKind::doc, n.text));
        out.symbols.back() = nonterminal(nt);
        co_return;
      }
      case ImageKind::trigger:
        // A commit's prefix, where its exit tests it, is read as it stands.
        co_await emit(_image.children[n.first], out);
        co_return;
      case ImageKind::lit:
      case ImageKind::cpset:
      case ImageKind::byte:
      case ImageKind::through:
      case ImageKind::scan:
        put(out, token(id));
        co_return;
      case ImageKind::create:
      case ImageKind::flag:
        if (!wraps(id)) {
          co_await over(id, _image.children[n.first], entry_of(n.row), out);
        } else if (n.kind == ImageKind::create) {
          co_await built(n, out);
        } else {
          // A nest in the flag wraps what stands before it, and the flag marks
          // the node the nest makes: its record reads both.
          co_await emit(_image.children[n.first], out);
          const std::uint32_t nt = _cfg.nonterminals++;
          produce(nt, std::move(out), entry_of(n.row));
          out = {nonterminal(nt)};
        }
        co_return;
      case ImageKind::assign:
      case ImageKind::append:
        co_await over(id, _image.children[n.first], entry_of(n.row), out);
        co_return;
      case ImageKind::mark: unsupported("a mark");
      case ImageKind::same: unsupported("an echo");
      case ImageKind::commit: unsupported("a commit");
    }
  }

  // What the condition `n`, an `&e` or a `!e`, asks of the text that comes.
  Descent<Test> condition(ImageNode n) {
    Test test;
    test.unless = n.kind == ImageKind::notOf;
    test.elected = _image.nodes[_image.children[n.first]].kind == ImageKind::trigger;
    const bool outer = std::exchange(_conditional, true);
    if (named(_image.children[n.first], test.ahead)) {
      std::ranges::sort(test.ahead);
      test.ahead.erase(std::unique(test.ahead.begin(), test.ahead.end()), test.ahead.end());
      _conditional = outer;
      co_return test;
    }
    // What it reads is more than a token: a production of its own reads
    // that from where the condition stands.
    refuse_recursion(_image.children[n.first]);
    test.ahead.clear();
    Alternative read;
    co_await emit(_image.children[n.first], read);
    _conditional = outer;
    produce(_cfg.nonterminals++, std::move(read));
    _cfg.productions.back().start = true;
    _cfg.conditions.push_back(static_cast<std::uint32_t>(_cfg.productions.size()) - 1);
    test.condition = static_cast<std::uint32_t>(_cfg.conditions.size());
    co_return test;
  }

  // A condition reads ahead on no stack of its own: refused where a data
  // rule it reads calls itself, directly or through other rules.
  void refuse_recursion(ImageId body) {
    // The data rules the image `id` calls, each where it is named.
    const auto calls = [&](ImageId id) {
      std::vector<std::uint32_t> out;
      for (std::vector<ImageId> open{id}; !open.empty();) {
        const ImageNode n = _image.nodes[open.back()];
        open.pop_back();
        if (n.kind == ImageKind::embed)
          throw Refused("`section<G>(open, close)` is allowed in a rule, not in a condition");
        if (n.kind == ImageKind::ref) {
          if (_image.stage(n.a).kind == ImageRuleKind::data) out.push_back(n.a);
          continue;
        }
        for (std::uint32_t i = 0; i < n.count; ++i) open.push_back(_image.children[n.first + i]);
      }
      if (_reads.size() < _image.rules.size()) _reads.resize(_image.rules.size(), 0);
      return out;
    };
    // Depth first: a rule met again on the way down calls itself.
    struct Down {
      std::uint32_t rule;
      std::vector<std::uint32_t> calls;
      std::size_t next = 0;
    };
    std::vector<Down> path{{kNone, calls(body)}};
    while (!path.empty()) {
      Down& top = path.back();
      if (top.next == top.calls.size()) {
        if (top.rule != kNone) _reads[top.rule] = 2;
        path.pop_back();
        continue;
      }
      const std::uint32_t rule = top.calls[top.next++];
      if (_reads[rule] == 2) continue;
      if (_reads[rule] == 1)
        throw Refused("a condition cannot test a data rule that calls itself, directly or through "
                      "other rules: test the terminals " + std::string(_image.rules[rule]->name) +
                      " starts with");
      _reads[rule] = 1;
      path.push_back({rule, calls(body_of(rule))});
    }
  }

  // The tests one of which holds where the condition `id` of an implied
  // token does: `|` is one or the other, `+` both.
  Descent<std::vector<Test>> asked(ImageId id) {
    const ImageNode n = _image.nodes[id];
    std::vector<Test> all;
    switch (n.kind) {
      case ImageKind::doc:
        co_return co_await asked(_image.children[n.first]);
      case ImageKind::choice:
        for (std::uint32_t i = 0; i < n.count; ++i)
          for (Test& test : co_await asked(_image.children[n.first + i]))
            all.push_back(std::move(test));
        co_return all;
      case ImageKind::seq: {
        all.emplace_back();
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const std::vector<Test> more = co_await asked(_image.children[n.first + i]);
          std::vector<Test> both;
          for (const Test& a : all)
            for (const Test& b : more) {
              // Two tests of the token, or two of the gap, are not one test.
              if ((a.gap != Test::Gap::any && b.gap != Test::Gap::any) ||
                  ((!a.ahead.empty() || a.condition != 0) && (!b.ahead.empty() || b.condition != 0)))
                unsupported("a condition of an implied token that asks two things of one text");
              Test joined = a.gap != Test::Gap::any ? a : b;
              const Test& token = !a.ahead.empty() || a.condition != 0 ? a : b;
              joined.ahead = token.ahead;
              joined.condition = token.condition;
              joined.unless = token.unless;
              joined.elected = token.elected;
              both.push_back(std::move(joined));
            }
          all = std::move(both);
        }
        co_return all;
      }
      case ImageKind::eof:
        all.emplace_back().ahead = {_cfg.regions[_region].end};
        co_return all;
      case ImageKind::gapHas:
      case ImageKind::gapIs:
        gap_of(id, false, all.emplace_back());
        co_return all;
      case ImageKind::notOf:
      case ImageKind::andOf:
        if (const ImageId tested = _image.children[n.first];
            _image.nodes[tested].kind == ImageKind::gapHas ||
            _image.nodes[tested].kind == ImageKind::gapIs)
          gap_of(tested, n.kind == ImageKind::notOf, all.emplace_back());
        else
          all.push_back(co_await condition(n));
        co_return all;
      default:
        unsupported("this condition of an implied token");
    }
  }

  // A test of the text between two tokens reads nothing: a nonterminal of
  // its own is whole only where that text is, or with `unless` is not, what
  // `tested` says. `id` is the node the nonterminal is kept for.
  void gap_test(ImageId id, ImageId tested, bool unless, Alternative& out) {
    const auto [nt, made] = auxiliary(nodes(), id);
    put(out, nonterminal(nt));
    if (!made) return;
    produce(nt, {});
    gap_of(tested, unless, _cfg.productions.back().tests.emplace_back());
  }

  // What the gap test `tested`, or with `unless` its contrary, asks.
  void gap_of(ImageId tested, bool unless, Test& test) {
    const ImageNode n = _image.nodes[tested];
    test.gapUnless = unless;
    if (n.kind == ImageKind::gapHas && (n.flags & kGapLine) != 0) {
      test.gap = Test::Gap::line;
    } else if (n.kind == ImageKind::gapIs &&
               _image.nodes[_image.children[n.first]].kind == ImageKind::epsilon) {
      test.gap = Test::Gap::empty;
    } else {
      test.gap = n.kind == ImageKind::gapHas ? Test::Gap::has : Test::Gap::is;
      test.gapPattern = _image.children[n.first];
    }
  }

  // Each subset shares its remaining items, or its prefix where a nest
  // needs the node already built there. A group may stop once only optional
  // items remain.
  Descent<void> unordered(ImageId id, ImageNode n, Alternative& out) {
    if (n.count > 11) throw Refused("an unordered group has too many items");
    const std::uint32_t all = (1u << n.count) - 1;
    const bool prefix = wraps(id);
    const auto opening = out.opening;
    const auto start = _cfg.nonterminals;
    if (!prefix) {
      const auto [nt, made] = auxiliary(nodes(), id);
      put(out, nonterminal(nt));
      if (!made) co_return;
      _cfg.nonterminals += all - 1;
    } else {
      _cfg.nonterminals += all + 1;
    }
    const auto part = [&](std::uint32_t i) { return _image.children[n.first + i]; };
    std::uint32_t optional = 0;
    for (std::uint32_t i = 0; i < n.count; ++i) {
      const auto& item = _image.nodes[part(i)];
      if (item.kind == ImageKind::repeat && item.m == 0) optional |= 1u << i;
    }
    const auto end = start + all;
    const auto outer = _unordered;
    bool carriesOuter = false;
    if (prefix) {
      produce(start, std::move(out));
      carriesOuter = _cfg.productions.back().prefixTurn;
      slot(_prefixes, start) = _unordered = start;
      out = {nonterminal(end)};
    }
    for (std::uint32_t done = 0; done < all; ++done) {
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if ((done >> i & 1) != 0) continue;
        Alternative alt;
        if (prefix) {
          const auto choice = _cfg.nonterminals++;
          produce(choice, {nonterminal(start + done)}, kNone, i);
          alt = {nonterminal(choice)};
          if (done == 0) alt.opening = opening;
        }
        const ImageNode item = _image.nodes[part(i)];
        if ((optional >> i & 1) == 0) {
          co_await emit(_image.children[n.first + i], alt);
        } else {
          // An item that may be left out, read: once, and for a repetition
          // with no bound the rest of it.
          if (item.n != 1 && item.n != kUnboundedCount)
            unsupported("a counted repetition in an unordered group");
          co_await emit(_image.children[item.first], alt);
          if (item.n == kUnboundedCount) co_await emit(part(i), alt);
        }
        const std::uint32_t then = done | 1u << i;
        if (!prefix && then != all) put(alt, nonterminal(start + then));
        produce(start + (prefix ? then : done), std::move(alt), kNone, prefix ? 0 : i);
      }
      if ((~done & all & ~optional) == 0) {
        if (prefix)
          produce(end, {nonterminal(start + done)}, kNone, n.count);
        else
          produce(start + done, {}, kNone, n.count);
      }
    }
    if (carriesOuter) _prefixes[end] = outer;
    _unordered = outer;
  }

  // The terminals a condition names, where it tests one token, the next:
  // the tokens it is true of. False where it reads more than a token.
  bool named(ImageId id, std::vector<std::uint16_t>& out) {
    std::vector<ImageId> open{id};
    while (!open.empty()) {
      const ImageId at = open.back();
      open.pop_back();
      const ImageNode n = _image.nodes[at];
      switch (n.kind) {
        case ImageKind::lit:
        case ImageKind::cpset:
        case ImageKind::byte:
        case ImageKind::through:
          out.push_back(static_cast<std::uint16_t>(unread(token(at))));
          break;
        case ImageKind::eof:
          out.push_back(_cfg.regions[_region].end);
          break;
        case ImageKind::indent:
        case ImageKind::dedent:
          out.push_back(static_cast<std::uint16_t>(unread(indentation(n.kind == ImageKind::indent))));
          break;
        case ImageKind::ref: {
          const Symbol s = rule_symbol(n.a);
          if (!is_terminal(s)) return false;
          out.push_back(static_cast<std::uint16_t>(unread(s)));
          break;
        }
        case ImageKind::choice:
        case ImageKind::doc:
        case ImageKind::trigger:
          for (std::uint32_t i = 0; i < n.count; ++i) open.push_back(_image.children[n.first + i]);
          break;
        case ImageKind::seq:
          if (n.count == 1) {
            open.push_back(_image.children[n.first]);
            break;
          }
          [[fallthrough]];
        default:
          return false;
      }
    }
    return true;
  }

  // Whether `id` holds a nest that wraps what stands before `id`: one in the
  // same construction context.
  bool wraps(ImageId id) {
    for (std::size_t at = _wraps.size(); at < _image.nodes.size(); ++at) {
      const ImageNode& n = _image.nodes[at];
      bool found = n.kind == ImageKind::nest;
      switch (n.kind) {
        case ImageKind::seq:
        case ImageKind::choice:
        case ImageKind::unordered:
        case ImageKind::repeat:
        case ImageKind::doc:
        case ImageKind::create:
        case ImageKind::flag:
        case ImageKind::skip:
          for (std::uint32_t i = 0; i < n.count && !found; ++i)
            found = _wraps[_image.children[n.first + i]] != 0;
          break;
        default:
          break;
      }
      _wraps.push_back(found ? 1 : 0);
    }
    return _wraps[id] != 0;
  }

  // A nest keeps its suffix in the last child of the record that builds
  // its wrapper. A nested directive can then wrap the result.
  Descent<void> built(ImageNode made, Alternative& out) {
    const ImageId payload = _image.children[made.first];
    const ImageNode n = _image.nodes[payload];
    const std::uint32_t count = n.kind == ImageKind::seq ? n.count : 1;
    const auto part = [&](std::uint32_t i) {
      return n.kind == ImageKind::seq ? _image.children[n.first + i] : payload;
    };
    const std::uint32_t entry = entry_of(made.row);
    const std::uint32_t nt = _cfg.nonterminals++;
    Alternative own;
    std::uint32_t at = 0;
    for (; at < count && !wraps(part(at)); ++at) co_await emit(part(at), own);
    produce(nt, std::move(own), made.kind == ImageKind::nest ? marked(WriteKind::nestBody) : entry);
    put(out, nonterminal(nt));
    if (made.kind == ImageKind::nest) {
      const std::uint32_t whole = _cfg.nonterminals++;
      produce(whole, std::move(out), entry);
      out = {nonterminal(whole)};
    }
    for (; at < count; ++at) co_await emit(part(at), out);
  }

  // A repetition whose item nests: each turn reads what stands before it,
  // which its nest wraps, so the list is that and the turns that follow.
  Descent<void> nesting(ImageNode n, Alternative& out) {
    const ImageId item = _image.children[n.first];
    for (std::uint64_t i = 0; i < n.m; ++i) co_await emit(item, out);
    if (n.n == kUnboundedCount) {
      const std::uint32_t list = _cfg.nonterminals++;
      produce(list, std::move(out));
      Alternative turn{nonterminal(list)};
      co_await emit(item, turn);
      produce(list, std::move(turn));
      out = {nonterminal(list)};
      co_return;
    }
    // `x` up to k times after `a` is `a` or (`a x`, then `x` up to k - 1).
    std::vector<std::uint32_t> options;
    for (std::uint64_t left = n.n > n.m ? n.n - n.m : 0; left != 0; --left) {
      const std::uint32_t option = _cfg.nonterminals++;
      produce(option, Alternative(out), kNone, 1);
      co_await emit(item, out);
      options.push_back(option);
    }
    for (std::size_t i = options.size(); i-- > 0;) {
      produce(options[i], std::move(out));
      out = {nonterminal(options[i])};
    }
  }

  // A directive stands over what it wraps: a nonterminal of its own reads
  // that, and its record applies the directive.
  Descent<void> over(ImageId id, ImageId payload, std::uint32_t entry, Alternative& out) {
    const auto [nt, made] = auxiliary(nodes(), id);
    if (made) co_await alternatives(payload, nt, entry);
    put(out, nonterminal(nt));
  }

  Descent<void> repeat(ImageId id, ImageNode n, Alternative& out) {
    const ImageId item = _image.children[n.first];
    for (std::uint64_t i = 0; i < n.m; ++i) co_await emit(item, out);
    if (n.n == kUnboundedCount) {
      // Left recursion: each turn is reduced as it ends.
      const auto [nt, made] = auxiliary(nodes(), id);
      if (made) {
        produce(nt, {});
        Alternative turn{nonterminal(nt)};
        co_await emit(item, turn);
        produce(nt, std::move(turn));
      }
      put(out, nonterminal(nt));
      co_return;
    }
    if (n.n <= n.m) co_return;
    // `x` up to k times is `option(x + (x up to k - 1 times))`.
    const auto [outer, made] = auxiliary(nodes(), id);
    put(out, nonterminal(outer));
    if (!made) co_return;
    std::uint32_t at = outer;
    for (std::uint64_t left = n.n - n.m; left != 0; --left) {
      produce(at, {}, kNone, 1);
      Alternative present;
      co_await emit(item, present);
      std::uint32_t next = kNone;
      if (left > 1) {
        next = _cfg.nonterminals++;
        put(present, nonterminal(next));
      }
      produce(at, std::move(present));
      at = next;
    }
  }

  // The symbol kept for a rule or a node, none yet where it is `kNone`.
  static Symbol& slot(std::vector<Symbol>& symbols, std::uint32_t index) {
    if (index >= symbols.size()) symbols.resize(index + 1, kNone);
    return symbols[index];
  }

  GrammarImage& _image;
  const LowerDesc& _desc;
  Cfg _cfg;
  // The published actions, by the address the image names each with; the
  // actions that carry a use's text, by that text.
  std::unordered_map<const void*, std::uint16_t> _action;
  std::map<std::tuple<WriteKind, std::string_view, std::uint16_t>, std::uint16_t> _marks;
  // Folded uses can match every spelling in their group; exact uses match only their own.
  struct Texts { std::string_view all, folded; };
  std::unordered_map<std::string_view, Texts, decltype(&foldedHash), decltype(&foldedEqual)>
      _texts{0, foldedHash, foldedEqual};
  std::unordered_map<std::string_view, std::string_view> _exactDocs;
  // Per scope, the symbols of its rules and expressions, those a condition
  // writes kept apart: it names tokens its own way and writes no record.
  // Terminals are kept in scope 0 alone.
  struct Scope {
    std::vector<Symbol> rules, nodes, conditionalRules, conditionalNodes;
  };
  Scope& scope() { return _scopes[_region]; }
  std::vector<Symbol>& nodes() { return _conditional ? scope().conditionalNodes : scope().nodes; }
  // The scope of the given lists of trivia rules, made where it is new: in
  // the text of the scope under way, or of a section of `grammar` that ends
  // at `end`.
  std::uint16_t region_of(const ImageTrivia& trivia, std::uint16_t end, std::uint32_t grammar) {
    Region region;
    region.end = end;
    region.grammar = grammar;
    const auto listed = [&](ImageRules rules, Trivia role, std::vector<std::uint16_t>& out) {
      out.reserve(rules.size());
      for (auto rule : rules) out.push_back(twin(terminal_of(_image.rule(*rule)), role));
      std::ranges::sort(out);
      out.erase(std::ranges::unique(out).begin(), out.end());
    };
    listed(trivia.ignored, Trivia::ignored, region.ignored);
    listed(trivia.hidden, Trivia::hidden, region.hidden);
    const auto index = _cfg.regions.size();
    _cfg.regions.push_back(std::move(region));
    const auto [known, fresh] = _regionIds.insert(index);
    if (fresh)
      _scopes.resize(_cfg.regions.size());
    else
      _cfg.regions.pop_back();
    return static_cast<std::uint16_t>(*known);
  }

  struct RegionOrder {
    const std::vector<Region>& regions;
    bool operator()(std::size_t a, std::size_t b) const { return regions[a] < regions[b]; }
  };
  std::set<std::size_t, RegionOrder> _regionIds{RegionOrder{_cfg.regions}};
  std::vector<Scope> _scopes = std::vector<Scope>(1);
  // Per terminal and role, its twin.
  std::map<std::pair<std::uint16_t, Trivia>, std::uint16_t> _twins;
  // Per image node in postorder, whether it holds a nest of its own context.
  std::vector<std::uint8_t> _wraps;
  // The unordered group whose prefix each marked nonterminal carries.
  std::vector<std::uint32_t> _prefixes;
  std::uint32_t _unordered = kNone;
  // Per rule, 1 while a condition's reading of it is being followed, 2 once
  // it is known to call itself through none.
  std::vector<std::uint8_t> _reads;
  std::uint16_t _region = 0;
  std::uint32_t _rule = kNone;
  std::uint32_t _alternative = 0;
  // The nonterminals of the sections, each of which reads its close; whether
  // a condition is being written.
  std::vector<std::uint32_t> _sections;
  bool _conditional = false;
};

}  // namespace

Cfg lower(GrammarImage& image, const LowerDesc& desc) {
  return Lowering(image, desc).run();
}

}  // namespace cursiv::engine::gen
