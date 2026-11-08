#include "cursiv/formatting/FormattingAction.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/formatting/FormattingEngine.hpp"

namespace cursiv {

FormattingRegion& FormattingRegion::prepend(const FormattingAction& action) {
  if (_collector != nullptr)
    for (const NodeId node : nodes())
      _collector->collect(node, FormattingMode::Prepend, action);
  return *this;
}

FormattingRegion& FormattingRegion::append(const FormattingAction& action) {
  if (_collector != nullptr)
    for (const NodeId node : nodes())
      _collector->collect(node, FormattingMode::Append, action);
  return *this;
}

FormattingRegion& FormattingRegion::surround(const FormattingAction& action) {
  prepend(action);
  append(action);
  return *this;
}

std::size_t FormattingRegion::normalizeIndex(
    std::ptrdiff_t index) const noexcept {
  const auto size = static_cast<std::ptrdiff_t>(nodes().size());
  if (index < 0) return static_cast<std::size_t>(std::max(size + index, {}));
  return static_cast<std::size_t>(std::min(index, size));
}

FormattingRegion FormattingRegion::slice(std::ptrdiff_t start) const {
  return slice(start, static_cast<std::ptrdiff_t>(nodes().size()));
}

FormattingRegion FormattingRegion::slice(std::ptrdiff_t start,
                                         std::ptrdiff_t end) const {
  const std::size_t from = normalizeIndex(start);
  const std::size_t to = normalizeIndex(end);
  if (from >= to) return FormattingRegion(_cst, {}, _collector);
  const std::span<const NodeId> kept = nodes().subspan(from, to - from);
  if (kept.size() == 1) return FormattingRegion(kept.front(), _cst, _collector);
  return FormattingRegion(_cst, std::vector<NodeId>(kept.begin(), kept.end()),
                          _collector);
}

std::string HiddenNodeFormatter::indentation(std::int32_t delta) const {
  const auto level = std::max<std::int64_t>(
      static_cast<std::int64_t>(_baseIndentation) + delta, 0);
  if (level > std::numeric_limits<std::int32_t>::max())
    throw std::length_error("formatting count exceeds addressable range");
  return detail::indentation_text(static_cast<std::int32_t>(level), *_options);
}

void HiddenNodeFormatter::replace(std::string text) const {
  if (text == _cst->text(_node)) return;
  _collector->replaceHidden(_node, std::move(text));
}

HiddenNodeFormatter& HiddenNodeFormatter::prepend(
    const FormattingAction& action) {
  _collector->collect(_node, FormattingMode::Prepend, action);
  return *this;
}

HiddenNodeFormatter& HiddenNodeFormatter::append(
    const FormattingAction& action) {
  _collector->collect(_node, FormattingMode::Append, action);
  return *this;
}

HiddenNodeFormatter& HiddenNodeFormatter::surround(
    const FormattingAction& action) {
  prepend(action);
  append(action);
  return *this;
}

}  // namespace cursiv
