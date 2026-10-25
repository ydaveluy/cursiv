#include "cursiv/core/workspace/TextDocument.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/text/TextOffset.hpp"

// Nothing here is on a parse's path: a document is addressed in client units
// only when a client asks. It compiles once.

namespace cursiv {

namespace {

constexpr std::uint64_t kOnes = 0x0101010101010101ull;
constexpr std::uint64_t kHighs = 0x8080808080808080ull;

// A byte of the word is zero exactly where its own subtraction borrows into a
// high bit the byte did not carry: eight bytes tested against one value at a
// stroke.
[[nodiscard]] constexpr std::uint64_t zero_byte(std::uint64_t w) noexcept {
  return (w - kOnes) & ~w & kHighs;
}

[[nodiscard]] inline std::uint64_t load_word(const char* p) noexcept {
  std::uint64_t w = 0;
  std::memcpy(&w, p, 8);
  return w;
}

// The UTF-16 units eight bytes of well-formed UTF-8 carry: every byte that
// leads a sequence counts one, and a four-byte lead counts a second for its
// low surrogate. On ill-formed bytes it answers nothing an editor would.
[[nodiscard]] inline std::uint32_t utf16_in_word(std::uint64_t w) noexcept {
  const std::uint64_t tail = w & ~(w << 1U) & kHighs;
  const std::uint64_t wide = zero_byte((w ^ (kOnes * 0xF0U)) & (kOnes * 0xF8U));
  return 8U - static_cast<std::uint32_t>(std::popcount(tail)) +
         static_cast<std::uint32_t>(std::popcount(wide));
}

// One step of a decode that replaces, as an editor opening the file does: a
// well-formed sequence is its scalar, two units past the BMP, and each
// maximal ill-formed subpart is one U+FFFD.
struct Decoded {
  std::uint32_t units;
  std::uint32_t bytes;
  bool wellFormed;
};

[[nodiscard]] Decoded decode_one(const char* p, std::size_t left) noexcept {
  const auto lead = static_cast<unsigned char>(p[0]);
  if (lead < 0x80U) return {1, 1, true};
  std::uint32_t need = 0;
  unsigned lo = 0x80U;
  unsigned hi = 0xBFU;
  if (lead >= 0xC2U && lead <= 0xDFU) {
    need = 1;
  } else if (lead >= 0xE0U && lead <= 0xEFU) {
    need = 2;
    if (lead == 0xE0U) lo = 0xA0U;
    if (lead == 0xEDU) hi = 0x9FU;
  } else if (lead >= 0xF0U && lead <= 0xF4U) {
    need = 3;
    if (lead == 0xF0U) lo = 0x90U;
    if (lead == 0xF4U) hi = 0x8FU;
  } else {
    return {1, 1, false};
  }
  for (std::uint32_t i = 1; i <= need; ++i) {
    if (i >= left) return {1, i, false};
    const auto next = static_cast<unsigned char>(p[i]);
    if (next < lo || next > hi) return {1, i, false};
    lo = 0x80U;
    hi = 0xBFU;
  }
  return {need == 3 ? 2U : 1U, need + 1, true};
}

[[nodiscard]] bool well_formed(const char* p, std::size_t n) noexcept {
  for (std::size_t at = 0; at < n;) {
    const Decoded step = decode_one(p + at, n - at);
    if (!step.wellFormed) return false;
    at += step.bytes;
  }
  return true;
}

}  // namespace

TextDocument& TextDocument::operator=(TextDocument&& o) noexcept {
  _uri = std::move(o._uri);
  _languageId = std::move(o._languageId);
  _version = o._version;
  setText(std::move(o._text));
  o.setText({});
  return *this;
}

TextDocument TextDocument::create(std::string uri, std::string languageId,
                                  std::int64_t version, std::string text) {
  TextDocument d;
  d._uri = std::move(uri);
  d._languageId = std::move(languageId);
  d._version = version;
  d.setText(std::move(text));
  return d;
}

TextDocument& TextDocument::update(
    TextDocument& document,
    std::span<const TextDocumentContentChangeEvent> changes,
    std::int64_t version) {
  for (const TextDocumentContentChangeEvent& c : changes) {
    if (!c.range.has_value()) {
      document.setText(c.text);
      continue;
    }
    // An inverted range is straightened, never shrunk to an insertion.
    std::size_t from = document.offsetAt(c.range->start);
    std::size_t to = document.offsetAt(c.range->end);
    if (to < from) std::swap(from, to);
    std::string next(document._text);
    next.replace(from, to - from, c.text);
    document.setText(std::move(next));
  }
  document._version = version;
  return document;
}

std::string TextDocument::applyEdits(const TextDocument& document,
                                     std::span<const TextEdit> edits) {
  struct Edit {
    Position start;
    std::size_t from;
    std::size_t to;
    std::string_view text;
  };
  std::vector<Edit> order;
  order.reserve(edits.size());
  for (const TextEdit& e : edits) {
    Range range = e.range;
    if (range.end < range.start) std::swap(range.start, range.end);
    order.push_back({range.start, document.offsetAt(range.start),
                     document.offsetAt(range.end), e.newText});
  }
  // Ascending stable sort, applied back to front: earlier offsets stay valid,
  // and two insertions at the same point land in array order.
  std::stable_sort(
      order.begin(), order.end(),
      [](const Edit& a, const Edit& b) { return a.start < b.start; });
  std::string out(document._text);
  std::size_t nextStart = out.size();
  for (const Edit& e : std::views::reverse(order)) {
    // Touching is legal; overlapping edits would corrupt the original offsets.
    if (e.to > nextStart)
      throw std::invalid_argument("cursiv: overlapping text edits");
    nextStart = e.from;
    out.replace(e.from, e.to - e.from, e.text);
  }
  return out;
}

std::string_view TextDocument::getText(const Range& range) const {
  std::size_t from = offsetAt(range.start);
  std::size_t to = offsetAt(range.end);
  if (to < from) std::swap(from, to);
  return std::string_view(_text).substr(from, to - from);
}

std::uint32_t TextDocument::lineCount() const {
  ensureLineIndex();
  return static_cast<std::uint32_t>(_lineStarts.size());
}

Position TextDocument::positionAt(TextOffset offset) const {
  ensureLineIndex();
  TextOffset at = offset > size() ? size() : offset;
  const std::uint32_t line = lineOf(at);
  // Clamped to the line's content: there is no position inside a terminator.
  at = std::min(at, lineEnd(line));
  TextOffset from = _lineStarts[line];
  if (asciiLine(line)) return {line, at - from};
  return {line, advanceUtf16(from, at, wellFormedLine(line))};
}

TextOffset TextDocument::offsetAt(const Position& position) const {
  ensureLineIndex();
  if (position.line >= _lineStarts.size()) return size();
  const TextOffset begin = _lineStarts[position.line];
  const TextOffset end = lineEnd(position.line);
  if (asciiLine(position.line))
    return position.character >= end - begin
               ? end
               : static_cast<TextOffset>(begin + position.character);
  TextOffset at = begin;
  std::uint32_t units = 0;
  const char* const p = _text.data();
  // Whole words while one keeps the count at or under the column asked, and
  // only where the word ends on a boundary of a well-formed line.
  if (wellFormedLine(position.line))
    while (end - at >= 8 &&
           (static_cast<unsigned char>(p[at + 8]) & 0xC0U) != 0x80U) {
      const std::uint32_t step = utf16_in_word(load_word(p + at));
      if (units + step > position.character) break;
      units += step;
      at = static_cast<TextOffset>(at + 8);
    }
  while (at < end && units < position.character) {
    const Decoded step = decode_one(p + at, _text.size() - at);
    units += step.units;
    at = static_cast<TextOffset>(at + step.bytes);
  }
  return at > end ? end : at;
}

Range TextDocument::rangeAt(TextOffset begin, TextOffset end) const {
  const Position from = positionAt(begin);
  return {from, end < begin ? from : positionAt(end)};
}

void TextDocument::setText(std::string text) {
  // Under the same lock the index is built with: a read action runs on many
  // threads, and dropping the table is a write to it like any other.
  const std::scoped_lock lock(_lineIndexMutex);
  _text = std::move(text);
  _lineStarts.clear();
  _asciiLines.clear();
  _wellFormedLines.clear();
  _lineIndexReady.store(false, std::memory_order_release);
}

std::uint32_t TextDocument::advanceUtf16(TextOffset& from, TextOffset to,
                                       bool wellFormed) const noexcept {
  std::uint32_t units = 0;
  TextOffset at = from;
  // A whole word only where it ends on a boundary: its last byte leads
  // nothing that runs past `to`, which a continuation byte after it would say.
  const char* const p = _text.data();
  if (wellFormed)
    while (to - at >= 8 &&
           (static_cast<unsigned char>(p[at + 8]) & 0xC0U) != 0x80U) {
      units += utf16_in_word(load_word(p + at));
      at = static_cast<TextOffset>(at + 8);
    }
  while (at < to) {
    const Decoded step = decode_one(p + at, _text.size() - at);
    units += step.units;
    at = static_cast<TextOffset>(at + step.bytes);
  }
  from = at;
  return units;
}

void TextDocument::ensureLineIndex() const {
  if (_lineIndexReady.load(std::memory_order_acquire)) return;
  const std::scoped_lock lock(_lineIndexMutex);
  if (_lineIndexReady.load(std::memory_order_relaxed)) return;
  _lineStarts.clear();
  _lineStarts.push_back(0);
  _asciiLines.clear();
  _wellFormedLines.clear();
  const char* const p = _text.data();
  const TextOffset n = size();
  const TextOffset words = n >= 8 ? static_cast<TextOffset>(n - 7) : 0;
  // The high bits of what the line being filled has read.
  std::uint64_t high = 0;
  // The next "\n" or "\r" at or after `at`, with what it passed folded in: a
  // word holding neither opens no line, so the byte walk only ever finishes
  // one.
  const auto nextBreak = [&](TextOffset at) {
    for (; at < words; at = static_cast<TextOffset>(at + 8)) {
      const std::uint64_t w = load_word(p + at);
      if ((zero_byte(w ^ (kOnes * '\n')) | zero_byte(w ^ (kOnes * '\r'))) != 0)
        break;
      high |= w & kHighs;
    }
    for (; at < n && p[at] != '\n' && p[at] != '\r'; ++at)
      high |= static_cast<unsigned char>(p[at]) & 0x80u;
    return at;
  };
  // Two bits per line, in line order: a line opens the word they go in. A
  // line with a high byte is decoded once, here, to know which walk it takes.
  const auto markLine = [&](TextOffset end) {
    const std::size_t line = _lineStarts.size() - 1;
    if (line % 64 == 0) {
      _asciiLines.push_back(0);
      _wellFormedLines.push_back(0);
    }
    const std::uint64_t bit = std::uint64_t{1} << (line % 64);
    const TextOffset start = _lineStarts.back();
    if (high == 0) _asciiLines.back() |= bit;
    if (high == 0 || well_formed(p + start, end - start))
      _wellFormedLines.back() |= bit;
    high = 0;
  };
  // The LSP terminators, the "\r\n" pair counting once.
  for (TextOffset at = 0; at < n;) {
    const TextOffset brk = nextBreak(at);
    if (brk == n) break;
    at = static_cast<TextOffset>(brk + 1);
    if (p[brk] == '\r' && at < n && p[at] == '\n') ++at;
    markLine(brk);
    _lineStarts.push_back(at);
  }
  markLine(n);
  _lineIndexReady.store(true, std::memory_order_release);
}

bool TextDocument::asciiLine(std::uint32_t line) const noexcept {
  return (_asciiLines[line / 64] >> (line % 64)) & 1u;
}

bool TextDocument::wellFormedLine(std::uint32_t line) const noexcept {
  return (_wellFormedLines[line / 64] >> (line % 64)) & 1u;
}

TextOffset TextDocument::lineEnd(std::uint32_t line) const noexcept {
  const TextOffset next = line + 1 < _lineStarts.size()
                              ? _lineStarts[line + 1]
                              : static_cast<TextOffset>(_text.size());
  TextOffset end = next;
  if (end > _lineStarts[line] && _text[end - 1] == '\n') --end;
  if (end > _lineStarts[line] && _text[end - 1] == '\r') --end;
  return end;
}

// The index opens at 0, so the answer is the entry before the first start
// past the offset. Where the offset falls names the line, but a document of
// one long line and then thousands of short ones makes a walk from there
// dearer than the bisection: the stride doubles and the frame is bisected.
std::uint32_t TextDocument::lineOf(TextOffset clamped) const noexcept {
  std::size_t lo = 0;
  std::size_t hi = _lineStarts.size();
  if (hi > 2 && !_text.empty()) {
    const std::size_t guess = std::min<std::size_t>(
        hi - 1, static_cast<std::size_t>(std::uint64_t{clamped} * (hi - 1) /
                                         _text.size()));
    std::size_t step = 1;
    if (_lineStarts[guess] <= clamped) {
      lo = guess;
      while (lo + step < hi && _lineStarts[lo + step] <= clamped) {
        lo += step;
        step <<= 1;
      }
      hi = std::min(hi, lo + step);
    } else {
      hi = guess;
      while (hi > step && _lineStarts[hi - step] > clamped) {
        hi -= step;
        step <<= 1;
      }
      lo = hi > step ? hi - step : 0;
    }
  }
  const auto begin = _lineStarts.begin();
  return static_cast<std::uint32_t>(
      std::upper_bound(begin + static_cast<std::ptrdiff_t>(lo),
                       begin + static_cast<std::ptrdiff_t>(hi), clamped) -
      begin - 1);
}

TextDocument::Cursor::Cursor(const TextDocument& document, TextOffset first)
    : _document(document) {
  _document.ensureLineIndex();
  _lastOffset = std::min(first, _document.size());
  _line = _document.lineOf(_lastOffset);
  selectLine();
}

Position TextDocument::Cursor::positionAt(TextOffset offset) {
  offset = std::min(offset, _document.size());
  assert(offset >= _lastOffset);
  _lastOffset = offset;
  const auto& starts = _document._lineStarts;
  if (_line + 1 < starts.size() && offset >= starts[_line + 1]) {
    ++_line;
    // Adjacent tokens stay on this or the next line; a sparse jump searches.
    if (_line + 1 < starts.size() && offset >= starts[_line + 1])
      _line = _document.lineOf(offset);
    selectLine();
  }
  return onLine(std::min(offset, _lineEnd));
}

bool TextDocument::Cursor::crossesLine(TextOffset offset) const noexcept {
  const auto& starts = _document._lineStarts;
  return _line + 1 < starts.size() && starts[_line + 1] <= offset;
}

Position TextDocument::Cursor::lineEnd() {
  // A requested offset inside CRLF can be past the content end already.
  _lastOffset = std::max(_lastOffset, _lineEnd);
  return onLine(_lineEnd);
}

Position TextDocument::Cursor::nextLine() {
  assert(_line + 1 < _document._lineStarts.size());
  return positionAt(_document._lineStarts[_line + 1]);
}

Position TextDocument::Cursor::onLine(TextOffset at) {
  if (_ascii) return {_line, at - _document._lineStarts[_line]};
  // positionAt counts a whole scalar/replacement when the requested offset
  // lies inside it. Keep the decoder's boundary, which can be past `at`, so
  // the next offset inside that same scalar does not count it again.
  if (at > _decodedAt)
    _character += _document.advanceUtf16(_decodedAt, at, _wellFormed);
  return {_line, _character};
}

void TextDocument::Cursor::selectLine() {
  _lineEnd = _document.lineEnd(_line);
  _ascii = _document.asciiLine(_line);
  _wellFormed = _document.wellFormedLine(_line);
  _decodedAt = _document._lineStarts[_line];
  _character = 0;
}

}  // namespace cursiv
