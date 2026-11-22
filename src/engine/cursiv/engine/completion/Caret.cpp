#include "cursiv/engine/completion/Caret.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engine/ast/AstBuilder.hpp"
#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/engine/recovery/Repair.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

namespace cursiv::engine {

namespace {

template<class Context>
void branch(Forks<Reading, Context>& forks, const Explorer& x, Context context) {
  if (x.rereads && !forks.enter(x, x.arms)) return;
  Reading r = x;
  for (auto i = x.armCount; i-- > 0;) {
    r.resume = x.arms + i;
    forks.open.push_back({r, context});
  }
}

// The text before a caret read as far as it goes: the stacks left where the
// caret stands, and what may be written there.
class Stand : private Machine::CaretTests {
 public:
  // The text of `text` up to `cut`: read by the states up to `from`, then by
  // every reading at once. The token that touches the caret begins at
  // `held`, `cut` for none; with `loose` that token is one the text before
  // it does not read. From `gap` on, only trivia stand before the caret: a
  // reading refused there is not repaired.
  Stand(const Language& language, std::string_view text, TextOffset from, TextOffset held,
        TextOffset gap, TextOffset cut, bool loose, const CancellationToken& cancelToken)
      : _language(language), t(language.tables), _stop(cancelToken), _held(held), _gap(gap),
        _cut(cut), _loose(loose), _before(text), _builder(_before),
        _m(t, _before.text().substr(0, from), _builder.stream(), cancelToken, this) {
    const std::string_view read = _before.text();
    reserve_for(_builder.stream(), from);
    // The end of the text read stands for a token no state acts on, so the
    // stack is left as the tokens made it.
    Machine& m = _m;
    m.read_to(m.end, t.terminalCount);
    const Exit exit = run(m);
    if ((!loose && m.forked < from) || exit != Exit::refused) return;
    if (m.tok.begin(m) < from && m.tok.begin(m) >= gap) return;
    if (m.tok.begin(m) < from) repair(m, _builder, read.data() + read.size());
    if (m.tok.begin(m) < from) return;
    _capture = true;
    _stacks.clear(std::span<const Frame>(m.frames, m.top));
    const auto top = static_cast<std::uint32_t>(m.top - m.frames) - 1;
    // The readings go on past the text a repair deleted, not yet written.
    const char* past = m.at;
    for (const Machine::Hidden& h : m.pending)
      if (h.entry == kSkippedElement) past = std::max(past, m.src + h.end);
    if (from == cut) {
      _roots.push_back({top, past, kNoStack, m.indented});
      return;
    }
    // From there on every reading is kept: each action of a split, to the
    // caret. The token that touches the caret is told with the mode of the
    // state that read it.
    m.read_to(m.src + cut, m.ended);
    std::uint16_t prefix = 1;
    std::unordered_set<std::uint16_t> endings;
    for (const char* p = m.src + held; p < m.end && prefix != 0; ++p) {
      if (p >= m.mark) m.watch(p);
      if (t.match && t.lexStates[prefix].accept) endings.insert(t.lexStates[prefix].accept);
      prefix = t.transition(prefix, t.byteClass[byte_at(p)]);
    }
    // The DFA may have handed the prefix to a matcher before reaching the caret.
    std::map<std::uint16_t, std::vector<std::uint64_t>> heldIn;
    const auto possible = [&](std::uint16_t mode) -> const std::uint64_t* {
      const auto reach = t.setOf(t.lexStates[prefix].reach);
      if (endings.empty()) return reach;
      auto [at, fresh] = heldIn.try_emplace(mode);
      if (fresh) {
        auto& words = at->second;
        words.assign(reach, reach + t.followWords);
        for (const auto accept : endings)
          for (auto how = t.modes[mode].win.at(0, accept); how > kWinNone;) {
            const auto& r = t.resolutions[how & ~kWinResolved];
            if (r.test != LexTest::match) break;
            words[r.token / 64] |= std::uint64_t{1} << (r.token % 64);
            how = r.otherwise;
          }
      }
      return at->second.data();
    };
    Forks<Reading, Root> paths;
    auto& open = paths.open;
    open.push_back({{top, 2 * m.under(), past, Token{nullptr, 0}, m.indented}, {}});
    std::unordered_set<Root, Root::Hash> known;
    std::map<std::tuple<std::uint32_t, TextOffset, std::uint32_t, std::uint32_t, std::uint16_t>, std::uint32_t> readers;
    Explorer::Closure closure{&chain};
    while (paths.pending()) {
      throwIfCancelled(_stop);
      auto o = std::move(open.back());
      open.pop_back();
      const auto state = _stacks.state(o.reading.node);
      const std::uint16_t mode = t.stateMode[state];
      chain = o.context.tests;
      closure.context = o.context.held;
      Explorer x(m, _stacks, o.reading, {.closure = paths.active.empty() ? &closure : nullptr});
      const Exit exit = x.step();
      const Token seen = exit == Exit::shifted ? x.taken : x.tok;
      // A suffix can change which terminal reads the bytes touching the caret.
      // Try its other readings here and elect them on the edited text in keeps_held.
      if (held < cut && seen.terminal() != t.indentTerminal && seen.terminal() != t.dedentTerminal &&
          mode != kNoToken && o.reading.resume == 2 * state &&
          seen.begin(m) == held && o.reading.at <= m.src + held) {
        const auto reach = possible(mode);
        const auto follow = t.setOf(t.followOf[state]);
        for (std::uint32_t w = 0; w < t.followWords; ++w)
          for (auto bits = reach[w] & follow[w]; bits; bits &= bits - 1) {
            const auto term = static_cast<std::uint16_t>(64 * w + std::countr_zero(bits));
            if (term == seen.terminal() && seen.end == m.end) continue;
            Reading other = o.reading;
            other.resume += 1;
            other.tok = token(held, m.end, term);
            open.push_back({other, o.context});
          }
      }
      o.context.tests = chain;
      if (exit == Exit::split) {
        branch(paths, x, o.context);
      } else if (exit == Exit::shifted) {
        if (x.taken.begin(m) == held && x.taken.end == m.end) {
          const auto node = mode != kNoToken ? o.reading.node : _stacks.parent(x.node);
          const auto from = static_cast<TextOffset>(o.reading.at - m.src);
          const auto indent = o.reading.indented;
          const auto [at, added] = readers.try_emplace(
              std::tuple{node, from, indent.level, indent.depth, x.taken.terminal()}, _readers.size());
          if (added) _readers.push_back({node, from, indent, x.taken.terminal()});
          o.context.held = at->second;
        }
        Reading next = x;
        next.tok = {nullptr, 0};
        open.push_back({next, o.context});
      } else if (exit == Exit::refused && x.tok.terminal() == m.ended &&
                 x.tok.begin(m) == cut && (held == cut || o.context.held != kNoStack)) {
        o.context.node = x.node;
        o.context.at = x.at;
        o.context.indented = x.indented;
        if (known.insert(o.context).second) _roots.push_back(o.context);
      }
    }
  }

