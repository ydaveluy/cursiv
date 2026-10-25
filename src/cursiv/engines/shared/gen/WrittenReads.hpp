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

class WrittenReads {
 public:
  explicit WrittenReads(GrammarImage& image) : _image(image) {}

  bool nothing(ImageId node);
  bool run(ImageId node);

 private:
  struct Slot {
    std::uint32_t rule = 0;
    bool nothing = false;
    bool run = false;
  };

  Slot& slot(std::uint32_t rule);
  bool reads_into(std::uint32_t rule);
  bool nothing_of(ImageId node);
  bool run_of(ImageId node);
  void solve(std::size_t at);
  void settle();
  template <class Ask>
  bool answer(Ask ask);

  GrammarImage& _image;
  std::vector<Slot> _slots;
  std::size_t _moved = 0;
};

}  // namespace cursiv
