// What the grammar as written may read: whether an expression may match
// nothing, and whether it may be nothing but a run. A rule's answer is the
// least fixpoint over the rules a query reaches, solved when the generator
// runs: one answer per rule, however the rules call each other.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/grammar/GrammarImage.hpp"

namespace cursiv {

// A directive, prose, mark or region over one operand reads what it does.
inline bool over_operand(detail::ImageKind kind) {
  switch (kind) {
    case detail::ImageKind::doc:
    case detail::ImageKind::mark:
    case detail::ImageKind::skip:
    case detail::ImageKind::assign:
    case detail::ImageKind::append:
    case detail::ImageKind::flag:
    case detail::ImageKind::create:
    case detail::ImageKind::nest:
      return true;
    default:
      return false;
  }
}

// A condition takes no position, bare or under a repetition or a wrapper whose
// reading is its operand's: the image's reading of the grammar's own
// `is_condition`.
inline bool is_condition_node(const detail::GrammarImage& image,
                              detail::ImageId id) {
  for (;;) {
    const detail::ImageNode& n = image.nodes[id];
    switch (n.kind) {
      case detail::ImageKind::andOf:
      case detail::ImageKind::notOf:
      case detail::ImageKind::gapHas:
      case detail::ImageKind::gapIs:
      case detail::ImageKind::eof:
        return true;
      case detail::ImageKind::repeat:
      case detail::ImageKind::doc:
      case detail::ImageKind::mark:
      case detail::ImageKind::skip:
      case detail::ImageKind::embed:
      case detail::ImageKind::assign:
      case detail::ImageKind::append:
      case detail::ImageKind::flag:
        id = image.kids[n.first];
        break;
      default:
        return false;
    }
  }
}

class WrittenReads {
 public:
  explicit WrittenReads(detail::GrammarImage& image) : _image(image) {}

  bool nothing(detail::ImageId node);
  bool run(detail::ImageId node);

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
  bool nothing_of(detail::ImageId node);
  bool run_of(detail::ImageId node);
  bool solve(std::uint32_t at);
  void settle();
  template <class Ask>
  bool answer(Ask ask);

  detail::GrammarImage& _image;
  std::vector<Slot> _slots;
  // Each rule's slot, and the slots still to solve.
  std::vector<std::uint32_t> _slotOf, _work;
  // The slot whose body is being read, if any.
  std::uint32_t _solving = kNoSlot;
};

}  // namespace cursiv