  // Where the first split began whose readings the text before the caret
  // did not tell apart; past every offset for none.
  [[nodiscard]] TextOffset forked() const { return _m.forked; }
  [[nodiscard]] bool stands() const { return !_roots.empty(); }

  // Adds to `out` each terminal a reading shifts at the caret, the text
  // resuming at `resume` of `text`: with the member it is read for, and only
  // where the lexer still reads it and what stands before it once it is
  // written.
  void offer(std::string_view text, TextOffset resume, std::vector<NextFeature>& out) {
    Machine& m = _m;
    const AstTables& ast = _language.ast;
    const auto replaced = text.substr(_cut, resume - _cut);
    _suffix = text.substr(resume);
    _spliced.clear();
    _prefix = _wordAt = 0;
    std::unordered_map<std::uint16_t, std::size_t> modeAt;
    for (const Root& root : _roots) {
      const std::uint32_t state = _stacks.state(root.node);
      const auto [at, added] = modeAt.try_emplace(t.stateMode[state], _modes.size());
      if (added) {
        Mode& mode = _modes.emplace_back(t.stateMode[state], std::vector<std::uint64_t>(t.followWords));
        if (mode.mode != kNoToken) {
          if (!replaced.empty()) mode.replaced = read(text.substr(_cut), mode.mode);
          if (_gap < _cut) {
            mode.trivia = trivia_at(text, _gap, _cut, mode.mode);
            const auto* cut = text.data() + _cut;
            if (mode.trivia.end && mode.trivia.end > cut) {
              const auto width = t.word_width(cut, text.data() + text.size());
              mode.inside = trivia_at(text.substr(0, _cut + width), mode.trivia.begin, _cut,
                                      mode.mode).end != cut;
            }
          }
        }
      }
      for (std::uint32_t w = 0; w < t.followWords; ++w)
        _modes[at->second].takes[w] |= t.setOf(t.followOf[state])[w];
    }
    struct Continuation { std::uint32_t tests, fresh; };
    Forks<Reading, Continuation> paths;
    auto& open = paths.open;
    std::map<std::uint32_t, bool> fits;
    // Each feature offered once, in the order first found.
    using Key = std::tuple<std::uint16_t, std::string_view, std::uint8_t, const AstTypeTag*, bool>;
    const auto key = [](const NextFeature& f) {
      return Key{f.feature, f.property, f.narrow, f.type, f.entered};
    };
    std::set<Key> offered;
    for (const NextFeature& f : out) offered.insert(key(f));
    const auto keep = [&](const NextFeature& f) {
      if (offered.insert(key(f)).second) out.push_back(f);
    };
    for (std::uint32_t w = 0; w < t.followWords; ++w) {
      std::uint64_t candidates = 0;
      for (const Mode& mode : _modes) candidates |= mode.takes[w];
      if (w == 0) candidates &= ~std::uint64_t{1};
      for (; candidates != 0; candidates &= candidates - 1) {
        const auto terminal = static_cast<std::uint16_t>(64 * w + std::countr_zero(candidates));
        // Indentation is made by the parse, never written.
        if (t.terminalTrivia[terminal] != Trivia::none || terminal == t.indentTerminal ||
            terminal == t.dedentTerminal)
          continue;
        throwIfCancelled(_stop);
        // The text ends at the caret in the token tried: a state that reads
        // the text again there reads it.
        m.read_to(m.end, terminal);
        const Token pinned = token(_cut, m.src + _cut, terminal);
        Explorer::Closure closure{&chain};
        for (const Root& root : _roots) {
          // A terminal the root's state has no action for is refused there.
          const std::uint32_t state = _stacks.state(root.node);
          if (!t.inSet(t.followOf[state], terminal)) continue;
          const Mode& mode = _modes[modeAt.at(t.stateMode[state])];
          fits.clear();
          const auto fits_text = [&](std::uint32_t tests) {
            const auto [fit, fresh] = fits.try_emplace(tests);
            if (fresh)
              fit->second = written(terminal, mode, root, tests, replaced);
            return fit->second;
          };
          if (!fits_text(root.tests)) continue;
          closure.context = std::uint64_t{mode.mode} << 32 | root.held;
          open.assign(1, {{root.node, 2 * state + 1, root.at, pinned, root.indented}, {root.tests, 0}});
          while (paths.pending()) {
            throwIfCancelled(_stop);
            const auto r = open.back().reading;
            const auto [tests, fresh] = open.back().context;
            chain = tests;
            open.pop_back();
            Explorer x(m, _stacks, r, {.closure = &closure});
            x.fresh = fresh;
            const Exit exit = x.step();
            if (exit == Exit::split) {
              branch(paths, x, {chain, x.fresh});
              continue;
            }
            if (exit != Exit::shifted || x.taken.terminal() != terminal ||
                (chain != root.tests && !fits_text(chain))) continue;
            // The member the token is read for: the write its own leaf
            // carries, else that of the innermost rule it begins.
            const std::uint32_t below = _stacks.parent(x.node);
            bool named = false;
            const auto each = [&](std::uint32_t state, auto&& visit) {
              if (const std::uint32_t list = ast.caretOf[state]; list != 0)
                for (std::uint32_t k = ast.caretAt[list - 1]; k != ast.caretAt[list]; ++k)
                  visit(ast.caretWord[k]);
            };
            // `write` told at `node`, `above` frames under the token: its node
            // begins with the token where a state its rule begins in is
            // reached through frames this reading made of no text and
            // repetitions.
            const auto add = [&](std::uint32_t write, std::uint32_t node, std::uint32_t above) {
              NextFeature next;
              next.feature = terminal;
              const detail::FieldDesc& member = ast.meta->fields[ast.memberOf[write]];
              next.property = member.name;
              next.type = ast.meta->tag_of(member.owner);
              next.narrow = ast.narrowOf[write];
              for (; node != kNoStack && !next.entered; node = _stacks.parent(node), ++above) {
                bool turn = false;
                each(_stacks.state(node), [&](std::uint32_t word) {
                  next.entered = next.entered || word == (kCaretNode | write << 16);
                  turn = turn || word == kCaretTurn;
                });
                if (above >= x.fresh && !turn) break;
              }
              keep(next);
              named = true;
            };
            each(x.from, [&](std::uint32_t word) {
              if (word >> 30 == 0 && (word & 0xFFFF) == terminal) add(word >> 16, below, 0);
            });
            for (std::uint32_t node = below, above = 0; !named && node != kNoStack;
                 node = _stacks.parent(node), ++above) {
              each(_stacks.state(node), [&](std::uint32_t word) {
                if (word >> 30 != kCaretRule >> 30) return;
                const std::uint64_t* const set =
                    ast.caretSets + std::size_t{word & 0xFFFF} * t.followWords;
                if (((set[terminal / 64] >> (terminal % 64)) & 1) != 0)
                  add((word >> 16) & 0x3FFF, node, above);
              });
              if (above >= x.fresh) break;
            }
            if (!named) {
              NextFeature next;
              next.feature = terminal;
              keep(next);
            }
          }
        }
      }
    }
  }

