#include "cursiv/DocumentDisposable.hpp"

#include <memory>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/ParseHelper.hpp"

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
