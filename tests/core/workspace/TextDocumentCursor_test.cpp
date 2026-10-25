#include "cursiv/core/workspace/TextDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/test/Check.hpp"

using cursiv::TextDocument;
using cursiv::TextOffset;
using Cursor = cursiv::TextDocument::Cursor;

namespace {

void matches_position_at(const std::string& text) {
  const auto document = TextDocument::create("file:///cursor", "test", 1, text);
  const auto size = static_cast<TextOffset>(text.size());
  for (const TextOffset first : {TextOffset{0}, size / 2, size}) {
    Cursor cursor(document, first);
    for (TextOffset offset = first; offset <= size; ++offset) {
      const auto expected = document.positionAt(offset);
      CHECK(cursor.positionAt(offset) == expected);
      CHECK(cursor.positionAt(offset) == expected);
    }
    CHECK(cursor.positionAt(std::numeric_limits<TextOffset>::max()) ==
          document.positionAt(std::numeric_limits<TextOffset>::max()));
  }
}

void unicode_and_line_boundaries() {
  for (const std::string_view text : {
           "",
           "ascii",
           "\n\r\r\n",
           "a\rb\r\nc\nd\r\n",
           "a\xc3\xa9\xf0\x9f\x98\x80z\r\n\xe4\xb8\xad\n",
           "\xe2\x82"
           "A\r\n\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80",
           "abcdefgh\xf0\x9f\x98\x80\xe2\x82\xacijklmnop",
       })
    matches_position_at(std::string(text));

  std::string manyLines;
  for (int i = 0; i < 140; ++i)
    manyLines += i % 3 == 0 ? "\xc3\xa9\xf0\x9f\x98\x80\r\n" : "abcdefgh\r\n";
  matches_position_at(manyLines);
}

// Include every byte offset, even inside a scalar or a malformed subpart.
// Adjacent pieces can also join into a valid sequence, so these exercise both
// the vectorised well-formed path and the replacement decoder.
void generated_texts_match_the_public_conversion() {
  constexpr std::string_view pieces[] = {"abcdefgh",
                                         "a",
                                         "\xc3\xa9",
                                         "\xe2\x82\xac",
                                         "\xf0\x9f\x98\x80",
                                         "\n",
                                         "\r\n",
                                         "\r",
                                         "\x80",
                                         "\xbf",
                                         "\xc3",
                                         "\xe2\x82",
                                         "\xf0\x9f\x98",
                                         "\xc0\x80",
                                         "\xed\xa0\x80",
                                         "\xf5",
                                         "\xff"};
  std::uint32_t state = 7;
  for (int round = 0; round < 1000; ++round) {
    std::string text;
    for (int i = 0; i < 24; ++i) {
      state = state * 1103515245U + 12345U;
      text += pieces[(state >> 16U) % std::size(pieces)];
    }
    matches_position_at(text);
  }
}

void sparse_offsets_and_independent_cursors() {
  std::string text;
  for (int i = 0; i < 10000; ++i) text += "long prefix\r\n";
  text += "\xc3\xa9\xf0\x9f\x98\x80 end\n";
  const auto document = TextDocument::create("file:///sparse", "test", 1, text);
  const auto size = static_cast<TextOffset>(text.size());
  Cursor full(document);
  Cursor range(document, size - 10);
  CHECK(full.positionAt(1) == document.positionAt(1));
  CHECK(range.positionAt(size - 10) == document.positionAt(size - 10));
  CHECK(full.positionAt(size - 7) == document.positionAt(size - 7));
  CHECK(range.positionAt(size - 7) == document.positionAt(size - 7));
  CHECK(full.positionAt(size) == document.positionAt(size));
  CHECK(range.positionAt(size) == document.positionAt(size));
}

void line_ends_and_transitions_match_the_public_conversion() {
  const std::string text = "é😀\r\n\r\nx\r最後";
  const auto document = TextDocument::create("file:///lines", "test", 1, text);
  constexpr auto pastEnd = std::numeric_limits<TextOffset>::max();
  for (const TextOffset first : {0U, 1U, 3U, 7U, 8U, 99U}) {
    Cursor cursor(document, first);
    auto position = cursor.positionAt(first);
    for (;;) {
      const auto end = document.offsetAt({position.line, pastEnd});
      CHECK(cursor.lineEnd() == document.positionAt(end));
      CHECK(cursor.lineEnd() == document.positionAt(end));
      if (position.line + 1 == document.lineCount()) {
        CHECK(!cursor.crossesLine(pastEnd));
        break;
      }
      const auto next = document.offsetAt({position.line + 1, 0});
      CHECK(!cursor.crossesLine(next - 1));
      CHECK(cursor.crossesLine(next));
      position = cursor.nextLine();
      CHECK(position == document.positionAt(next));
    }
  }
}

}  // namespace

int main() {
  unicode_and_line_boundaries();
  generated_texts_match_the_public_conversion();
  sparse_offsets_and_independent_cursors();
  line_ends_and_transitions_match_the_public_conversion();
  return cursiv::test::report("TextDocumentCursor");
}