 private:
  struct PendingTest {
    Machine::Predicate predicate;
    const char* at;
    Token token;
    Indented indent;
    std::uint32_t prior;
  };

  bool test(Machine::Predicate predicate, const char* p, Token tok, Indented indent) override {
    if (!_capture || tok.end != _m.end) {
      const auto result = _m.on_text([&] { return predicate(_m, p, tok, indent); });
      if (!result.atCut && tok.end != _m.end) return result.value;
      if (!_capture) {
        _m.forked = std::min(_m.forked, static_cast<TextOffset>(p - _m.src));
        return result.value;
      }
    }
    _tests.push_back({predicate, p, tok, indent, chain});
    chain = static_cast<std::uint32_t>(_tests.size());
    return true;
  }

  // Keep the longer side of an edit in place across candidates.
  std::string_view splice(TextOffset from, std::string_view word) {
    const std::size_t prefix = _cut - from;
    const auto limit = std::min(kMaxDocument, _spliced.max_size());
    const auto size = std::uint64_t{prefix} + word.size() + _suffix.size();
    if (size > limit) throw std::length_error("completion text exceeds the document size limit");
    auto at = std::max(_wordAt, prefix);
    const std::size_t room = limit - _suffix.size();
    if (std::max(prefix, _prefix) <= _suffix.size()) {
      const auto head = _spliced.empty() ? 0 : _spliced.size() - _suffix.size();
      const auto need = prefix + word.size();
      const auto grown = std::min<std::uint64_t>(room, std::max<std::uint64_t>(need, 2 * std::uint64_t{head}));
      at = (head >= need ? head : grown) - word.size();
    }
    at = std::min(at, room - word.size());
    if (at != _wordAt) _prefix = 0;
    _wordAt = at;
    if (_spliced.size() != at + word.size() + _suffix.size()) {
      _spliced.resize(at + word.size());
      _spliced.append(_suffix);
    }
    char* begin = _spliced.data() + (at - prefix);
    if (prefix > _prefix) {
      _before.text().copy(begin, prefix - _prefix, from);
      _prefix = prefix;
    }
    word.copy(_spliced.data() + at, word.size());
    return {begin, static_cast<std::size_t>(size)};
  }

