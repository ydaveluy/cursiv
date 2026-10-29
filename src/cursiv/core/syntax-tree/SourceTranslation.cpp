#include "cursiv/core/syntax-tree/SourceTranslation.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cursiv {
namespace {

using Piece = SourceTranslation::Piece;
using Side = TextRange Piece::*;

// `at` moved from the `From` side of the pieces to the `To` side, where
// `next` is the first piece whose `From` range ends past `at`. A point
// strictly inside a piece goes to the piece's start, or to its end for the
// end of a range.
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

// Most spans lie in one run between two pieces and move by that run's
// shift. For the others, ends never decrease along a run of records, so one
// cursor walks the pieces (and steps back for spans that break the rule, as
// repairs may); a begin is searched back from its end, rarely more than a few
// pieces away.
template <Side From, Side To, class Span>
void move(std::span<const Piece> pieces, std::span<Span> spans) noexcept {
  const Piece* const first = pieces.data();
  const Piece* const last = first + pieces.size();
  const auto past = [](const Piece& p, TextOffset at) {
    return (p.*From).end > at;
  };
  const Piece* next = first;
  // The run before `next`: a span starts at or after `runBegin` and ends
  // before `runEnd` to move by `shift`.
  TextOffset runBegin = 0;
  std::uint64_t runEnd = 0;
  TextOffset shift = 0;
  for (Span& s : spans) {
    if (s.begin >= runBegin && s.end < runEnd) {
      s.begin += shift;
      s.end += shift;
      continue;
    }
    while (next != last && !past(*next, s.end)) ++next;
    while (next != first && past(next[-1], s.end)) --next;
    const Piece* begin = next;
    for (std::ptrdiff_t step = 1;
         begin != first && past(begin[-1], s.begin); step *= 2) {
      const Piece* const probe = begin - std::min(step, begin - first);
      if (!past(*probe, s.begin)) {
        begin = std::partition_point(probe + 1, begin, [&](const Piece& p) {
          return !past(p, s.begin);
        });
        break;
      }
      begin = probe;
    }
    s.begin = across<From, To>(pieces, begin, s.begin, false);
    s.end = across<From, To>(pieces, next, s.end, true);
    // An end at an empty piece's offset is past it, not in this run.
    runBegin = next == first ? 0 : (next[-1].*From).end;
    shift = next == first ? 0 : (next[-1].*To).end - (next[-1].*From).end;
    runEnd = next == last ? std::uint64_t{1} << 32
                          : std::uint64_t{(next->*From).begin} +
                                ((next->*From).begin != (next->*From).end);
  }
}

constexpr Side kSource = &Piece::source;
constexpr Side kText = &Piece::text;

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
  _errors.push_back({.kind = DiagnosticKind::conversion_error,
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

void SourceTranslation::to_document(std::span<CstNode> records) const noexcept {
  move<kText, kSource>(_pieces, records);
}
void SourceTranslation::to_document(
    std::span<ParseDiagnostic> diagnostics) const noexcept {
  move<kText, kSource>(_pieces, diagnostics);
}
void SourceTranslation::to_text(std::span<CstNode> records) const noexcept {
  move<kSource, kText>(_pieces, records);
}
void SourceTranslation::to_text(
    std::span<ParseDiagnostic> diagnostics) const noexcept {
  move<kSource, kText>(_pieces, diagnostics);
}

}  // namespace cursiv
