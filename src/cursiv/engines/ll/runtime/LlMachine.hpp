// The descent's calls, kept on the heap: a step runs in place a callee that
// can finish there and hands every other call to this loop, which runs it
// before resuming the step; a callee run in place that calls in turn waits
// here too. The native stack never holds more than a step, the callee it runs
// in place and the plain rules below them, a chain the generator bounds and
// none of which reaches a rule that calls itself: how deep the text nests is
// bounded by memory, not by the stack.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"

namespace cursiv {

// What a step answers once its rule is done; any other answer is the rule it
// calls.
inline constexpr std::uint16_t kLlDone = 0xFFFFu;

using LlStep = std::uint16_t (*)(std::uint16_t rule, LlContext& c, void* frame);

class LlMachine {
 public:
  // A frame opens with its resume point, zero at the top of the rule; every
  // other field is written before the step reads it.
  template <LlStep Step>
  void run(LlContext& c, std::uint16_t entry, const std::uint32_t* bytes) {
    _bytes = bytes;
    c.machine = this;
    push(entry);
    while (_top != kNoCall) {
      Call top;
      std::memcpy(&top, _frames.get() + _top, sizeof top);
      const std::uint16_t next =
          Step(top.rule, c, _frames.get() + _top + sizeof(Call));
      if (next == kLlDone) {
        _used = _top;
        _top = top.below;
      } else {
        push(next);
      }
    }
    c.machine = nullptr;
  }

  // A callee run in place that called in turn: its frame waits here, above
  // its caller's, for the call it hands over.
  void hold(std::uint16_t rule, const void* frame, std::size_t size) {
    push(rule);
    std::memcpy(_frames.get() + _top + sizeof(Call), frame, size);
  }

 private:
  // A call lies in front of its frame, with where its caller's lies.
  struct Call {
    std::uint32_t below;
    std::uint16_t rule;
  };
  static constexpr std::uint32_t kNoCall = ~std::uint32_t{0};

  void push(std::uint16_t rule) {
    const std::size_t at = _used;
    _used += sizeof(Call) + ((_bytes[rule] + 7u) & ~std::uint32_t{7});
    if (_used > _room) grow();
    const Call call{_top, rule};
    std::memcpy(_frames.get() + at, &call, sizeof call);
    const std::uint16_t pc = 0;
    std::memcpy(_frames.get() + at + sizeof(Call), &pc, sizeof pc);
    _top = static_cast<std::uint32_t>(at);
  }

  CURSIV_NOINLINE void grow() {
    std::size_t room = _room == 0 ? 4096 : _room;
    while (room < _used) room *= 2;
    auto frames = std::make_unique_for_overwrite<std::byte[]>(room);
    if (_room != 0) std::memcpy(frames.get(), _frames.get(), _room);
    _frames = std::move(frames);
    _room = room;
  }

  std::unique_ptr<std::byte[]> _frames;
  std::size_t _used = 0, _room = 0;
  std::uint32_t _top = kNoCall;
  const std::uint32_t* _bytes = nullptr;
};

// The call of `rule`, whose frame is `F` and step `S`, run in place: its frame
// stands on the native stack while it runs, and moves to the machine only if
// it calls in turn. A rule already in its caller's place hands the call to the
// machine instead. `kLlDone` once the callee is done; otherwise the call its
// caller hands over.
template <class F, std::uint16_t (*S)(LlContext&, void*)>
CURSIV_ALWAYS_INLINE inline std::uint16_t ll_call(LlContext& c,
                                                  std::uint16_t rule) {
  if (c.nested) return rule;
  alignas(F) std::byte frame[sizeof(F)];
  const std::uint16_t pc = 0;
  std::memcpy(frame, &pc, sizeof pc);
  c.nested = true;
  const std::uint16_t next = S(c, frame);
  c.nested = false;
  if (next != kLlDone) c.machine->hold(rule, frame, sizeof(F));
  return next;
}

}  // namespace cursiv