  bool holds(std::uint32_t tests, std::string_view word, std::size_t length) {
    if (!tests) return true;
    Machine m(t, splice(0, word), _builder.stream(), _stop);
    m.matchTies = _m.matchTies;
    m.levels.base = &_m.levels;
    for (; tests; tests = _tests[tests - 1].prior) {
      const auto& test = _tests[tests - 1];
      Token tok = test.token;
      tok.end = m.src + (tok.end - _m.src);
      if (tok.begin(m) == _cut && tok.end == m.src + _cut) tok.end += length;
      if (!test.predicate(m, m.src + (test.at - _m.src), tok, test.indent)) return false;
    }
    return true;
  }

  // A stack left at the caret, by its top, with where its last token ends;
  // and the stack whose state read the token that touches the caret, where
  // this reading read one.
  struct Root {
    std::uint32_t node = 0;
    const char* at = nullptr;
    std::uint32_t held = kNoStack;
    Indented indented{};
    std::uint32_t tests = 0;
    bool operator==(const Root&) const = default;
    struct Hash {
      std::size_t operator()(const Root& r) const {
        return std::hash<std::uint64_t>{}(std::uint64_t{r.node} << 32 ^
                                          reinterpret_cast<std::uintptr_t>(r.at) ^
                                          std::uint64_t{r.held} << 40 ^ r.indented.level ^ r.tests);
      }
    };
  };

