// The descent's calls, kept on the heap: a rule's step hands back the rule it
// calls with its own frame saying where it resumes, and this loop runs the
// callee before resuming it. Nesting is bounded by memory, not by the stack.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace cursiv {

struct LlContext;

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
    push(entry, bytes);
    while (!_calls.empty()) {
      const Call top = _calls.back();
      const std::uint16_t next = Step(top.rule, c, _frames.get() + top.at);
      if (next == kLlDone) {
        _used = top.at;
        _calls.pop_back();
      } else {
        push(next, bytes);
      }
    }
  }

 private:
  struct Call {
    std::uint16_t rule;
    std::uint32_t at;
  };

  void push(std::uint16_t rule, const std::uint32_t* bytes) {
    const std::size_t at = _used;
    _used += (bytes[rule] + 7u) & ~std::uint32_t{7};
    if (_used > _room) grow();
    const std::uint16_t top = 0;
    std::memcpy(_frames.get() + at, &top, sizeof top);
    _calls.push_back({rule, static_cast<std::uint32_t>(at)});
  }

  void grow() {
    std::size_t room = _room == 0 ? 4096 : _room;
    while (room < _used) room *= 2;
    auto frames = std::make_unique_for_overwrite<std::byte[]>(room);
    if (_room != 0) std::memcpy(frames.get(), _frames.get(), _room);
    _frames = std::move(frames);
    _room = room;
  }

  std::unique_ptr<std::byte[]> _frames;
  std::size_t _used = 0, _room = 0;
  std::vector<Call> _calls;
};

}  // namespace cursiv
