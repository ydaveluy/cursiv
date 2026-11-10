#include "cursiv/shared/gen/WrittenReads.hpp"

#include <algorithm>

#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

WrittenReads::Slot& WrittenReads::slot(std::uint32_t rule) {
  // Readying a rule may add rules to the image.
  if (rule >= _slotOf.size())
    _slotOf.resize(std::max<std::size_t>(rule + 1, _image.rules.size()),
                   kNoSlot);
  std::uint32_t& at = _slotOf[rule];
  if (at == kNoSlot) {
    at = static_cast<std::uint32_t>(_slots.size());
    _slots.push_back({.rule = rule, .readers = {}});
    _work.push_back(at);
  }
  Slot& s = _slots[at];
  if (_solving != kNoSlot &&
      (s.readers.empty() || s.readers.back() != _solving))
    s.readers.push_back(_solving);
  return s;
}

// A call reads the rule it names, unless that is a token, an operator table
// or another grammar.
bool WrittenReads::reads_into(std::uint32_t rule) {
  if (_image.rules[rule].grammar) return false;
  const ImageRuleKind kind = _image.ready(rule).face[1].kind;
  return kind != ImageRuleKind::terminal && kind != ImageRuleKind::infix &&
         kind != ImageRuleKind::grammar;
}

Descent<bool> WrittenReads::nothing_of(ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto child = [&](std::uint32_t i) {
    return _image.children[n.first + i];
  };
  if (over_operand(n.kind)) co_return co_await nothing_of(child(0));
  switch (n.kind) {
    case ImageKind::epsilon:
    case ImageKind::eof:
    case ImageKind::notOf:
    case ImageKind::andOf:
    case ImageKind::gapHas:
    case ImageKind::gapIs:
    case ImageKind::implied:
      co_return true;
    case ImageKind::seq:
    case ImageKind::unordered:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (!co_await nothing_of(child(i))) co_return false;
      co_return true;
    case ImageKind::choice:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (co_await nothing_of(child(i))) co_return true;
      co_return false;
    case ImageKind::repeat:
      if (n.m == 0) co_return true;
      co_return co_await nothing_of(child(0));
    case ImageKind::ref: {
      // An operator table reads nothing where its operand does, through each
      // table it stands on. A cycle of tables reads something: others refuse
      // it.
      std::uint32_t rule = n.a;
      for (std::size_t hop = 0;
           hop < _image.rules.size() && !_image.rules[rule].grammar; ++hop) {
        const ImageFace& face = _image.ready(rule).face[1];
        if (face.kind != ImageRuleKind::infix) break;
        rule = face.primary;
      }
      co_return reads_into(rule) ? slot(rule).nothing : false;
    }
    default:
      co_return false;
  }
}

Descent<bool> WrittenReads::run_of(ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto child = [&](std::uint32_t i) {
    return _image.children[n.first + i];
  };
  if (over_operand(n.kind) || n.kind == ImageKind::embed)
    co_return co_await run_of(child(0));
  switch (n.kind) {
    case ImageKind::repeat:
      co_return n.m < n.n&& n.n > 1;
    case ImageKind::choice:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (co_await run_of(child(i))) co_return true;
      co_return false;
    // A run among parts that read nothing; the first part that is neither
    // ends it.
    case ImageKind::seq: {
      bool run = false;
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if (co_await run_of(child(i))) {
          run = true;
        } else if (!co_await nothing_of(child(i))) {
          co_return false;
        }
      }
      co_return run;
    }
    case ImageKind::ref:
      co_return reads_into(n.a) ? slot(n.a).run : false;
    default:
      co_return false;
  }
}

bool WrittenReads::solve(std::uint32_t at) {
  const std::uint32_t rule = _slots[at].rule;
  const ImageId body = _image.ready(rule).face[1].body;
  _solving = at;
  const bool nothing = descend(nothing_of(body));
  const bool run = descend(run_of(body));
  _solving = kNoSlot;
  Slot& s = _slots[at];
  if (s.nothing == nothing && s.run == run) return false;
  s.nothing = nothing;
  s.run = run;
  return true;
}

// Only what reads a slot that moved is solved again.
void WrittenReads::solve_pending() {
  while (!_work.empty()) {
    const std::uint32_t at = _work.back();
    _work.pop_back();
    if (solve(at))
      _work.insert(_work.end(), _slots[at].readers.begin(),
                   _slots[at].readers.end());
  }
}

template <class Ask>
bool WrittenReads::answer(Ask ask) {
  for (;;) {
    const bool r = ask();
    if (_work.empty()) return r;
    solve_pending();
  }
}

bool WrittenReads::nothing(ImageId node) {
  return answer([&] { return descend(nothing_of(node)); });
}

bool WrittenReads::run(ImageId node) {
  return answer([&] { return descend(run_of(node)); });
}

}  // namespace cursiv::engine