  // The longest token mode `mode` reads at the start of `text`: what as, how
  // many bytes, and its rank among tokens that end together.
  struct Read {
    std::uint16_t terminal;
    std::size_t length;
    std::uint8_t rank;
  };
  // A mode at the caret, the terminals it acts on and its reading before the edit.
  struct TriviaSpan {
    TextOffset begin = 0;
    const char* end = nullptr;
  };
  struct Mode {
    std::uint16_t mode;
    std::vector<std::uint64_t> takes;
    Read replaced{kNoToken, 0, 0};
    TriviaSpan trivia;
    bool inside = false;
  };

  [[nodiscard]] Read read(std::string_view text, std::uint16_t mode) {
    Machine spliced(t, text, _builder.stream(), _stop);
    spliced.read_to(spliced.end, t.terminalCount);
    const Token tok = spliced.walk(spliced.src, mode, 1, spliced.src);
    return {tok.terminal(), static_cast<std::size_t>(tok.end - spliced.src),
            tok.terminal() < t.terminalCount ? spliced.rank(tok) : std::uint8_t{0xFF}};
  }

  // Follow ties and lexical retries to the token this stack actually shifts.
  [[nodiscard]] bool elects(std::uint16_t terminal, Read& first, std::uint16_t mode,
                            const Root& root, std::string_view word, bool current = false) {
    if (first.terminal >= t.terminalCount) return false;
    Machine m(t, current ? _before.text() : splice(0, word), _builder.stream(), _stop);
    m.levels.base = &_m.levels;
    const char* begin = m.src + _cut;
    Token tok = m.walk(begin, mode, 1, begin);
    if (!takes(m, _stacks, {root.node, 0, m.src + (root.at - _m.src), tok, root.indented},
               2 * _stacks.state(root.node) + 1, 1, terminal, &tok)) return false;
    first = {terminal, static_cast<std::size_t>(tok.end - begin), m.rank(tok)};
    return true;
  }

  struct Holder {
    std::uint32_t node;
    TextOffset from;
    Indented indented;
    std::uint16_t terminal;
  };

  // Re-read the token touching the caret in the edited text, from the stack
  // and indentation that read it before the edit.
  [[nodiscard]] bool keeps_held(std::string_view word, const Holder& held) {
    const std::uint32_t state = _stacks.state(held.node);
    const std::uint16_t mode = t.stateMode[state];
    const bool indent = held.terminal == t.indentTerminal || held.terminal == t.dedentTerminal;
    if (!t.retries[state] && !indent) {
      const Read r = read(splice(_held, word), mode);
      if (r.length != _cut - _held || r.terminal == t.terminalCount) return false;
      if (r.terminal == held.terminal) return true;
    }
    Machine m(t, splice(0, word), _builder.stream(), _stop);
    m.levels.base = &_m.levels;
    if (indent) {
      Indented level = held.indented;
      const Token token = m.indentation(m.src + held.from, level);
      return token.end == m.src + _cut && token.terminal() == held.terminal;
    }
    const char* const start = m.src + _held;
    Token tok = m.walk(start, mode, 1, start);
    return takes(m, _stacks, {held.node, 0, m.src + held.from, tok, held.indented},
                 2 * state + 1, 1, held.terminal, &tok) && tok.end == m.src + _cut;
  }

