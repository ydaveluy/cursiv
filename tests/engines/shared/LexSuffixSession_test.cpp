#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cursiv/engines/shared/runtime/LexWalk.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using cursiv::Scan;
using cursiv::SymbolId;
using cursiv::TextOffset;

// Mode 0 recognizes ID=[ab]+, LONG=[ab][ab >]*!, space and `>`.
// Mode 1 recognizes only `>`; mode 2 an `a` word bounded against `b`.
struct Tape {
  static constexpr bool kRefuses = false;
  std::uint64_t* steps;

  std::uint16_t start(std::uint16_t mode) const {
    return mode == 1 ? 6 : mode == 2 ? 7 : 0;
  }
  std::uint16_t step(std::uint16_t state, unsigned char b) const {
    ++*steps;
    switch (state) {
      case 0:
        return b == 'a' || b == 'b' ? 1
               : b == ' '           ? 4
               : b == '>'           ? 5
                                    : cursiv::kDeadState;
      case 1:
        if (b == 'a' || b == 'b') return 1;
        [[fallthrough]];
      case 2:
        if (b == '!') return 3;
        return b == 'a' || b == 'b' || b == ' ' || b == '>'
                   ? 2
                   : cursiv::kDeadState;
      case 4:
        return b == ' ' ? 4 : cursiv::kDeadState;
      case 6:
        return b == '>' ? 5 : cursiv::kDeadState;
      case 7:
      case 8:
        return b == 'a' ? 8 : cursiv::kDeadState;
      default:
        return cursiv::kDeadState;
    }
  }
  std::uint16_t accept(std::uint16_t state) const {
    return state == 1 || state == 8 ? 1
           : state == 3             ? 2
           : state == 4             ? 3
           : state == 5             ? 4
                                    : 0;
  }
  bool bounded(std::uint16_t state) const { return state == 8; }
  bool bound_has(std::uint16_t, const char* src, TextOffset at,
                 TextOffset len) const {
    return at < len && src[at] == 'b';
  }
  TextOffset bound_reach(std::uint16_t, const char*, TextOffset at,
                         TextOffset len) const {
    return std::min(at + 1, len);
  }
  SymbolId refused(std::uint16_t, const char*, TextOffset, TextOffset) const {
    return cursiv::kInvalidSymbol;
  }
};

// A table that refuses words shares suffixes in the modes it marks alone.
struct RefusingTape : Tape {
  static constexpr bool kRefuses = true;
  bool backtracks(std::uint16_t mode) const { return mode == 2; }
};

struct RelayTape : Tape {
  std::uint16_t relay(std::uint16_t state) const {
    return state == 2 ? 0 : cursiv::kNoRelay;
  }
};

template <class Ad>
Scan read(const Ad& tape, std::string_view text, std::uint16_t mode,
          TextOffset at, TextOffset* seen, unsigned* fallbacks = nullptr) {
  const auto len = static_cast<TextOffset>(text.size());
  return cursiv::LexSuffixSession<Ad>::match(
      tape, text.data(), len, mode, at, seen, [&] {
        if (fallbacks) ++*fallbacks;
        TextOffset halt = at;
        const auto out =
            cursiv::lex_walk(tape, text.data(), len, mode, at, halt);
        if (seen) *seen = std::max(*seen, halt < len ? halt + 1 : len);
        return out;
      });
}

void cached_suffixes_agree_with_complete_walks() {
  std::uint64_t steps = 0;
  const Tape tape{&steps};
  std::uint32_t rng = 9271;
  const auto next = [&] { return rng = rng * 1664525u + 1013904223u; };
  for (unsigned round = 0; round != 256; ++round) {
    std::string text;
    const auto size = next() % 97;
    for (unsigned i = 0; i != size; ++i) text += "ab >!?"[next() % 6];
    const auto len = static_cast<TextOffset>(text.size());
    cursiv::LexSuffixSession<Tape> session(text.data(), len);
    // Descending and ascending visits exercise memoized failed suffixes as
    // well as suffixes containing a later, longer acceptance.
    for (unsigned pass = 0; pass != 2; ++pass)
      for (TextOffset p = 0; p <= len; ++p)
        for (std::uint16_t mode = 0; mode != 3; ++mode) {
          const auto at = pass == 0 ? len - p : p;
          TextOffset halt = at;
          const auto want =
              cursiv::lex_walk_from(tape, text.data(), len, tape.start(mode),
                                    at, halt);
          TextOffset seen = 0;
          const auto got = read(tape, text, mode, at, &seen);
          CHECK(got.term == want.term && got.end == want.end);
          CHECK(seen == (halt < len ? halt + 1 : len));
        }
  }
}

