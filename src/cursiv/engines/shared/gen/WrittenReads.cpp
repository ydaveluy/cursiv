#include "cursiv/engines/shared/gen/WrittenReads.hpp"

namespace cursiv {

WrittenReads::Slot& WrittenReads::slot(std::uint32_t rule) {
  for (Slot& s : _slots)
    if (s.rule == rule) return s;
  _slots.push_back({rule});
  ++_moved;
  return _slots.back();
}

// A call reads the rule it names, unless that is a token, an operator table
// or another grammar.
bool WrittenReads::reads_into(std::uint32_t rule) {
  if (_image.rules[rule].grammar) return false;
  const ImageRuleKind kind = _image.ready(rule).face[1].kind;
  return kind != ImageRuleKind::terminal && kind != ImageRuleKind::infix &&
         kind != ImageRuleKind::grammar;
}

bool WrittenReads::nothing_of(ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto kid = [&](std::uint32_t i) { return _image.kids[n.first + i]; };
  if (over_operand(n.kind)) return nothing_of(kid(0));
  switch (n.kind) {
    case ImageKind::epsilon:
    case ImageKind::eof:
    case ImageKind::notOf:
    case ImageKind::andOf:
    case ImageKind::gapHas:
    case ImageKind::gapIs:
    case ImageKind::implied:
      return true;
    case ImageKind::seq:
    case ImageKind::unordered:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (!nothing_of(kid(i))) return false;
      return true;
    case ImageKind::choice:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (nothing_of(kid(i))) return true;
      return false;
    case ImageKind::repeat:
      return n.m == 0 || nothing_of(kid(0));
    case ImageKind::ref:
      return reads_into(n.a) ? slot(n.a).nothing : false;
    default:
      return false;
  }
}

bool WrittenReads::run_of(ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto kid = [&](std::uint32_t i) { return _image.kids[n.first + i]; };
  if (over_operand(n.kind) || n.kind == ImageKind::embed) return run_of(kid(0));
  switch (n.kind) {
    case ImageKind::repeat:
      return n.m < n.n && n.n > 1;
    case ImageKind::choice:
      for (std::uint32_t i = 0; i < n.count; ++i)
        if (run_of(kid(i))) return true;
      return false;
    // A run among parts that read nothing; the first part that is neither
    // ends it.
    case ImageKind::seq: {
      bool run = false;
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if (run_of(kid(i))) {
          run = true;
        } else if (!nothing_of(kid(i))) {
          return false;
        }
      }
      return run;
    }
    case ImageKind::ref:
      return reads_into(n.a) ? slot(n.a).run : false;
    default:
      return false;
  }
}

void WrittenReads::solve(std::size_t at) {
  const std::uint32_t rule = _slots[at].rule;
  const ImageId body = _image.ready(rule).face[1].body;
  const bool nothing = nothing_of(body);
  const bool run = run_of(body);
  Slot& s = _slots[at];
  if (s.nothing == nothing && s.run == run) return;
  s.nothing = nothing;
  s.run = run;
  ++_moved;
}

void WrittenReads::settle() {
  for (auto before = _moved;; before = _moved) {
    for (std::size_t i = 0; i < _slots.size(); ++i) solve(i);
    if (_moved == before) return;
  }
}

template <class Ask>
bool WrittenReads::answer(Ask ask) {
  for (;;) {
    const auto before = _moved;
    const bool r = ask();
    if (_moved == before) return r;
    settle();
  }
}

bool WrittenReads::nothing(ImageId node) {
  return answer([&] { return nothing_of(node); });
}

bool WrittenReads::run(ImageId node) {
  return answer([&] { return run_of(node); });
}

}  // namespace cursiv