  // The candidate's opening must leave the token or trivia to its left intact.
  bool keeps_before(std::string_view word, const Mode& site, const Root& root) {
    if (site.trivia.end) {
      const auto prefix = _cut - site.trivia.begin;
      const auto edited = splice(site.trivia.begin, word);
      if (site.trivia.end == _before.text().data() + _cut) return read(edited, site.mode).length == prefix;
      return !t.word_width(word.data(), word.data() + word.size()) ||
             trivia_at(edited, 0, prefix, site.mode).end == edited.data() + prefix;
    }
    if (_held == _cut) return true;
    if (_loose) return read(splice(_held, word), t.modeFault[site.mode]).length <= _cut - _held;
    return keeps_held(word, _readers[root.held]);
  }

  // Whether a spelling can be written between the preceding text and the suffix.
  [[nodiscard]] CURSIV_NOINLINE bool written(std::uint16_t terminal, const Mode& site, const Root& root,
                             std::uint32_t tests, std::string_view replaced) {
    const auto mode = site.mode;
    if (site.inside) return false;
    const auto fits = [&](std::string_view word, Read alone, bool current = false) {
      return (alone.terminal == terminal || elects(terminal, alone, mode, root, word, current)) &&
             (!current || alone.length == word.size()) &&
             !beaten(word, mode, alone, current) && holds(tests, word, alone.length) &&
             keeps_before(word, site, root);
    };
    if (!replaced.empty() && fits(replaced, site.replaced, true)) return true;
    const auto words = _language.grammar.terminals[terminal].spellings;
    if (!words.empty()) return std::ranges::any_of(words, [&](std::string_view word) {
      return fits(word, read(splice(_cut, word), mode));
    });
    bool opaque = false, elected = false;
    for (bool letters : {true, false})
      if (spelling(terminal, site, root, letters, opaque, elected, fits)) return true;
    return opaque && !elected && holds(tests, {}, 0);
  }

  // Whether another reading at the caret elects another token than `mine`,
  // what `mode` reads of `text`: one its own mode reads longer, or as long
  // with a lower rank, and that it acts on.
  [[nodiscard]] bool beaten(std::string_view word, std::uint16_t mode, const Read& mine,
                            bool current) {
    const auto stronger = [&](Read r) {
      return r.length > mine.length || (r.length == mine.length && r.rank < mine.rank);
    };
    for (const Mode& other : _modes) {
      if (other.mode == mode || other.inside) continue;
      const Read r = current ? other.replaced : read(splice(_cut, word), other.mode);
      if (r.terminal >= t.terminalCount || r.terminal == mine.terminal ||
          ((other.takes[r.terminal / 64] >> (r.terminal % 64)) & 1) == 0)
        continue;
      if (!stronger(r)) continue;
      if (std::ranges::any_of(_roots, [&](const Root& root) {
        const auto state = _stacks.state(root.node);
        Read elected = r;
        return t.stateMode[state] == other.mode && t.inSet(t.followOf[state], r.terminal) &&
               elects(r.terminal, elected, other.mode, root, word, current) && stronger(elected) &&
               holds(root.tests, word, elected.length) && keeps_before(word, other, root);
      })) return true;
    }
    return false;
  }

  // The trivia covering the last byte before the cut, when the whole gap is trivia.
  [[nodiscard]] TriviaSpan trivia_at(std::string_view text, TextOffset from, TextOffset cut,
                                std::uint16_t mode) {
    Machine m(t, text, _builder.stream(), _stop);
    m.ended = t.terminalCount;
    for (const char* p = m.src + from; p < m.src + cut;) {
      const Token tok = m.walk(p, mode, 1, p);
      if (tok.end <= p || tok.terminal() >= t.terminalCount ||
          t.terminalTrivia[tok.terminal()] == Trivia::none) break;
      if (tok.end >= m.src + cut) return {tok.begin(m), tok.end};
      p = tok.end;
    }
    return {};
  }