void many_short_guards_share_a_long_successful_or_failed_suffix() {
  for (const bool closed : {false, true}) {
    std::string text;
    for (unsigned i = 0; i != 4096; ++i) text += "a > ";
    if (closed) text += '!';
    std::uint64_t steps = 0;
    const Tape tape{&steps};
    const auto len = static_cast<TextOffset>(text.size());
    cursiv::LexSuffixSession<Tape> session(text.data(), len);
    for (TextOffset at = 0; at + 4 <= len; at += 4) {
      TextOffset seen = 0;
      const auto out = read(tape, text, 0, at, &seen);
      CHECK(out.term == (closed ? 1 : 0));
      CHECK(out.end == (closed ? len : at + 1));
      CHECK(seen == len);
    }
    CHECK(steps < 8 * std::uint64_t{len});
  }
}

void sessions_restore_their_parent_and_never_reuse_another_parse() {
  std::uint64_t steps = 0;
  const Tape tape{&steps};
  std::string text = "a > a ?";
  const auto len = static_cast<TextOffset>(text.size());
  unsigned fallbacks = 0;
  CHECK(read(tape, text, 0, 0, nullptr, &fallbacks).term == 0);
  CHECK(fallbacks == 1);
  {
    cursiv::LexSuffixSession<Tape> session(text.data(), len);
    CHECK(read(tape, text, 0, 0, nullptr).term == 0);
    try {
      const std::string other = "a > a !";
      cursiv::LexSuffixSession<Tape> nested(other.data(), len);
      CHECK(read(tape, other, 0, 0, nullptr).term == 1);
      throw std::runtime_error("unwind the nested session");
    } catch (const std::runtime_error&) {
    }
    const auto before = steps;
    CHECK(read(tape, text, 0, 0, nullptr).term == 0);
    CHECK(steps == before);
    const std::string other = "a > a !";
    CHECK(read(tape, other, 0, 0, nullptr, &fallbacks).term == 1);
    CHECK(read(tape, std::string_view(text).substr(0, 3), 0, 0, nullptr,
               &fallbacks)
              .term == 0);
    CHECK(fallbacks == 3);
  }
  text.back() = '!';  // Same address and size, but a new public parse.
  {
    cursiv::LexSuffixSession<Tape> session(text.data(), len);
    CHECK(read(tape, text, 0, 0, nullptr).term == 1);
  }
}

void context_dependent_tokens_keep_the_normal_matcher() {
  std::uint64_t steps = 0;
  const std::string text = "a > a !";
  const auto len = static_cast<TextOffset>(text.size());
  unsigned fallbacks = 0;
  {
    const RefusingTape tape{{&steps}};
    cursiv::LexSuffixSession<RefusingTape> session(text.data(), len);
    CHECK(read(tape, text, 0, 0, nullptr, &fallbacks).term == 1);
    CHECK(fallbacks == 1);
    CHECK(read(tape, text, 2, 0, nullptr, &fallbacks).term == 0);
    CHECK(fallbacks == 1);
  }
  {
    const RelayTape tape{{&steps}};
    cursiv::LexSuffixSession<RelayTape> session(text.data(), len);
    const auto out = cursiv::LexSuffixSession<RelayTape>::match(
        tape, text.data(), len, 0, 0, nullptr, [&] {
          ++fallbacks;
          return Scan{len, 42};
        });
    CHECK(out.term == 42 && out.end == len);
    CHECK(fallbacks == 2);
  }
}

