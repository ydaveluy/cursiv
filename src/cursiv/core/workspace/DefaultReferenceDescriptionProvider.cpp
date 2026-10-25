#include "cursiv/core/workspace/DefaultReferenceDescriptionProvider.hpp"

#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

namespace cursiv {

std::vector<ReferenceDescription>
DefaultReferenceDescriptionProvider::createDescriptions(
    const Document& document, const CancellationToken& cancelToken) const {
  std::vector<ReferenceDescription> rows;
  const DocumentReferences* const references =
      document.parseResult.references.get();
  if (references == nullptr) return rows;
  const Cst& cst = document.parseResult.cst();
  rows.reserve(references->handles.size());

  // One row per resolved target and not one per reference: a multi reference
  // names its whole overload set, and the inverse table groups rows by target
  // -- so a reference described only through its first target would be
  // invisible when asked from any other member of the set.
  references->visitResolved(cst, [&](TextOffset begin, TextOffset end,
                                     const AstNode& target, bool multi) {
    throw_if_cancelled(cancelToken);
    const Document* const home = target.getDocument();
    rows.push_back(
        {.sourceDocumentId = document.id,
         .sourceBegin = begin,
         .sourceEnd = end,
         .targetDocumentId = home == nullptr ? kNoDocument : home->id,
         .targetSymbolId = target.cstNodeId,
         .local = home == &document,
         .multiReference = multi});
  });

  // Only what resolved is described: a reference pointing nowhere puts no
  // edge in an index that answers "who points here", and the rebuild of a
  // document whose missing symbol turned up rides the linking-error flag,
  // never a row here.
  return rows;
}

}  // namespace cursiv