  // Try DFA spellings against the lexer and the predicates pending at the caret.
  template<class Fits>
  bool spelling(std::uint16_t terminal, const Mode& site, const Root& root,
                bool letters, bool& opaque, bool& elected, Fits&& fits) {
    const auto mode = site.mode;
    auto& sample = _samples[letters];
    if (!sample) sample = samples(t, letters);
    const LexBlocks win = t.modes[mode].win;
    // On the second pass, keep paths that extend the preceding token differently.
    // Its last acceptance still distinguishes paths once that lexer stops.
    const TextOffset from = site.trivia.end ? site.trivia.begin : _held;
    std::uint16_t left = 0, leftMode = mode;
    const auto continues = [&](std::uint16_t state) {
      const auto* onward = t.modes[leftMode].onward;
      return (onward[state >> 3] >> (state & 7)) & 1;
    };
    if (!letters && from < _cut) {
      auto [at, fresh] = _leftAt.try_emplace(from, 1);
      if (fresh) {
        const auto text = _before.text();
        for (auto i = from; i != _cut && at->second; ++i) {
          if (((i - from) & 1023) == 0) throwIfCancelled(_stop);
          at->second = t.transition(at->second, t.byteClass[byte_at(text.data() + i)]);
        }
      }
      left = at->second;
      if (!site.trivia.end)
        leftMode = _loose ? t.modeFault[mode] : t.stateMode[_stacks.state(_readers[root.held].node)];
      if (leftMode == kNoToken || !continues(left)) left = 0;
    }
    for (const auto& entry : _queue) _reached[entry.state] = 0;
    if (_reached.size() < 2) _reached.resize(2);
    _reached[1] = 1;
    _queue.assign(1, {0, left, 1, 0});
    std::unordered_set<std::uint64_t> other;
    for (std::size_t next = 0; next < _queue.size(); ++next) {
      if ((next & 63) == 0) throwIfCancelled(_stop);
      const auto s = _queue[next].state;
      const auto context = _queue[next].context;
      // Whether one of the readings of the state is the terminal.
      bool ends = false;
      bool matcher = false;
      unsigned back = 0;
      const auto accept = t.lexStates[s].accept;
      const auto ending = win.at(0, accept);
      for (std::uint16_t how = ending; how != kWinNone && !ends;) {
        if (how < kWinNone) {
          ends = how == terminal;
          back = 0;
          break;
        }
        const LexResolution& r = t.resolutions[how & ~kWinResolved];
        ends = r.token == terminal;
        matcher = ends && r.test == LexTest::match;
        back = r.back;
        how = r.otherwise;
      }
      if (s == 1 && matcher) opaque = true;
      if (s != 1 && (ends || (ending != kWinNone && t.lexTieEnds &&
                             t.inSet(t.lexTieEnds[accept], terminal)))) {
        std::string found;
        for (auto at = next; at; at = _queue[at].prior) {
          if ((found.size() & 1023) == 1023) throwIfCancelled(_stop);
          found.push_back(_queue[at].byte);
        }
        std::ranges::reverse(found);
        if (found.size() > back) found.resize(found.size() - back);
        else found.clear();
        if (!found.empty() && (!matcher || keeps_before(found, site, root))) {
          opaque |= matcher;
          Read alone = read(splice(_cut, found), mode);
          if (alone.terminal == terminal || elects(terminal, alone, mode, root, found)) {
            elected = true;
            if (fits(found, alone)) return true;
          }
        }
      }
      // Only through the states past which the terminal can still end.
      for (unsigned c = 0; c < t.lexClassCount; ++c) {
        const std::uint16_t to = t.transition(s, c);
        if (to == 0 || (*sample)[c] < 0) continue;
        const std::uint64_t* const reach = t.setOf(t.lexStates[to].reach);
        if (((reach[terminal / 64] >> (terminal % 64)) & 1) == 0) continue;
        auto after = context;
        if (const auto side = static_cast<std::uint16_t>(context)) {
          const auto state = t.transition(side, c);
          const auto accept = t.modes[leftMode].win.at(0, t.lexStates[state].accept);
          after = accept == kWinNone ? context & 0xFFFF0000u : std::uint32_t{accept} << 16;
          if (state && continues(state)) after |= state;
        }
        if (to >= _reached.size()) _reached.resize(to + 1);
        const auto first = _reached[to];
        if (first && (_queue[first - 1].context == after ||
                      !other.insert(std::uint64_t{to} << 32 | after).second)) continue;
        const char b = static_cast<char>((*sample)[c]);
        if (!letters && !left && s == 1 && !keeps_before({&b, 1}, site, root)) continue;
        if (!first) _reached[to] = _queue.size() + 1;
        _queue.push_back({next, after, to, b});
      }
    }
    return false;
  }