// A accepts one 'a', LONG one or more 'a' then 'b'. On a document of only
// 'a', each token is short but the failed longer reading reaches its end.
struct BackupTape {
  static constexpr bool kRefuses = false;
  std::uint64_t* steps;
  std::uint16_t start(std::uint16_t mode) const { return mode == 0 ? 0 : 4; }
  std::uint16_t step(std::uint16_t state, unsigned char byte) const {
    ++*steps;
    const std::uint16_t base = state >= 4 ? 4 : 0;
    state -= base;
    if (state == 0) return byte == 'a' ? base + 1 : cursiv::kDeadState;
    if (state == 1 || state == 2)
      return byte == 'a' ? base + 2 : byte == 'b' ? base + 3
                                                 : cursiv::kDeadState;
    return cursiv::kDeadState;
  }
  std::uint16_t accept(std::uint16_t state) const {
    if (state >= 4) state -= 4;
    return state == 1 ? 2 : state == 3 ? 3 : 0;
  }
  bool bounded(std::uint16_t) const { return false; }
  bool bound_has(std::uint16_t, const char*, TextOffset, TextOffset) const {
    return false;
  }
  TextOffset bound_reach(std::uint16_t, const char*, TextOffset at,
                         TextOffset) const {
    return at;
  }
  SymbolId refused(std::uint16_t, const char*, TextOffset, TextOffset) const {
    return cursiv::kInvalidSymbol;
  }
};

void ordinary_tokens_share_only_the_suffixes_they_backtrack_over() {
  const std::string text(8192, 'a');
  const auto len = static_cast<TextOffset>(text.size());
  std::uint64_t steps = 0;
  const BackupTape tape{&steps};
  {
    cursiv::LexSuffixSession<BackupTape> session(text.data(), len);
    for (TextOffset at = 0; at < len; ++at) {
      TextOffset halt = at;
      const auto token =
          cursiv::lex_walk(tape, text.data(), len, 0, at, halt);
      CHECK(token.term == 1 && token.end == at + 1);
      CHECK(halt == len);
    }
    CHECK(steps < 40u * std::uint64_t{len});
  }
  // A successful short walk keeps precisely the ordinary DFA work; there is
  // no extra pass to seed a cache and the caller's halt stays exact.
  const std::string shortTokens = "abababababab";
  const auto size = static_cast<TextOffset>(shortTokens.size());
  steps = 0;
  std::uint64_t plainSteps = 0;
  const BackupTape plain{&plainSteps};
  cursiv::LexSuffixSession<BackupTape> session(shortTokens.data(), size);
  // A predicate may already have filled the same suffix cache. That must
  // not move healthy ordinary tokens away from their fast DFA walk.
  const auto predicate = cursiv::LexSuffixSession<BackupTape>::match(
      tape, shortTokens.data(), size, 0, 0, nullptr, [&] {
        return Scan{0, cursiv::kInvalidSymbol};
      });
  CHECK(predicate.term == 2 && predicate.end == 2);
  steps = 0;
  for (TextOffset at = 0; at < size; at += 2) {
    TextOffset halt = at, plainHalt = at;
    const auto want = cursiv::lex_walk_from(
        plain, shortTokens.data(), size, plain.start(0), at, plainHalt);
    const auto got =
        cursiv::lex_walk(tape, shortTokens.data(), size, 0, at, halt);
    CHECK(got.term == want.term && got.end == want.end);
    CHECK(halt == plainHalt);
  }
  CHECK(steps == plainSteps);
}

// In a table that refuses words, a mode it does not mark walks plainly and
// leaves the session as it was; a marked mode shares the failed suffix.
struct RefusingBackupTape : BackupTape {
  static constexpr bool kRefuses = true;
  bool backtracks(std::uint16_t mode) const { return mode == 0; }
};

