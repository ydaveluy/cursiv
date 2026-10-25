// What electing between two look-ahead readings' derivations works in: their
// subtrees as nodes, built once per step of the cell's trail and walked in the
// order the rule reads, and the verdicts kept across cells.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

struct LrTrailRoom {
  static constexpr std::uint32_t kNone = ~std::uint32_t{0};
  // A subtree, as the events of `events[events, events + size)` in the
  // order the rule reads them, and the `owed` words after them its parent
  // places.
  struct Node {
    std::uint32_t tag;
    std::uint32_t sym;
    std::uint32_t events;
    std::uint32_t size;
  };
  // A frame a reading pushed over the stack the cell found, the frame under
  // it, and how many of the cell's frames are left under both.
  struct Frame {
    std::uint32_t node;
    std::uint32_t below;
    std::uint32_t kept;
  };
  struct Walk {
    std::uint32_t node;
    std::uint32_t at;
  };
  // A verdict already read, by the paths of the two readings and the state
  // they stand in, and the frames of its cell's stack it read: how many, and
  // the symbols they stand for.
  struct Memo {
    static constexpr std::int8_t kFree = -128;
    std::uint64_t left = 0;
    std::uint64_t right = 0;
    std::uint32_t top = 0;
    std::uint32_t depth = 0;
    std::uint32_t exactAt = 0;
    std::uint32_t exactSize = 0;
    std::int8_t first = kFree;
  };

  // A new cell: its steps are new.
  void restart() {
    nodes.clear();
    events.clear();
    frames.clear();
    frameOf.clear();
    work = 0;
    closed = false;
  }

  // Sized at a parse's first election: grown from nothing, the room costs a
  // small document more than its elections do.
  void reserve() {
    nodes.reserve(64);
    events.reserve(256);
    frames.reserve(64);
    frameOf.reserve(64);
    pending.reserve(32);
    popped.reserve(32);
    left.reserve(32);
    right.reserve(32);
    region.reserve(32);
    syms.reserve(32);
    words.reserve(32);
    wordAt.reserve(32);
    walkLeft.reserve(16);
    walkRight.reserve(16);
  }

  std::vector<Memo> memo;
  // Fingerprints only index; snapshots retain both traces and frozen symbols.
  std::vector<std::uint32_t> memoExact;
  std::uint32_t memos = 0;
  // The lowest frame of the cell's stack the elections in hand read.
  std::uint32_t read = 0;
  // What the cell's elections cost so far, and whether one arm is left: no
  // election follows, so no step is kept.
  std::uint32_t work = 0;
  bool closed = false;
  std::vector<Node> nodes;
  std::vector<std::uint64_t> events;
  std::vector<Frame> frames;
  // Per step of the trail, the top frame of the stack it leaves.
  std::vector<std::uint32_t> frameOf;
  std::vector<std::uint32_t> pending, popped, left, right, region, syms, words;
  std::vector<std::uint16_t> wordAt;
  std::vector<Walk> walkLeft, walkRight;
};

}  // namespace cursiv
