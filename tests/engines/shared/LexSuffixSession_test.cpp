#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/engine/parser/Match.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/engine/parser/Repairer.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

namespace {

using namespace cursiv;
using namespace cursiv::engine;

std::uint64_t scanned = 0;

const char* scan(const std::uint8_t* in, const char* p, const char* end) {
  while (p != end) {
    ++scanned;
    if (in[byte_at(p)] == 0) break;
    ++p;
  }
  return p;
}

// Mode 0 reads ID=[a]+ or LONG=a[a ]*!; mode 1 only ID, mode 2 only LONG.
struct Lexer {
  std::array<std::uint8_t, 256> classes{};
  // Eighteen classes per row, with shared full blocks and two-entry tails.
  std::array<std::uint16_t, 52> next{};
  std::array<std::uint32_t, 8> rows{0, 16, 18, 16, 18, 34, 36, 34};
  std::array<LexState, 5> states{{{0, 0, 0}, {2, 0, 0}, {4, 1, 0}, {6, 0, 0}, {0, 2, 0}}};
  std::array<std::uint16_t, 9> win{kWinNone, 1,        2,        kWinNone, 1,
                                   kWinNone, kWinNone, kWinNone, 2};
  std::array<std::uint8_t, 3> onward{14, 6, 14};
  std::array<std::uint8_t, 5> runs{0, 0, 0, 1, 0};
  std::array<std::uint8_t, 4 * 256> sets{};
  std::array<RunScanner, 4> scanners{scan, scan, scan, scan};
  std::array<Trivia, 3> trivia{};
  std::array<std::uint8_t, 3> ranks{};
  std::array<LexMode, 3> modes{};
  Tables tables;

  Lexer() {
    classes.fill(3);
    classes['a'] = 0;
    classes[' '] = 16;
    classes['!'] = 17;
    next[18] = 2;
    next[34] = next[36] = 3;
    next[35] = 4;
    for (unsigned b = 0; b < 256; ++b) {
      sets[b] = b == 'a' || b == ' ';
      sets[256 + b] = b >= '0' && b <= '9';
      sets[512 + b] = b != '$' && b != '\\' && b != '`';
      sets[768 + b] = b % 3 != 0;
    }
    tables.byteClass = classes.data();
    tables.lexNext = {next.data(), rows.data()};
    tables.lexStates = states.data();
    tables.lexRun = runs.data();
    tables.runSets = sets.data();
    tables.runScanners = scanners.data();
    for (std::size_t i = 0; i < modes.size(); ++i)
      modes[i] = {nullptr, {win.data() + 3 * i, rows.data()}, onward.data() + i};
    tables.modes = modes;
    tables.terminalTrivia = trivia.data();
    tables.terminalRank = ranks.data();
    tables.terminalCount = 3;
    tables.lexClassCount = 18;
  }
};

struct Reading {
  Cst tree;
  CstBuilder builder;
  CancellationToken stop;
  Machine machine;

