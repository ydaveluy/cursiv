#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "asciiword/generated/Words/Table.hpp"
#include "asciiwordbytes/generated/Bytes/Table.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/engine/ast/AstBuilder.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

namespace {

using namespace std::literals;
using namespace cursiv;
using namespace cursiv::engine;

Token walked(Machine& m, std::uint16_t mode) {
  const char* p = m.src;
  for (;;) {
    if (p == m.end) return token(static_cast<TextOffset>(p - m.src), p, m.ended);
    const Token t = m.walk(p, mode, 1, p);
    if (m.t.terminalTrivia[t.terminal()] == Trivia::none) return t;
    CHECK(t.end > p);
    p = t.end;
  }
}

void same_reading(const Tables& tables, std::string_view text) {
  static constexpr std::array<std::uint8_t, 1u << 16> noRuns{};
  Tables scalar = tables;
  scalar.lexRun = noRuns.data();
  Cst a(text), b(text);
  CstBuilder writeA(a), writeB(b);
  Machine generated(tables, a.text(), writeA.stream(), kNoCancel);
  Machine reference(scalar, b.text(), writeB.stream(), kNoCancel);
  for (std::uint16_t mode = 0; mode < tables.modes.size(); ++mode) {
    if (!tables.modes[mode].read) continue;
    const Token got = tables.modes[mode].read(generated, generated.src, mode);
    const Token want = walked(reference, mode);
    const bool same = got.head == want.head && got.end - generated.src == want.end - reference.src;
    if (!same)
      std::fprintf(stderr, "mode %u, input '%.*s': reader %u, DFA %u\n", unsigned(mode),
                   static_cast<int>(std::min<std::size_t>(text.size(), 40)), text.data(),
                   unsigned(got.terminal()), unsigned(want.terminal()));
    CHECK(same);
  }
}

void generated_readers_agree_with_the_automaton() {
  const auto& words = tasciiword::generated::Words::kTables;
  const auto& bytes = tasciiword::generated::Bytes::kTables;
  for (const std::string_view text : {"", "a", "if", "ifx", "ifé", "whileβ", "WHILE",
                                      "aé!", "é", "αβ", "_123", "# café\nifé;"}) {
    same_reading(words, text);
    same_reading(bytes, text);
  }
  for (const std::string_view prefix : {"@a", "@b", "@A", "@B", "if", "~xxxxxxxxxxxxxxxxxxxx"})
    same_reading(words, std::string(prefix) + std::string(32, ' '));
  std::mt19937 random(20261118);
  constexpr std::string_view pieces[]{"a", "if", "while", "é", "β", "_", "9", "!", " ",
                                      ";", "#", "\n", "\x80", "\xc3", "\0"sv};
  for (unsigned trial = 0; trial < 4000; ++trial) {
    std::string text;
    for (unsigned size = random() % 32; size != 0; --size)
      text += pieces[random() % std::size(pieces)];
    same_reading(words, text);
    same_reading(bytes, text);
  }
  for (const std::string_view suffix : {";", "é;", "β;", "é!;", "\xc3"}) {
    const std::string text = std::string(2 * 1024 * 1024, 'a') + std::string(suffix);
    same_reading(words, text);
    same_reading(bytes, text);
  }
}

void unicode_continuations_keep_their_tokens() {
  const auto& tables = tasciiword::generated::Words::kTables;
  for (const std::string_view text : {"if; while; abc; α; ifé; whileβ; xé!;",
                                      "# café\nifé; WHILE;", ""}) {
    Cst cst;
    CHECK(parse(tables, text, cst));
    CHECK(cst.diagnostics().empty());
    auto built = AstBuilder::build(tasciiword::generated::Words::kAst, std::move(cst));
    CHECK(built.error == AstBuildError::None);
  }
}

void guarded_ties() {
  for (const std::string_view text : {":-b;", ":-c;", ": -a;", ": -b;", ": -c;", ":-a;"}) {
    Cst cst;
    CHECK(parse(tasciiword::generated::Words::kTables, text, cst));
    CHECK(cst.diagnostics().empty() == (text != ":-a;"));
  }
  for (const char letter : std::string_view("abcd")) {
    for (const char sign : std::string_view("+-?*")) {
      const std::string text = std::string("=12345") + letter + sign + ';';
      Cst cst;
      const bool clean = parse(tasciiword::generated::Words::kTables, text, cst) &&
                         cst.diagnostics().empty();
      const bool expected = sign == '+' ? letter == 'a' || letter == 'b'
                          : sign == '-' ? letter == 'b' || letter == 'c' : true;
      if (clean != expected)
        std::fprintf(stderr, "guarded lexical tie: %s\n", text.c_str());
      CHECK(clean == expected);
    }
  }
}

}  // namespace

int main() {
  generated_readers_agree_with_the_automaton();
  unicode_continuations_keep_their_tokens();
  guarded_ties();
  return cursiv::test::report("AsciiWord_test");
}
