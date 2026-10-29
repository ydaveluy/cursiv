// What the grammar as written may read: whether an expression may match
// nothing, and whether it may be nothing but a run. A rule's answer is the
// least fixpoint over the rules a query reaches, solved when the generator
// runs: one answer per rule, however the rules call each other.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/grammar/GrammarImage.hpp"

namespace cursiv {

// A directive, prose, mark or region over one operand reads what it does.
inline bool over_operand(ImageKind kind) {
  switch (kind) {
    case ImageKind::doc:
    case ImageKind::mark:
    case ImageKind::skip:
    case ImageKind::assign:
    case ImageKind::append:
    case ImageKind::flag:
    case ImageKind::create:
    case ImageKind::nest:
      return true;
    default:
      return false;
  }
}

// A condition takes no position, bare or under a repetition or a wrapper whose
// reading is its operand's: the image's reading of the grammar's own
// `is_condition`.
inline bool is_condition_node(const GrammarImage& image, ImageId id) {
  for (;;) {
    const ImageNode& n = image.nodes[id];
    switch (n.kind) {
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::eof:
        return true;
      case ImageKind::repeat:
      case ImageKind::doc:
      case ImageKind::mark:
      case ImageKind::skip:
      case ImageKind::embed:
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
        id = image.kids[n.first];
        break;
      default:
        return false;
    }
  }
}

class WrittenReads {
 public:
  explicit WrittenReads(GrammarImage& image) : _image(image) {}

  bool nothing(ImageId node);
  bool run(ImageId node);

 private:
  static constexpr std::uint32_t kNoSlot = ~std::uint32_t{0};

  struct Slot {
    std::uint32_t rule = 0;
    bool nothing = false;
    bool run = false;
    // The slots whose bodies read this one: solved again when it moves.
    std::vector<std::uint32_t> readers;
  };

  Slot& slot(std::uint32_t rule);
  bool reads_into(std::uint32_t rule);
  bool nothing_of(ImageId node);
  bool run_of(ImageId node);
  bool solve(std::uint32_t at);
  void settle();
  template <class Ask>
  bool answer(Ask ask);

  GrammarImage& _image;
  std::vector<Slot> _slots;
  // Each rule's slot, and the slots still to solve.
  std::vector<std::uint32_t> _slotOf, _work;
  // The slot whose body is being read, if any.
  std::uint32_t _solving = kNoSlot;
  std::size_t _moved = 0;
};

}  // namespace cursiv