  Reading(const Tables& tables, std::string_view text,
          CancellationToken token = kNoCancel)
      : tree(text),
        builder(tree),
        stop(token),
        machine(tables, tree.text(), builder.stream(), stop) {}
};

void guards_report_when_the_window_cuts_their_input() {
  Lexer lexer;
  const LexResolution eof{.token = 2, .otherwise = 1, .back = 1, .test = LexTest::eof};
  const LexResolution word{.token = 2, .otherwise = 1, .test = LexTest::word};
  const LexResolution resolutions[] = {eof, word};
  std::array<std::uint8_t, 256> wordBytes{};
  wordBytes[0xc3] = 2;
  const unicode::Range accent{U'é', U'é'};
  lexer.tables.resolutions = resolutions;
  lexer.tables.wordBytes = wordBytes.data();
  lexer.tables.wordRanges = &accent;
  lexer.tables.wordRangeCount = 1;
  Reading input(lexer.tables, "xé!");
  Machine& m = input.machine;
  const auto test = [&](unsigned window, unsigned guard, unsigned at, bool holds, bool cut) {
    m.read_to(m.src + window, lexer.tables.terminalCount);
    const auto result = m.on_text([&] { return m.reading(kWinResolved | guard, m.src + at).token == 2; });
    CHECK(result.value == holds && result.atCut == cut);
  };
  test(2, 0, 2, true, true);
  test(3, 0, 2, false, false);
  test(1, 1, 1, true, true);
  test(2, 1, 1, true, true);
  test(3, 1, 1, false, false);

  std::array<std::uint8_t, 256> classes{};
  classes['!'] = 1;
  const std::uint16_t next[] = {0, 0, 1, 2, 0, 0};
  const std::uint8_t ends[] = {0, 0, 1};
  const TextMonitor untilBang{classes.data(), next, ends, 2};
  const std::uint32_t waitAt[] = {0, 3}, waitWord[] = {0, 0, 1};
  lexer.tables.monitors = &untilBang;
  lexer.tables.waitAt = waitAt;
  lexer.tables.waitWord = waitWord;
  for (const unsigned width : {3u, 4u}) {
    m.read_to(m.src + width, lexer.tables.terminalCount);
    for (unsigned read = 0; read < 2; ++read) {
      const auto result = m.on_text([&] { return m.settles(0, m.src); });
      CHECK(result.value == (width == 4) && result.atCut == (width == 3));
    }
  }
}

void cached_suffixes_agree_with_complete_walks() {
  Lexer lexer;
  std::mt19937 random(20261118);
  for (unsigned run = 0; run < lexer.scanners.size(); ++run) {
    const auto* in = lexer.sets.data() + 256 * run;
    std::vector<unsigned char> allowed;
    for (unsigned b = 0; b < 256; ++b)
      if (in[b]) allowed.push_back(static_cast<unsigned char>(b));
    std::string text(4096, '\0');
    for (char& byte : text) byte = static_cast<char>(random() % 256);
    for (unsigned i = 100; i < 2000; ++i)
      text[i] = static_cast<char>(allowed[random() % allowed.size()]);
    Reading reading(lexer.tables, text);
    Machine& m = reading.machine;
    for (unsigned i = 0; i < 5000; ++i) {
      const unsigned length = 100 + random() % (text.size() - 100);
      const unsigned begin = random() % length;
      m.end = m.mark = m.src + length;
      const char* want = m.src + begin;
      while (want != m.end && in[byte_at(want)] != 0) ++want;
      CHECK(m.run_on(run, m.src + begin) == want);
    }
  }
}

void many_reads_share_a_long_successful_or_failed_suffix() {
  Lexer lexer;
  for (bool successful : {false, true}) {
    std::string text;
    for (unsigned i = 0; i < 2048; ++i) text += "a ";
    if (successful) text += '!';
    Reading reading(lexer.tables, text);
    Machine& m = reading.machine;
    scanned = 0;
    for (std::size_t at = 0; at < 2048; at += 2) {
      const Token token = m.walk(m.src + at, 0, 1, m.src + at);
      CHECK(token.begin(m) == at);
      CHECK(token.terminal() == (successful ? 2 : 1));
      CHECK(token.end == (successful ? m.end : m.src + at + 1));
    }
    CHECK(scanned < 2 * text.size());
  }
}

void matcher_suffixes_share_the_furthest_end_until_the_cut_changes() {
  Lexer lexer;
  lexer.tables.match = [](Machine& m, Matching& s) {
    while (s.runs()) {
      const auto th = s.take();
      const char* p = m.src + th.at;
      const char* limit = Matching::stride(m, p);
      for (;;) {
        s.end(th.call, static_cast<TextOffset>(p - m.src));
        if (p >= limit && (m.observe_end(p == m.end) || !s.passes(m, th, 1, p, limit))) break;
        ++scanned;
        if (*p++ != 'a') break;
      }
    }
  };
  const std::string text = std::string(4096, 'a') + "!more";
  Reading reading(lexer.tables, text);
  Machine& m = reading.machine;
  for (const unsigned length : {4097u, 1030u, 4097u}) {
    m.read_to(m.src + length, lexer.tables.terminalCount);
    const auto matched = std::min(length, 4096u);
    scanned = 0;
    for (unsigned at = 0; at < matched; at += kQuietSpan) {
      const auto result = m.on_text([&] { return m.match(0, m.src + at) == m.src + matched; });
      CHECK(result.value && result.atCut == (matched == length));
    }
    CHECK(scanned <= 2 * length);
  }
}

void a_short_read_keeps_no_point() {
  Lexer lexer;
  const std::string text = "aaa!";
  Reading reading(lexer.tables, text);
  Machine& m = reading.machine;
  scanned = 0;
  CHECK(m.run_on(0, m.src) == m.src + 3);
  const auto once = scanned;
  CHECK(m.run_on(0, m.src) == m.src + 3);
  CHECK(once != 0 && scanned == 2 * once);
}

void a_match_can_end_before_its_last_checkpoint() {
  Lexer lexer;
  lexer.tables.match = [](Machine& m, Matching& s) {
    while (s.runs()) {
      const auto th = s.take();
      const char* p = m.src + kQuietSpan;
      const char* limit = nullptr;
      if (s.passes(m, th, 1, p, limit) && *p == '!')
        s.end(th.call, static_cast<TextOffset>(p + 1 - m.src - 4));
    }
  };
  const std::string text = std::string(kQuietSpan, 'a') + '!';
  Reading reading(lexer.tables, text);
  auto& m = reading.machine;
  for (int i = 0; i < 2; ++i)
    CHECK(m.match(0, m.src) == m.src + kQuietSpan - 3);
}

void a_scanner_end_does_not_belong_to_a_checkpoint_it_jumped_over() {
  Lexer lexer;
  lexer.tables.match = [](Machine& m, Matching& s) {
    while (s.runs()) {
      const auto th = s.take();
      if (th.at == 0) s.end(th.call, 2 * kQuietSpan, true);
      const char* limit = nullptr;
      s.passes(m, th, 1, m.src + kQuietSpan, limit);
    }
  };
  const std::string text(2 * kQuietSpan, 'a');
  Reading reading(lexer.tables, text);
  auto& m = reading.machine;
  CHECK(m.match(0, m.src) == m.end);
  CHECK(m.match(0, m.src + kQuietSpan) == nullptr);
}

void context_dependent_tokens_keep_the_normal_matcher() {
  Lexer lexer;
  const std::string text = std::string(4096, 'a') + " aa!";
  Reading reading(lexer.tables, text);
  Machine& m = reading.machine;
  for (const unsigned mode : {0u, 1u, 2u, 1u, 0u}) {
    const Token token = m.walk(m.src, mode, 1, m.src);
    CHECK(token.begin(m) == 0);
    CHECK(token.terminal() == (mode == 1 ? 1 : 2));
    CHECK(token.end == (mode == 1 ? m.src + 4096 : m.end));
  }
}

void sessions_never_reuse_another_parse() {
  Lexer lexer;
  std::string text(4096, 'a');
  text += " !";
  {
    Reading parent(lexer.tables, text);
    CHECK(parent.machine.run_on(0, parent.machine.src) ==
          parent.machine.end - 1);
    const std::string other(4096, 'b');
    Reading child(lexer.tables, other);
    CHECK(child.machine.run_on(0, child.machine.src) == child.machine.src);
    CHECK(parent.machine.run_on(0, parent.machine.src) ==
          parent.machine.end - 1);
  }
  text[200] = '!';
  Reading reading(lexer.tables, text);
  CHECK(reading.machine.run_on(0, reading.machine.src) ==
        reading.machine.src + 200);
}

void cached_runs_follow_shorter_and_longer_input_limits() {
  Lexer lexer;
  const std::string text =
      std::string(1024, 'a') + '!' + std::string(1024, 'a');
  Reading reading(lexer.tables, text);
  Machine& m = reading.machine;
  for (const unsigned length : {128u, 512u, 1024u, 64u, 2049u, 100u, 2049u}) {
    m.end = m.mark = m.src + length;
    for (const unsigned at : {0u, 10u, 40u})
      CHECK(m.run_on(0, m.src + at) == m.src + std::min(length, 1024u));
  }
  CHECK(m.run_on(1, m.src) == m.src);
  scanned = 0;
  Reading growing(lexer.tables, text);
  for (unsigned length = 64; length <= 1024; length += 64) {
    growing.machine.end = growing.machine.mark = growing.machine.src + length;
    CHECK(growing.machine.run_on(0, growing.machine.src) ==
          growing.machine.end);
  }
  CHECK(scanned == 1024);
}

void cached_and_uncached_runs_read_the_stop() {
  Lexer lexer;
  const std::string text(2 * kWatchedBytes, 'a');
  for (bool cached : {false, true}) {
    CancellationTokenSource source;
    Reading reading(lexer.tables, text, source.get_token());
    Machine& m = reading.machine;
    if (cached) CHECK(m.run_on(0, m.src) == m.end);
    source.request_stop();
    m.mark = m.src;
    scanned = 0;
    bool cancelled = false;
    try {
      (void)m.run_on(0, m.src);
    } catch (const OperationCancelled&) {
      cancelled = true;
    }
    CHECK(cancelled && scanned == 0);
  }
}

void a_forced_reread_stops_at_the_next_grammar_choice() {
  Lexer lexer;
  lexer.tables.explore = [](Explorer& x) {
    return x.resume == 3 ? x.split(x.tok, 4, 2) : x.refused(x.tok);
  };
  Reading input(lexer.tables, "a");
  auto& m = input.machine;
  m.split(token(0, m.end, 1), 2, 2, 1);
  CHECK(arm(m) == 3 && m.plan.size() == 1);
}

void a_lexical_fallback_cannot_elect_its_own_cycle() {
  Lexer lexer;
  lexer.tables.explore = [](Explorer& x) {
    return x.resume == 2 ? x.refused(x.tok) : x.split(x.tok, 2, 2, 1);
  };
  Reading input(lexer.tables, "a");
  auto& m = input.machine;
  m.split(token(0, m.end, 1), 2, 2, 1);
  CHECK(arm(m) == kNoStack);
}

void a_split_keeps_the_bytes_consumed_by_a_repair() {
  Lexer lexer;
  Reading reading(lexer.tables, "abc");
  auto& m = reading.machine;
  m.at = m.src + 2;
  m.hide(0, m.at, kSkippedElement);
  CHECK(m.split(token(2, m.end, 1), 4, 2) == Exit::split);
  CHECK(m.at == m.src + 2);
}

void refused_readings_keep_the_preferred_derivation() {
  Lexer lexer;
  constexpr std::uint32_t empty[]{0, 0}, order[]{0, 4};
  lexer.tables.productionSize = empty;
  lexer.tables.productionOrder = order;
  lexer.tables.explore = [](Explorer& x) {
    x.push(1, x.resume - 2, x.tok);
    return x.refused(x.tok);
  };
  Reading reading(lexer.tables, "a");
  auto& m = reading.machine;
  m.split(token(0, m.end, 1), 2, 2);
  CHECK(arm(m) == 2);
}

void consecutive_splits_survive_a_forest_election() {
  Lexer lexer;
  constexpr std::uint32_t empty[]{0, 0, 0}, order[]{0, 4, 8};
  lexer.tables.productionSize = empty;
  lexer.tables.productionOrder = order;
  lexer.tables.explore = [](Explorer& x) {
    if (x.resume == 2) return x.split(x.tok, 4, 2);
    x.push(1, x.resume == 4 ? 0 : x.resume == 5 ? 1 : 2, x.tok);
    return Exit::accepted;
  };
  Reading input(lexer.tables, "a");
  auto& m = input.machine;
  m.split(token(0, m.end, 1), 2, 2);
  CHECK(arm(m) == 2);
  CHECK((m.plan == std::vector<std::uint32_t>{2, 4}));
}

void an_election_compares_each_ambiguous_child_before_the_last_decides() {
  Lexer lexer;
  constexpr unsigned width = 256;
  constexpr std::uint32_t sizes[]{width, 0, 0, 0}, order[]{0, 4, 8, 12};
  lexer.tables.productionSize = sizes;
  lexer.tables.productionOrder = order;
  lexer.tables.explore = [](Explorer& x) {
    for (unsigned i = 0; i < width; ++i) {
      const auto parent = x.node;
      const auto preferred = i + 1 == width && x.resume == 3 ? 2u : 1u;
      x.stacks.push(x.resume, parent, i, 0, 0, {.production = preferred}, 0);
      x.node = x.stacks.push(x.resume, parent, i, 0, 0, {.production = 3}, 0).node;
    }
    x.push(1, x.pop(width, 0), x.tok);
    return Exit::accepted;
  };
  Reading reading(lexer.tables, "a");
  auto& m = reading.machine;
  m.split(token(0, m.end, 1), 2, 2);
  CHECK(arm(m) == 2);
}

void lexical_probes_charge_their_actions_and_keep_all_fallbacks() {
  Lexer lexer;
  lexer.tables.explore = [](Explorer& x) {
    if (x.resume == 2) return x.split(x.tok, 10, 2);
    if (x.resume == 10) return x.refused(x.tok);
    CHECK(x.resume == 11);
    x.shift(1, 0, x.tok);
    return Exit::shifted;
  };
  Reading input(lexer.tables, "a");
  auto& m = input.machine;
  Repairer writer(m, true);
  writer.split(token(0, m.end, 1), 2, 4, 3);
  CHECK(m.rereads == 3);
  resolve(m, {m.frames, m.top}, m.at, m.tok, m.indented, m.arms, m.armCount, m.rereads, m.plan);
  CHECK(m.explored == 4 && m.plan.size() == 1 && m.plan.front() == 2);
}

void prefix_predictions_require_a_unique_shift() {
  Lexer lexer;
  Reading input(lexer.tables, "a");
  auto& m = input.machine;
  m.split(token(0, m.end, 1), 2, 2);
  const Reader read = [](Machine& m, const char*) { return m.tok; };
  const std::uint8_t absent[]{0, 0}, unknown[]{0, 1}, shifts[]{0, 2};
  PrefixArm arms[]{{read, unknown}, {read, absent}};
  CHECK(predict_prefix(m, arms).arm == kNoStack);
  arms[0].shifts = absent;
  CHECK(predict_prefix(m, arms).arm == kNoStack);
  arms[1].shifts = shifts;
  CHECK(predict_prefix(m, arms).arm == 3);
  const Reader grouped = [](Machine& m, const char*) { return m.grouped(m.tok, {1, 2}); };
  arms[0].read = grouped;
  CHECK(predict_prefix(m, arms).arm == kNoStack);
  arms[0].read = read;
  lexer.modes[0].read = grouped;
  const std::array<std::uint16_t, 2> through[]{{0, 1}};
  const PrefixPath path{{}, through};
  CHECK(predict_prefix(m, arms, &path).arm == kNoStack);

  Reading longer(lexer.tables, "abc");
  auto& many = longer.machine;
  many.split(token(0, many.end, 1), 7, 3);
  const Reader shortWord = [](Machine& m, const char*) { return token(0, m.src + 1, 1); };
  const Reader longWord = [](Machine& m, const char*) { return token(0, m.end, 1); };
  PrefixArm rivals[]{{shortWord, shifts}, {shortWord, shifts}, {longWord, shifts}};
  CHECK(predict_prefix(many, rivals).arm == 9);
  std::swap(rivals[0], rivals[2]);
  CHECK(predict_prefix(many, rivals).arm == 7);
  rivals[1].read = longWord;
  CHECK(predict_prefix(many, rivals).arm == kNoStack);
}

void a_typed_reread_returns_the_longest_eligible_shift() {
  Lexer lexer;
  lexer.tables.explore = [](Explorer& x) {
    const auto length = x.resume % 2 + 1;
    x.shift(1, 0, token(0, x.text.src + length, 1));
    return Exit::shifted;
  };
  Reading input(lexer.tables, "aa");
  auto& m = input.machine;
  Cactus stacks;
  stacks.clear({m.frames, m.top});
  const cursiv::engine::Reading from{0, 0, m.src, token(0, m.src + 1, 2)};
  for (unsigned first : {2u, 3u}) {
    Token shifted{};
    CHECK(takes(m, stacks, from, first, 2, 1, &shifted));
    CHECK(shifted.terminal() == 1 && shifted.end == m.end);
  }
}

void predictions_restore_the_stack_across_a_reduced_prefix() {
  Lexer lexer;
  static std::vector<std::vector<std::uint32_t>> states;
  states.clear();
  lexer.tables.drive = [](Machine& m) {
    if (m.at == m.end) return m.refused(m.tok);
    const auto i = static_cast<std::uint32_t>(m.at - m.src);
    m.make_room();
    auto open = m.tape.records();
    if (i == 100) open = m.pop(80);
    m.tape.leaf(i, i + 1, 0);
    m.push(i + 1, open);
    m.make_room();
    m.push(0, m.tape.records());
    auto& expected = states.emplace_back();
    for (const Frame* f = m.frames; f != m.top; ++f) expected.push_back(f->state);
    ++m.at;
    return m.split(token(i, m.at, 1), 2, 2, 1);
  };
  lexer.tables.predict = [](Machine&) { return Prediction{2}; };
  lexer.tables.explore = [](Explorer& x) {
    const auto& expected = states.at(x.tok.begin(x.text));
    CHECK(x.stacks.base() == expected.size());
    for (std::uint32_t i = 0; i < x.stacks.base(); ++i)
      CHECK(x.stacks.state(i) == expected.at(i));
    return Exit::accepted;
  };
  const std::string text(128, 'a');
  Reading input(lexer.tables, text);
  bool retry = true;
  CHECK(run(input.machine, &retry) == Exit::refused && !retry);
}

void lexical_refusals_expire_only_after_real_shifts() {
  Lexer lexer;
  lexer.tables.predict = [](Machine& m) {
    static constexpr std::uint8_t shifts[]{0, 2, 0, 0};
    const PrefixArm arms[]{
        {[](Machine& m, const char* p) { return token(p - m.src, p + 1, m.t.terminalCount); }, shifts},
        {[](Machine& m, const char* p) { return token(p - m.src, p + 1, 1); }, shifts}};
    return predict_prefix(m, arms);
  };
  lexer.tables.drive = [](Machine& m) {
    if (!m.resume) return m.split(token(0, m.src + 1, 1), 2, 2);
    for (TextOffset i = m.at - m.src; m.src + i != m.end; ++i) {
      if (m.src[i] == '?' && i != m.tok.begin(m)) return m.split(token(i, m.src + i + 1, 1), 6, 2);
      m.tape.leaf(i, i, 0);
      if (m.src[i] == ' ') m.tape.hidden(i, i + 1, 0);
      else m.tape.leaf(i, i + 1, 0);
    }
    return m.refused(token(m.end - m.src, m.end, 0));
  };
  lexer.tables.explore = [](Explorer& x) {
    auto p = x.at;
    while (p != x.text.end && *p == ' ') ++p;
    const auto i = static_cast<TextOffset>(p - x.text.src);
    if (p == x.text.end || x.resume == 8) return x.refused(token(i, p, 0));
    if (*p == '?' && x.resume == 10) return x.split(token(i, p + 1, 1), 6, 2);
    x.shift(x.resume == 2 || x.resume == 6 ? 4 : 5, 0, token(i, p + 1, 1));
    return Exit::shifted;
  };
  std::string chain = "ab";
  for (unsigned i = 0; i < 4096; ++i) chain += "?b";
  chain += "cd";
  for (const std::string_view source : std::initializer_list<std::string_view>{"ab  c", "ab  cd", "ab?b  c", chain}) {
    Reading input(lexer.tables, source);
    bool retry = false;
    CHECK(run(input.machine, &retry) == Exit::refused);
    CHECK(retry == (source.back() == 'c'));
    CHECK((input.machine.explored == 0) == !retry);
  }
}

void only_divergent_predictions_restart_before_repair() {
  Lexer lexer;
  static std::array<unsigned, 3> predictions;
  lexer.tables.drive = [](Machine& m) {
    const auto first = (m.resume & ~1u) + 2;
    if (first > 6) return m.refused(m.tok);
    const auto at = (first - 2) / 2;
    m.make_room();
    m.tape.leaf(at, at + 1, 0);
    m.at = m.src + at + 1;
    return m.split(token(at, m.at, 1), first, 2, 1);
  };
  lexer.tables.explore = [](Explorer& x) { return x.refused(x.tok); };
  lexer.tables.predict = [](Machine& m) {
    ++predictions[(m.arms - 2) / 2];
    if (m.arms == 4) {
      constexpr std::uint8_t unknown[]{0, 1};
      const PrefixArm arm{[](Machine& m, const char*) { return m.tok; }, unknown};
      return predict_prefix(m, {&arm, 1});
    }
    const bool diverges = m.arms == 2 ? *m.src == 'b' : *m.src == 'c';
    return Prediction{m.arms + !diverges};
  };
  for (const std::string_view text : {"aaa", "bbb", "ccc"}) {
    Reading full(lexer.tables, text), predicted(lexer.tables, text);
    CHECK(run(full.machine) == Exit::refused);
    predictions = {};
    bool retry = false;
    CHECK(run(predicted.machine, &retry) == Exit::refused);
    CHECK((predictions == std::array{1u, 1u, 1u}));
    CHECK(retry == (text != "aaa"));
  }
}

void a_predicate_can_read_the_whole_suffix() {
  Lexer lexer;
  const std::uint16_t mode[]{kNoToken};
  lexer.tables.stateMode = mode;
  lexer.tables.explore = [](Explorer& x) {
    if (x.at == x.text.end) return Exit::accepted;
    x.taken = token(static_cast<TextOffset>(x.at - x.text.src), x.at + 1, 1);
    ++x.at;
    return Exit::shifted;
  };
  const std::string text(1'050'000, 'a');
  Reading reading(lexer.tables, text);
  auto& m = reading.machine;
  CHECK(m.reads(0, m.src, token(0, m.src + 1, 1), {}));
}

void a_predicate_leaves_a_cyclic_reread_and_tries_its_sibling() {
  Lexer lexer;
  const std::uint16_t mode[]{kNoToken};
  lexer.tables.stateMode = mode;
  lexer.tables.explore = [](Explorer& x) {
    if (x.resume == 2) return Exit::refused;
    if (x.resume == 3) return Exit::accepted;
    x.arms = 2;
    x.armCount = 3;
    x.rereads = 2;
    return Exit::split;
  };
  Reading reading(lexer.tables, "a");
  auto& m = reading.machine;
  CHECK(m.reads(0, m.src, token(0, m.end, 1), {}));
}

void readings_share_the_same_indentation_opening() {
  Lexer lexer;
  Reading reading(lexer.tables, "a:\n  b\n");
  auto& m = reading.machine;
  const auto indent = token(3, m.src + 5, 1);
  Indented first, again, muted;
  m.indent(first, indent);
  m.indent(again, indent);
  m.indent(muted, indent, true);
  CHECK(first == again && first != muted && m.levels.opened.size() == 2);
  Reading edited(lexer.tables, "a:\n  c\n");
  auto& trial = edited.machine;
  trial.levels.base = &m.levels;
  Indented shared;
  const auto blanks = token(3, trial.src + 5, 1);
  trial.indent(shared, blanks, true);
  CHECK(shared == muted && trial.levels.opened.empty());
  trial.indent(shared, blanks);
  CHECK(trial.levels.opened.size() == 1 && m.levels.opened.size() == 2);
  trial.dedent(shared);
  CHECK(shared == muted);
  trial.levels.close(shared.level, 77);
  CHECK(trial.levels.closes(shared.level, 77) && !m.levels.closes(shared.level, 77));
}

void long_token_and_trivia_scans_read_the_stop() {
  Lexer lexer;
  const std::array<std::uint8_t, 256> classes{};
  const std::uint16_t next[] = {0, 1};
  const std::uint8_t flags[] = {0, 2};
  const TextMonitor monitor{classes.data(), next, flags, 1};
  lexer.tables.monitors = &monitor;
  std::string text(kWatchedBytes + 2, ' ');
  text.back() = '\n';
  CancellationTokenSource stop;
  Reading reading(lexer.tables, text, stop.get_token());
  auto& m = reading.machine;
  CHECK(m.line_between(m.src, m.end) && m.gap_reads(0, m.src, m.end, true));
  const std::string word(kWatchedBytes + 2, 'a');
  Reading tokenReading(lexer.tables, word, stop.get_token());
  auto& tokenMachine = tokenReading.machine;
  Matching matching;
  matching.start(m, {}, false);
  for (TextOffset at = 0; at < 2048; ++at) matching.go({.at = at});
  stop.request_stop();
  const auto cancelled = [&](auto scan) {
    try {
      scan();
      CHECK(false);
    } catch (const OperationCancelled&) {}
  };
  cancelled([&] { (void)m.line_between(m.src, m.end); });
  cancelled([&] { (void)m.gap_reads(0, m.src, m.end, true); });
  cancelled([&] { (void)m.indentation(m.src, m.indented); });
  cancelled([&] { (void)tokenMachine.ending(token(0, tokenMachine.end, 1)); });
  cancelled([&] { (void)Matching::echoes(tokenMachine, {0, static_cast<TextOffset>(word.size())}, 0); });
  cancelled([&] { matching.remember(m); });
}

}  // namespace

