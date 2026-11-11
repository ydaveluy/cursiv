#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/text/Position.hpp"
#include "cursiv/text/Range.hpp"
#include "cursiv/text/TextOffset.hpp"

// The text and the two units it is addressed in: byte offsets for a parse,
// lines and UTF-16 columns for a client. It owns the bytes and the table
// between them, with the LSP text document contract.

namespace cursiv {

// One notification's change: a ranged replacement, or -- range absent -- a
// text that replaces the whole document.
struct TextDocumentContentChangeEvent {
  std::optional<Range> range;
  std::string text;
};

// A replacement an editor asks for, in the client's unit.
struct TextEdit {
  Range range;
  std::string newText;
};

class TextDocument {
 public:
  TextDocument() = default;
  // A move takes the line index the source has built. The index guards
  // itself, which is what makes a document non-copyable.
  TextDocument(const TextDocument&) = delete;
  TextDocument& operator=(const TextDocument&) = delete;
  TextDocument(TextDocument&& o) noexcept
      : _uri(std::move(o._uri)),
        _languageId(std::move(o._languageId)),
        _version(o._version),
        _text(std::move(o._text)),
        _lineStarts(std::move(o._lineStarts)),
        _asciiLines(std::move(o._asciiLines)),
        _wellFormedLines(std::move(o._wellFormedLines)),
        _lineIndexReady(o._lineIndexReady.load(std::memory_order_relaxed)) {
    o.setText({});
  }
  TextDocument& operator=(TextDocument&& o) noexcept;

  // Read-only identity, and the client's version; disk content is
  // version 0. A text longer than `kMaxDocument` bytes throws
  // `std::overflow_error`.
  [[nodiscard]] static TextDocument create(std::string uri,
                                           std::string languageId,
                                           std::int64_t version,
                                           std::string text);

  // The changes apply in order, each one over the text its predecessor left.
  static TextDocument& update(
      TextDocument& document,
      std::span<const TextDocumentContentChangeEvent> changes,
      std::int64_t version);

  // The text the edits leave; `document` is not changed. Edits may touch but
  // not overlap (`std::invalid_argument`); insertions at one point keep array
  // order.
  [[nodiscard]] static std::string applyEdits(const TextDocument& document,
                                              std::span<const TextEdit> edits);

  [[nodiscard]] std::string_view uri() const noexcept { return _uri; }
  [[nodiscard]] std::string_view languageId() const noexcept {
    return _languageId;
  }
  [[nodiscard]] std::int64_t version() const noexcept { return _version; }
  [[nodiscard]] std::string_view getText() const noexcept { return _text; }
  [[nodiscard]] std::string_view getText(const Range& range) const;

  [[nodiscard]] std::uint32_t lineCount() const;

  // Clamped at both ends: an offset past the text answers the last position,
  // and one that would fall between the `\r` and the `\n` of a pair answers
  // the end of the line before it -- there is no position inside a terminator.
  [[nodiscard]] Position positionAt(TextOffset offset) const;

  // The inverse over everything `positionAt` can produce: a line past the end
  // answers the end of the text, a column past its line the end of that line.
  [[nodiscard]] TextOffset offsetAt(const Position& position) const;

  // The pair every diagnostic is published as, with `end` before `begin` read
  // as an empty range at `begin`.
  [[nodiscard]] Range rangeAt(TextOffset begin, TextOffset end) const;

  // `positionAt` over ascending offsets in one pass, for semantic tokens. It
  // starts at the first offset, so a range never walks its document's prefix.
  class Cursor {
   public:
    explicit Cursor(const TextDocument& document, TextOffset first = 0);
    [[nodiscard]] Position positionAt(TextOffset offset);
    [[nodiscard]] bool crossesLine(TextOffset offset) const noexcept;
    [[nodiscard]] Position lineEnd();
    [[nodiscard]] Position nextLine();

   private:
    [[nodiscard]] Position onLine(TextOffset at);
    void selectLine();

    const TextDocument& _document;
    std::uint32_t _line = 0;
    TextOffset _lineEnd = 0;
    TextOffset _lastOffset = 0;
    TextOffset _decodedAt = 0;
    std::uint32_t _character = 0;
    bool _ascii = false;
    bool _wellFormed = false;
  };

 private:

  void setText(std::string text);
  // Replaces `[from, to)` in place; a failure leaves the text as it was. A
  // built index is patched.
  void replaceText(std::size_t from, std::size_t to, std::string_view text);
  // Under the index's lock, after the text changed.
  void dropLineIndex();
  // Under the index's lock, after `[from, to)` of the text became `inserted`
  // bytes.
  void patchLineIndex(TextOffset from, TextOffset to, TextOffset inserted);

  [[nodiscard]] TextOffset size() const noexcept {
    return static_cast<TextOffset>(_text.size());
  }

  // Count UTF-16 units up to `to` and advance `from` to the next decoding
  // boundary. A scalar outside the BMP counts two, an ill-formed subpart one
  // U+FFFD. The boundary can be past `to` when it lies inside a subpart.
  [[nodiscard]] std::uint32_t advanceUtf16(TextOffset& from, TextOffset to,
                                         bool wellFormed) const noexcept;

  // Built on the first question, then patched by ranged changes. Read actions
  // run on many threads, hence the mutex.
  void ensureLineIndex() const;
  // Scans the lines of `[from, to)`: `from` opens a line, `to` opens one or,
  // when `last`, ends the text. Appends the later starts to `starts`, and each
  // line's flags to `ascii` and `wellFormed` from bit 0.
  void scanLines(TextOffset from, TextOffset to, bool last,
                 std::vector<TextOffset>& starts,
                 std::vector<std::uint64_t>& ascii,
                 std::vector<std::uint64_t>& wellFormed) const;

  // The offset the line's content ends at, its terminator excluded -- both
  // bytes of it when the line ends `\r\n`.
  [[nodiscard]] TextOffset lineEnd(std::uint32_t line) const noexcept;

  [[nodiscard]] std::uint32_t lineOf(TextOffset clamped) const noexcept;

  // No byte of it leads a sequence, so its UTF-16 column is its byte column
  // and neither unit has to be counted out of the other.
  [[nodiscard]] bool asciiLine(std::uint32_t line) const noexcept;
  // Decodes without a replacement: the eight-byte count is only true there.
  [[nodiscard]] bool wellFormedLine(std::uint32_t line) const noexcept;

  std::string _uri;
  std::string _languageId;
  std::int64_t _version = 0;
  std::string _text;
  mutable std::vector<TextOffset> _lineStarts;
  // One bit per line each, in line order, built with the starts.
  mutable std::vector<std::uint64_t> _asciiLines;
  mutable std::vector<std::uint64_t> _wellFormedLines;
  mutable std::mutex _lineIndexMutex;
  mutable std::atomic<bool> _lineIndexReady = false;
};

}  // namespace cursiv
