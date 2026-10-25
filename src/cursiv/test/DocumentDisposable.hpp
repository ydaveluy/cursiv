// The cleanup every check returns: the one document it built, removed from the
// workspace. The document is held until it runs, so the caller may still read
// it after the check.
#pragma once

#include <memory>

#include "cursiv/test/Disposable.hpp"

namespace cursiv {
struct CoreServices;
struct Document;
}  // namespace cursiv

namespace cursiv::test {

[[nodiscard]] Disposable documentDisposable(const CoreServices& services,
                                            std::shared_ptr<Document> document);

}  // namespace cursiv::test
