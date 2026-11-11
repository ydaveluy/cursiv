#include "cursiv/workspace/TextDocument.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/Position.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Utf8.hpp"

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

// The 64 line flags from line `at` on, as one word.
[[nodiscard]] std::uint64_t flags_at(const std::vector<std::uint64_t>& flags,
                                     std::size_t at) noexcept {
  const std::size_t word = at / 64;
  const std::size_t shift = at % 64;
  std::uint64_t value = word < flags.size() ? flags[word] >> shift : 0;
  if (shift != 0 && word + 1 < flags.size())
    value |= flags[word + 1] << (64 - shift);
  return value;
}

// Sets the flags of `value` from line `at` on, over cleared ones.
void put_flags(std::vector<std::uint64_t>& flags, std::size_t at,
               std::uint64_t value) noexcept {
  const std::size_t word = at / 64;
  const std::size_t shift = at % 64;
  flags[word] |= value << shift;
  if (shift != 0 && word + 1 < flags.size())
    flags[word + 1] |= value >> (64 - shift);
}

// Replaces the flags of lines `[at, at + removed)` with the first `added` of
// `fresh`, moving the later ones. Flags past the last line stay clear.
void splice_flags(std::vector<std::uint64_t>& flags, std::size_t lines,
                  std::size_t at, std::size_t removed,
                  const std::vector<std::uint64_t>& fresh, std::size_t added) {
  if (added != removed) {
    const std::size_t tail = lines - at - removed;
    std::vector<std::uint64_t> moved((tail + 63) / 64);
    for (std::size_t i = 0; i < moved.size(); ++i)
      moved[i] = flags_at(flags, at + removed + (i * 64));
    flags.resize((at + added + tail + 63) / 64);
    flags[at / 64] &= (std::uint64_t{1} << (at % 64)) - 1;
    std::fill(flags.begin() + static_cast<std::ptrdiff_t>(at / 64) + 1,
              flags.end(), 0);
    for (std::size_t i = 0; i < moved.size(); ++i)
      put_flags(flags, at + added + (i * 64), moved[i]);
  }
  for (std::size_t i = 0; i < added; ++i) {
    const std::uint64_t bit = std::uint64_t{1} << ((at + i) % 64);
    std::uint64_t& word = flags[(at + i) / 64];
    word = ((fresh[i / 64] >> (i % 64)) & 1U) != 0 ? word | bit : word & ~bit;
  }
}

}  // namespace

TextDocument& TextDocument::operator=(TextDocument&& o) noexcept {
  if (this == &o) return *this;
  _uri = std::move(o._uri);
  _languageId = std::move(o._languageId);
  _version = o._version;
  {
    const std::scoped_lock lock(_lineIndexMutex);
    _text = std::move(o._text);
    _lineStarts = std::move(o._lineStarts);
    _asciiLines = std::move(o._asciiLines);
    _wellFormedLines = std::move(o._wellFormedLines);
    _lineIndexReady.store(o._lineIndexReady.load(std::memory_order_relaxed),
                          std::memory_order_release);
  }
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
    document.replaceText(from, to, c.text);
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
    order.emplace_back(range.start, document.offsetAt(range.start),
                       document.offsetAt(range.end), e.newText);
  }
  // Ascending stable sort: two insertions at the same point stay in array
  // order. The result is then laid down front to back, every byte once.
  std::ranges::stable_sort(
      order, [](const Edit& a, const Edit& b) { return a.start < b.start; });
  const std::string_view text = document._text;
  std::string out;
  out.reserve(text.size());
  std::size_t kept = 0;
  for (const Edit& e : order) {
    // Touching is legal; overlapping edits would corrupt the original offsets.
    if (e.from < kept)
      throw std::invalid_argument("cursiv: overlapping text edits");
    out.append(text.substr(kept, e.from - kept));
    out.append(e.text);
    kept = e.to;
  }
  out.append(text.substr(kept));
  return out;
}

std::string_view TextDocument::getText(const Range& range) const {
  std::size_t from = offsetAt(range.start);
  std::size_t to = offsetAt(range.end);
  if (to < from) std::swap(from, to);
  return std::string_view(_text).substr(from, to - from);
}

// One line per byte and one more: every line number, and the next, fits in
// 32 bits.
static_assert(kMaxDocument + 1 <= std::numeric_limits<std::uint32_t>::max());

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
      at += 8;
    }
  while (at < end && units < position.character) {
    const detail::Utf8Step step = detail::utf8_step(p + at, _text.size() - at);
    units += step.units;
    at = static_cast<TextOffset>(at + step.bytes);
  }
  return at > end ? end : at;
}

Range TextDocument::rangeAt(TextOffset begin, TextOffset end) const {
  // The end is mostly on the line of the start or the next one, which the
  // cursor reaches without a second search.
  Cursor cursor(*this, begin);
  const Position from = cursor.positionAt(begin);
  return {from, end < begin ? from : cursor.positionAt(end)};
}

void TextDocument::setText(std::string text) {
  // Every offset into the text is a `TextOffset`: a longer one is refused
  // before it replaces the current one.
  if (text.size() > kMaxDocument)
    throw std::overflow_error(
        "cursiv: document exceeds the size limit, just under 4 GiB");
  // Under the same lock the index is built with: a read action runs on many
  // threads, and dropping the table is a write to it like any other.
  const std::scoped_lock lock(_lineIndexMutex);
  _text = std::move(text);
  dropLineIndex();
}

