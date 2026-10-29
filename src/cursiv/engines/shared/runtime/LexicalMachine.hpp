// The suspended steps of a recursive byte recognizer. Calls keep their
// cursors on the heap and share completed readings within one recognition.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/runtime/PredicateState.hpp"

namespace cursiv {

// Environments are immutable and interned per recognition: a branch copies
// an integer, and a repetition deduplicates equal captures.
struct LexicalCursor {
  TextOffset at = 0;
  std::uint32_t captures = 0;
  auto operator<=>(const LexicalCursor&) const = default;
};

using LexicalState = PredicateStates<LexicalCursor>;

class LexicalCaptures {
 public:
  struct Binding {
    std::uint32_t tag;
    TextOffset begin, end;
    auto operator<=>(const Binding&) const = default;
  };

  LexicalCaptures() = default;
  // `_environments` points into the keys of `_interned`: a copy would point
  // into the original, while a move takes the map's nodes along.
  LexicalCaptures(const LexicalCaptures&) = delete;
  LexicalCaptures& operator=(const LexicalCaptures&) = delete;
  LexicalCaptures(LexicalCaptures&&) = default;
  LexicalCaptures& operator=(LexicalCaptures&&) = default;

  const Binding* find(std::uint32_t environment, std::uint32_t tag) const {
    if (environment == 0) return nullptr;
    const auto& bindings = *_environments[environment - 1];
    const auto at = std::ranges::lower_bound(bindings, tag, {}, &Binding::tag);
    return at != bindings.end() && at->tag == tag ? &*at : nullptr;
  }

  std::uint32_t bind(std::uint32_t environment, std::uint32_t tag,
                     TextOffset begin, TextOffset end) {
    if (const auto* old = find(environment, tag);
        old != nullptr && old->begin == begin && old->end == end)
      return environment;
    auto bindings = environment == 0 ? std::vector<Binding>{}
                                     : *_environments[environment - 1];
    const auto at = std::ranges::lower_bound(bindings, tag, {}, &Binding::tag);
    if (at != bindings.end() && at->tag == tag)
      *at = {tag, begin, end};
    else
      bindings.insert(at, {tag, begin, end});
    if (const auto old = _interned.find(bindings); old != _interned.end())
      return old->second;
    if (_environments.size() == std::numeric_limits<std::uint32_t>::max())
      throw std::overflow_error(
          "cursiv: too many lexical capture environments");
    const auto id = static_cast<std::uint32_t>(_environments.size() + 1);
    const auto kept = _interned.emplace(std::move(bindings), id).first;
    _environments.push_back(&kept->first);
    return id;
  }

 private:
  // Each environment once, as a key of `_interned`, whose nodes never move;
  // `_environments` names them by id.
  std::map<std::vector<Binding>, std::uint32_t> _interned;
  std::vector<const std::vector<Binding>*> _environments;
};

inline PredicateState lexical_endpoints(const LexicalState& state) {
  PredicateState out;
  for (std::size_t i = 0; i < state.size(); ++i)
    out.add(PredicateCursor{state[i].at});
  return out;
}

class LexicalMachine {
 public:
  struct Frame {
    std::uint16_t node;
    LexicalState in, cur, out;
    std::uint32_t at = 0;
    std::uint32_t round = 0;
    std::uint8_t stage = 0;
  };

  template <class Step>
  LexicalState run(std::uint16_t root, LexicalState in, Step&& step) {
    _memo.clear();
    enter(root, std::move(in));
    while (!_frames.empty()) step(*this);
    return std::move(_returned);
  }

  Frame& frame() { return _frames.back(); }
  LexicalState& returned() { return _returned; }

  void enter(std::uint16_t node, LexicalState in) {
    _frames.push_back(Frame{node, std::move(in), {}, {}, 0, 0, 0});
  }
  void finish(LexicalState out) {
    _frames.pop_back();
    _returned = std::move(out);
  }

  // A call opens with no captures and only its byte endpoints escape: what it
  // reads is told by where it starts, and kept as those endpoints.
  const LexicalState* cached(std::uint16_t node, TextOffset at) const {
    const auto found = _memo.find(key(node, at));
    return found == _memo.end() ? nullptr : &found->second;
  }
  void remember(std::uint16_t node, TextOffset at, const LexicalState& out) {
    _memo.emplace(key(node, at), resume(LexicalCursor{}, out));
  }

  // A lexical call has a local environment. Only its byte endpoints escape;
  // the caller resumes with the captures it held before entering the call.
  static LexicalState resume(LexicalCursor caller, const LexicalState& ends) {
    LexicalState out;
    for (std::size_t i = 0; i < ends.size(); ++i) {
      caller.at = ends[i].at;
      out.add(caller);
    }
    return out;
  }

 private:
  static std::uint64_t key(std::uint16_t node, TextOffset at) {
    return std::uint64_t{node} << 32 | at;
  }

  std::vector<Frame> _frames;
  LexicalState _returned;
  std::map<std::uint64_t, LexicalState> _memo;
};

}  // namespace cursiv
