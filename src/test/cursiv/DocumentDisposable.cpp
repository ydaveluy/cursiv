#include "cursiv/DocumentDisposable.hpp"

#include <exception>
#include <memory>
#include <utility>

#include "cursiv/Disposable.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv::test {

Disposable detail::documentDisposable(const CoreServices& services,
                                      std::shared_ptr<Document> document) {
  const SharedCoreServices* const shared = &services.shared();
  return Disposable::create([shared, held = std::move(document)] {
    const std::shared_ptr<Document> one[] = {held};
    clearDocuments(*shared, one);
  });
}

detail::DisposeOnThrow::~DisposeOnThrow() {
  if (std::uncaught_exceptions() <= _uncaught) return;
  try {
    _cleanup.dispose();
  } catch (...) {  // NOLINT(bugprone-empty-catch): the first exception wins
  }
}

}  // namespace cursiv::test