  // A byte of each class, -1 for none: of letters and digits first with
  // `letters`, else of the other bytes first.
  static std::array<int, 256> samples(const Tables& t, bool letters) {
    std::array<int, 256> sample;
    sample.fill(-1);
    const auto offer_byte = [&](unsigned b) {
      if (sample[t.byteClass[b]] < 0) sample[t.byteClass[b]] = static_cast<int>(b);
    };
    const auto word_bytes = [&] {
      for (unsigned b = 'a'; b <= 'z'; ++b) offer_byte(b);
      for (unsigned b = 'A'; b <= 'Z'; ++b) offer_byte(b);
      for (unsigned b = '0'; b <= '9'; ++b) offer_byte(b);
    };
    if (letters) word_bytes();
    const auto in_word = [](unsigned b) {
      return ((b | 0x20) >= 'a' && (b | 0x20) <= 'z') || (b >= '0' && b <= '9');
    };
    for (unsigned b = 0x21; b < 0x7F; ++b)
      if (letters || !in_word(b)) offer_byte(b);
    if (!letters) word_bytes();
    for (unsigned b = 0; b < 256; ++b) offer_byte(b);
    return sample;
  }

  const Language& _language;
  const Tables& t;
  const CancellationToken& _stop;
  TextOffset _held;
  TextOffset _gap;
  TextOffset _cut;
  bool _loose;
  Cst _before;
  CstBuilder _builder;
  Machine _m;
  bool _capture = false;
  std::vector<PendingTest> _tests;
  std::string _spliced;
  std::size_t _prefix = 0, _wordAt = 0;
  std::string_view _suffix;
  Cactus _stacks;
  std::vector<Holder> _readers;
  std::vector<Root> _roots;
  struct Spelling {
    std::size_t prior;
    std::uint32_t context;
    std::uint16_t state;
    char byte;
  };
  std::array<std::optional<std::array<int, 256>>, 2> _samples;
  std::unordered_map<TextOffset, std::uint16_t> _leftAt;
  std::vector<std::size_t> _reached;
  std::vector<Spelling> _queue;
  std::vector<Mode> _modes;
};

}  // namespace

void next_features(const Language& language, const Cst& cst, TextOffset at, TextOffset resume,
                   std::vector<NextFeature>& out, const CancellationToken& cancelToken) {
  throwIfCancelled(cancelToken);
  const std::string_view text = cst.text();
  if (resume < at || resume > text.size()) return;
  std::uint32_t leaves = 0;
  const auto before = [&](NodeId id) {
    if ((++leaves & 1023) == 0) throwIfCancelled(cancelToken);
    return cst.leafBefore(id);
  };
  // A caret inside a token or a comment, or behind replaced text, stands
  // where that begins, and the text resumes past it.
  TextOffset cut = at;
  NodeId id = cst.leafAfter(at);
  if (id < cst.size() && cst[id].begin < at) {
    cut = cst[id].begin;
    resume = std::max(resume, cst[id].end);
    id = cst.leafAfter(cut);
  }
  // Walk back over contiguous replacements, then keep the last written
  // token. Reread it if it touches the caret.
  TextOffset held = cut, gap = 0;
  bool retreat = true;
  while (id > 0) {
    id = before(id);
    const CstNode& leaf = cst[id];
    if (leaf.begin == leaf.end) continue;
    if (id && leaf.element() == kSkippedElement) {
      const CstNode& invented = cst[before(id)];
      if (invented.isRecovered() && invented.begin == leaf.begin && invented.end == leaf.begin) {
        if (retreat && leaf.end == cut) held = cut = leaf.begin;
        else retreat = false;
        continue;
      }
    }
    retreat = false;
    if (leaf.end > cut || leaf.isHidden()) continue;
    gap = leaf.end;
    if (gap == cut) held = leaf.begin;
    break;
  }
  // The states read up to the token that touches the caret, or to the first
  // split the text before the caret does not tell the readings of apart.
  TextOffset from = held;
  for (;;) {
    Stand stand(language, text, from, held, gap, cut, false, cancelToken);
    if (stand.forked() < from) {
      from = from == held ? stand.forked() : 0;
      continue;
    }
    if (stand.stands()) {
      stand.offer(text, resume, out);
      return;
    }
    break;
  }
  if (Stand plain(language, text, cut, held, gap, cut, true, cancelToken); plain.stands())
    plain.offer(text, resume, out);
}

}  // namespace cursiv::engine