int main() {
  guards_report_when_the_window_cuts_their_input();
  cached_suffixes_agree_with_complete_walks();
  many_reads_share_a_long_successful_or_failed_suffix();
  matcher_suffixes_share_the_furthest_end_until_the_cut_changes();
  a_match_can_end_before_its_last_checkpoint();
  a_scanner_end_does_not_belong_to_a_checkpoint_it_jumped_over();
  a_short_read_keeps_no_point();
  context_dependent_tokens_keep_the_normal_matcher();
  sessions_never_reuse_another_parse();
  cached_runs_follow_shorter_and_longer_input_limits();
  cached_and_uncached_runs_read_the_stop();
  a_forced_reread_stops_at_the_next_grammar_choice();
  a_lexical_fallback_cannot_elect_its_own_cycle();
  a_split_keeps_the_bytes_consumed_by_a_repair();
  refused_readings_keep_the_preferred_derivation();
  consecutive_splits_survive_a_forest_election();
  an_election_compares_each_ambiguous_child_before_the_last_decides();
  lexical_probes_charge_their_actions_and_keep_all_fallbacks();
  prefix_predictions_require_a_unique_shift();
  a_typed_reread_returns_the_longest_eligible_shift();
  lexical_refusals_expire_only_after_real_shifts();
  only_divergent_predictions_restart_before_repair();
  predictions_restore_the_stack_across_a_reduced_prefix();
  a_predicate_can_read_the_whole_suffix();
  a_predicate_leaves_a_cyclic_reread_and_tries_its_sibling();
  readings_share_the_same_indentation_opening();
  long_token_and_trivia_scans_read_the_stop();
  return cursiv::test::report("LexSuffixSession_test");
}
