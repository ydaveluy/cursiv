// The cleanup every check returns: it removes the document the check built,
// held until then. A check that throws runs it itself.
#pragma once

#include <exception>
#include <memory>

#include "cursiv/Disposable.hpp"

namespace cursiv {
struct CoreServices;
struct Document;
}  // namespace cursiv

namespace cursiv::test::detail {

[[nodiscard]] Disposable documentDisposable(const CoreServices& services,
                                            std::shared_ptr<Document> document);

// Runs `cleanup` when the scope is left by an exception. A cleanup that throws
// in turn gives way to the first exception.
class DisposeOnThrow {
 public:
  explicit DisposeOnThrow(Disposable& cleanup) noexcept : _cleanup(cleanup) {}
  DisposeOnThrow(const DisposeOnThrow&) = delete;
  DisposeOnThrow& operator=(const DisposeOnThrow&) = delete;
  ~DisposeOnThrow();

 private:
  Disposable& _cleanup;
  int _uncaught = std::uncaught_exceptions();
};

}  // namespace cursiv::test::detail
