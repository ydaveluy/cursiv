#include "cursiv/core/workspace/DocumentFactory.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

namespace cursiv {

std::shared_ptr<Document> DocumentFactory::createDocument(
    TextDocument textDocument, std::string_view uri) const {
  return std::make_shared<Document>(std::move(textDocument), std::string(uri));
}

}  // namespace cursiv