void TextDocument::replaceText(std::size_t from, std::size_t to,
                               std::string_view text) {
  if (text.size() > kMaxDocument - (_text.size() - (to - from)))
    throw std::overflow_error(
        "cursiv: document exceeds the size limit, just under 4 GiB");
  const std::scoped_lock lock(_lineIndexMutex);
  _text.replace(from, to - from, text);
  if (!_lineIndexReady.load(std::memory_order_relaxed)) return;
  // An index that cannot follow is built again on the next question.
  try {
    patchLineIndex(static_cast<TextOffset>(from), static_cast<TextOffset>(to),
                   static_cast<TextOffset>(text.size()));
  } catch (...) {
    dropLineIndex();
  }
}

// Rescans from the line holding `from` (the one before, after a `\r`) to the
// first old line start past `to`; the later lines move by the length change.
void TextDocument::patchLineIndex(TextOffset from, TextOffset to,
                                  TextOffset inserted) {
  std::vector<TextOffset>& starts = _lineStarts;
  const std::size_t lines = starts.size();
  std::size_t first = lineOf(from);
  if (first > 0 && starts[first] == from && _text[from - 1] == '\r') --first;
  const auto next = static_cast<std::size_t>(
      std::upper_bound(starts.begin() + static_cast<std::ptrdiff_t>(first),
                       starts.end(), to) -
      starts.begin());
  const auto moved = [&](TextOffset start) {
    return static_cast<TextOffset>(start - to + from + inserted);
  };
  const TextOffset end = next < lines ? moved(starts[next]) : size();
  std::vector<TextOffset> fresh;
  std::vector<std::uint64_t> ascii;
  std::vector<std::uint64_t> wellFormed;
  scanLines(starts[first], end, next == lines, fresh, ascii, wellFormed);
  const std::size_t added = fresh.size() + 1;
  const std::size_t removed = next - first;
  splice_flags(_asciiLines, lines, first, removed, ascii, added);
  splice_flags(_wellFormedLines, lines, first, removed, wellFormed, added);
  const auto at = [&](std::size_t line) {
    return starts.begin() + static_cast<std::ptrdiff_t>(line);
  };
  if (added > removed)
    starts.insert(at(next), added - removed, 0);
  else
    starts.erase(at(first + added), at(next));
  std::ranges::copy(fresh, at(first + 1));
  if (inserted != to - from)
    for (std::size_t line = first + added; line < starts.size(); ++line)
      starts[line] = moved(starts[line]);
}

void TextDocument::dropLineIndex() {
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
      at += 8;
    }
  while (at < to) {
    const detail::Utf8Step step = detail::utf8_step(p + at, _text.size() - at);
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
  scanLines(0, size(), true, _lineStarts, _asciiLines, _wellFormedLines);
  _lineIndexReady.store(true, std::memory_order_release);
}

void TextDocument::scanLines(TextOffset from, TextOffset to, bool last,
                             std::vector<TextOffset>& starts,
                             std::vector<std::uint64_t>& ascii,
                             std::vector<std::uint64_t>& wellFormed) const {
  const char* const p = _text.data();
  const TextOffset words = to >= 8 ? to - 7 : 0;
  // The high bits of what the line being filled has read.
  std::uint64_t high = 0;
  // The next "\n" or "\r" at or after `at`, with what it passed folded in: a
  // word holding neither opens no line, so the byte walk only ever finishes
  // one.
  const auto nextBreak = [&](TextOffset at) {
    for (; at < words; at += 8) {
      const std::uint64_t w = load_word(p + at);
      if ((zero_byte(w ^ (kOnes * '\n')) | zero_byte(w ^ (kOnes * '\r'))) != 0)
        break;
      high |= w & kHighs;
    }
    for (; at < to && p[at] != '\n' && p[at] != '\r'; ++at)
      high |= static_cast<unsigned char>(p[at]) & 0x80u;
    return at;
  };
  // Two bits per line, in line order: a line opens the word they go in. A
  // line with a high byte is decoded once, here, to know which walk it takes.
  std::size_t line = 0;
  TextOffset start = from;
  const auto markLine = [&](TextOffset end) {
    if (line % 64 == 0) {
      ascii.push_back(0);
      wellFormed.push_back(0);
    }
    const std::uint64_t bit = std::uint64_t{1} << (line % 64);
    if (high == 0) ascii.back() |= bit;
    if (high == 0 || detail::utf8_well_formed({p + start, end - start}))
      wellFormed.back() |= bit;
    high = 0;
    ++line;
  };
  // The LSP terminators, the "\r\n" pair counting once.
  for (TextOffset at = from;;) {
    const TextOffset brk = nextBreak(at);
    if (brk == to) break;
    at = brk + 1;
    if (p[brk] == '\r' && at < to && p[at] == '\n') ++at;
    markLine(brk);
    if (!last && at == to) return;
    starts.push_back(start = at);
  }
  markLine(to);
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

// The line is the entry before the first start past the offset. A galloping
// search from the offset's guess, then bisection, so one long line before many
// short ones stays cheap.
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
  if (const auto& starts = _document._lineStarts;
      _line + 1 < starts.size() && offset >= starts[_line + 1]) {
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
