#include "cursiv/test/DocumentDisposable.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelper.hpp"

namespace cursiv::test {

Disposable documentDisposable(const CoreServices& services,
                              std::shared_ptr<Document> document) {
  const SharedCoreServices* const shared = &services.shared;
  return Disposable::create([shared, held = std::move(document)] {
    const std::shared_ptr<Document> one[] = {held};
    clearDocuments(*shared, one);
  });
}

}  // namespace cursiv::test
