#include "cursiv/syntax-tree/SourceTranslation.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cursiv {
namespace {

using Piece = SourceTranslation::Piece;
using Side = TextRange Piece::*;

// `at` moved from the `From` side of the pieces to the `To` side; `next` is
// the first piece whose `From` range ends past `at`. A point inside a piece
// goes to its start, or its end for the end of a range.
template <Side From, Side To>
TextOffset across(std::span<const Piece> pieces, const Piece* next,
                  TextOffset at, bool end) noexcept {
  if (next != pieces.data() + pieces.size() && (next->*From).begin < at)
    return end ? (next->*To).end : (next->*To).begin;
  if (next == pieces.data()) return at;
  const Piece& before = next[-1];
  return at - (before.*From).end + (before.*To).end;
}

template <Side From>
const Piece* after(std::span<const Piece> pieces, TextOffset at) noexcept {
  return std::upper_bound(
      pieces.data(), pieces.data() + pieces.size(), at,
      [](TextOffset a, const Piece& p) { return a < (p.*From).end; });
}

// Moves `spans` from the `From` side to the `To` side; most move by their
// run's shift. `lost(i, span)` is called for a span with a bound inside a
// piece, or at the start of an inserted one: the way back cannot place it.
template <Side From, Side To, class Span, class Lost>
void move(std::span<const Piece> pieces, std::span<Span> spans, Lost&& lost) {
  const Piece* const first = pieces.data();
  const Piece* const last = first + pieces.size();
  // The first piece that ends past `at`, sought from `from` in steps that
  // double, forward or back.
  const auto seek = [&](const Piece* from, TextOffset at) {
    const auto before = [at](const Piece& p) { return (p.*From).end <= at; };
    std::ptrdiff_t step = 1;
    if (from != last && before(*from)) {
      while (step < last - from && before(from[step])) step *= 2;
      return std::partition_point(from + step / 2 + 1,
                                  from + std::min(step, last - from), before);
    }
    while (step <= from - first && !before(from[-step])) step *= 2;
    return std::partition_point(from - std::min(step, from - first),
                                from - step / 2, before);
  };
  const Piece* next = first;
  // The run before `next`: a span starts at or after `runBegin` and ends
  // before `runEnd` to move by `shift`.
  TextOffset runBegin = 0;
  std::uint64_t runEnd = 0;
  TextOffset shift = 0;
  // Whether a bound at `at` cannot come back: inside `p`, or at the start of
  // a `p` whose other side is empty.
  const auto stranded = [&](const Piece* p, TextOffset at) {
    return p != last &&
           ((p->*From).begin < at ||
            ((p->*To).begin == (p->*To).end && (p->*From).begin == at));
  };
  for (std::size_t i = 0; i < spans.size(); ++i) {
    Span& s = spans[i];
    if (s.begin >= runBegin && s.end < runEnd) {
      s.begin += shift;
      s.end += shift;
      continue;
    }
    next = seek(next, s.end);
    const Piece* const begin = seek(next, s.begin);
    if (stranded(begin, s.begin) || stranded(next, s.end))
      lost(i, TextRange{s.begin, s.end});
    s.begin = across<From, To>(pieces, begin, s.begin, false);
    s.end = across<From, To>(pieces, next, s.end, true);
    // An end at an empty piece's offset is past it, not in this run; one at
    // the start of a piece with an empty other side is lost.
    runBegin = next == first ? 0 : (next[-1].*From).end;
    shift = next == first ? 0 : (next[-1].*To).end - (next[-1].*From).end;
    runEnd = next == last ? std::uint64_t{1} << 32
                          : std::uint64_t{(next->*From).begin} +
                                ((next->*From).begin != (next->*From).end &&
                                 (next->*To).begin != (next->*To).end);
  }
}

constexpr Side kSource = &Piece::source;
constexpr Side kText = &Piece::text;

// The spans moved back to the text, then those a piece widened set again
// from what was kept of them.
template <class Kept, class Span>
void move_back(std::span<const Piece> pieces, std::span<const Kept> kept,
               std::span<Span> spans) noexcept {
  move<kSource, kText>(pieces, spans, [](std::size_t, TextRange) {});
  for (const Kept& k : kept) {
    assert(k.index < spans.size() && "the spans of the bound tree");
    if (k.index >= spans.size()) continue;
    spans[k.index].begin = k.text.begin;
    spans[k.index].end = k.text.end;
  }
}

}  // namespace

// Offsets narrow here: the translating parser takes no document past 4 GiB.
void SourceTranslation::replace(std::size_t begin, std::size_t end,
                                std::string_view spelling) {
  assert(_kept <= begin && begin <= end && end <= _source.size() &&
         "replacements follow the source in order");
  if (_pieces.empty()) _text.reserve(_source.size());
  _text.append(_source.substr(_kept, begin - _kept));
  const auto at = static_cast<TextOffset>(_text.size());
  _text.append(spelling);
  _pieces.push_back({{static_cast<TextOffset>(begin),
                      static_cast<TextOffset>(end)},
                     {at, static_cast<TextOffset>(_text.size())}});
  _kept = static_cast<TextOffset>(end);
}

void SourceTranslation::error(std::size_t begin, std::size_t end,
                              std::string message) {
  _errors.push_back({.kind = DiagnosticKind::ConversionError,
                     .begin = static_cast<TextOffset>(begin),
                     .end = static_cast<TextOffset>(end),
                     .message = std::move(message)});
}

void SourceTranslation::finish() {
  _text.append(_source.substr(_kept));
  _kept = static_cast<TextOffset>(_source.size());
}

TextRange SourceTranslation::original(TextRange range) const noexcept {
  const auto at = [&](TextOffset offset, bool end) {
    return across<kText, kSource>(_pieces, after<kText>(_pieces, offset),
                                  offset, end);
  };
  return {at(range.begin, false), at(range.end, true)};
}

std::optional<TextOffset> SourceTranslation::translated(
    TextOffset offset) const noexcept {
  const Piece* const next = after<kSource>(_pieces, offset);
  if (next != _pieces.data() + _pieces.size() && next->source.begin < offset)
    return std::nullopt;
  return across<kSource, kText>(_pieces, next, offset, false);
}

void SourceTranslation::toDocument(
    std::span<ParseDiagnostic> diagnostics) const noexcept {
  move<kText, kSource>(_pieces, diagnostics, [](std::size_t, TextRange) {});
}
void SourceTranslation::bind(std::span<CstNode> records,
                             std::span<ParseDiagnostic> repairs) {
  const auto keep = [](std::vector<Kept>& into) {
    into.clear();
    return [&into](std::size_t i, TextRange text) {
      into.push_back({static_cast<std::uint32_t>(i), text});
    };
  };
  move<kText, kSource>(_pieces, records, keep(_records));
  move<kText, kSource>(_pieces, repairs, keep(_repairs));
}
TextRange SourceTranslation::textOf(std::uint32_t index,
                                    TextRange span) const noexcept {
  if (const auto kept = std::ranges::partition_point(
          _records, [index](const Kept& k) { return k.index < index; });
      kept != _records.end() && kept->index == index)
    return kept->text;
  move<kSource, kText>(_pieces, std::span(&span, 1),
                       [](std::size_t, TextRange) {});
  return span;
}
void SourceTranslation::toText(std::span<CstNode> records) const noexcept {
  move_back(_pieces, std::span<const Kept>(_records), records);
}
void SourceTranslation::toText(
    std::span<ParseDiagnostic> repairs) const noexcept {
  move_back(_pieces, std::span<const Kept>(_repairs), repairs);
}

}  // namespace cursiv
