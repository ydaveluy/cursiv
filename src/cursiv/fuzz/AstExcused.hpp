// Names a member of one node that the walk could not write. Travels from
// generator to comparator so an unreachable member is named, never counted
// as agreement.
#pragma once

#include <string>

namespace cursiv {
struct AstNode;
}  // namespace cursiv

namespace cursiv::fuzzer {

struct AstExcused {
  const AstNode* node = nullptr;
  std::string member;
};

}  // namespace cursiv::fuzzer