void a_refusing_table_shares_suffixes_in_its_marked_modes() {
  const std::string text(8192, 'a');
  const auto len = static_cast<TextOffset>(text.size());
  std::uint64_t steps = 0;
  const RefusingBackupTape tape{{&steps}};
  using Session = cursiv::LexSuffixSession<RefusingBackupTape>;
  Session session(text.data(), len);
  TextOffset halt = 0;
  const auto unmarked = cursiv::lex_walk(tape, text.data(), len, 1, 0, halt);
  CHECK(unmarked.term == 1 && unmarked.end == 1 && halt == len);
  CHECK(!Session::has_backtracked());
  steps = 0;
  for (TextOffset at = 0; at < len; ++at) {
    halt = at;
    const auto token = cursiv::lex_walk(tape, text.data(), len, 0, at, halt);
    CHECK(token.term == 1 && token.end == at + 1);
    CHECK(halt == len);
  }
  CHECK(steps < 40u * std::uint64_t{len});
}

void ordinary_cached_walks_preserve_modes_bounds_and_halts() {
  for (const bool closed : {false, true}) {
    std::string text;
    for (unsigned i = 0; i != 128; ++i) text += "a > ";
    if (closed) text += '!';
    const auto len = static_cast<TextOffset>(text.size());
    std::uint64_t steps = 0;
    const Tape tape{&steps};
    cursiv::LexSuffixSession<Tape> session(text.data(), len);
    for (unsigned pass = 0; pass != 2; ++pass)
      for (TextOffset p = 0; p <= len; ++p)
        for (std::uint16_t mode = 0; mode != 3; ++mode) {
          const auto at = pass == 0 ? p : len - p;
          TextOffset halt = at, plainHalt = at;
          const auto want = cursiv::lex_walk_from(
              tape, text.data(), len, tape.start(mode), at, plainHalt);
          const auto got =
              cursiv::lex_walk(tape, text.data(), len, mode, at, halt);
          CHECK(got.term == want.term && got.end == want.end);
          CHECK(halt == plainHalt);
        }
  }
}

void generated_walks_continue_from_a_state_without_losing_their_prefix() {
  const std::string text = std::string(512, 'a') + '?' +
                           std::string(512, 'a') + 'b';
  const auto len = static_cast<TextOffset>(text.size());
  std::uint64_t steps = 0;
  const BackupTape tape{&steps};
  using Session = cursiv::LexSuffixSession<BackupTape>;
  for (std::uint16_t mode = 0; mode != 2; ++mode) {
    const auto initial = tape.start(mode);
    const auto cycle = static_cast<std::uint16_t>(initial + 2);
    // Predicates alone do not activate ordinary suffix continuation.
    {
      Session predicateSession(text.data(), len);
      CHECK(Session::match(tape, text.data(), len, mode, 0, nullptr, [&] {
              return Scan{};
            }).end == 1);
      CHECK(!Session::has_backtracked());
      CHECK(!Session::continue_at(tape, text.data(), len, cycle, 2).available);
    }
    Session session(text.data(), len);
    TextOffset halt = 0;
    const auto prefix =
        cursiv::lex_walk_from(tape, text.data(), len, initial, 0, halt);
    CHECK(prefix.term == 1 && prefix.end == 1 && halt == 512);
    const auto kept =
        Session::finish(tape, text.data(), len, initial, 0, halt, prefix);
    CHECK(kept.term == prefix.term && kept.end == prefix.end);
    CHECK(Session::has_backtracked());
    const auto before = steps;
    const auto failed = Session::continue_at(tape, text.data(), len, cycle, 2);
    CHECK(failed.available && failed.best.term == cursiv::kInvalidSymbol);
    CHECK(failed.halt == 512);
    CHECK(steps - before < 16);
    const auto accepted =
        Session::continue_at(tape, text.data(), len, cycle, 515);
    CHECK(accepted.available && accepted.best.term == 2);
    CHECK(accepted.best.end == len && accepted.halt == len);
  }
}

}  // namespace

int main() {
  cached_suffixes_agree_with_complete_walks();
  many_short_guards_share_a_long_successful_or_failed_suffix();
  sessions_restore_their_parent_and_never_reuse_another_parse();
  context_dependent_tokens_keep_the_normal_matcher();
  ordinary_tokens_share_only_the_suffixes_they_backtrack_over();
  a_refusing_table_shares_suffixes_in_its_marked_modes();
  ordinary_cached_walks_preserve_modes_bounds_and_halts();
  generated_walks_continue_from_a_state_without_losing_their_prefix();
  return cursiv::test::report("LexSuffixSession_test");
}
