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

struct RefusingTape : Tape {
  static constexpr bool kRefuses = true;
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
              cursiv::lex_walk(tape, text.data(), len, mode, at, halt);
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

}  // namespace

int main() {
  cached_suffixes_agree_with_complete_walks();
  many_short_guards_share_a_long_successful_or_failed_suffix();
  sessions_restore_their_parent_and_never_reuse_another_parse();
  context_dependent_tokens_keep_the_normal_matcher();
  return cursiv::test::report("LexSuffixSession_test");
}
