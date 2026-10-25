// What one validated text left behind: the findings, the document they
// were measured in, and the container whose grammar says where a member
// was written.
#pragma once

#include <memory>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/test/Disposable.hpp"

namespace cursiv {
struct CoreServices;
struct Document;
}  // namespace cursiv

namespace cursiv::test {

struct ValidationResult : Disposable {
  std::vector<Diagnostic> diagnostics;
  std::shared_ptr<Document> document;
  const CoreServices* services = nullptr;
};

}  // namespace cursiv::test
