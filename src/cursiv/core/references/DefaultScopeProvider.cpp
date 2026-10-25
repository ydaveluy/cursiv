#include "cursiv/core/references/DefaultScopeProvider.hpp"

#include <cstdint>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"

namespace cursiv {

namespace {

// The site's document, carried by the reference table rather than climbed to:
// every site of one document shares it, and the climb was paid per reference.
// The fallback covers a site read outside a linked document -- a test builds
// one by hand.
const Document* scope_of(const ReferenceInfo& context) {
  if (context.document != nullptr) return context.document;
  return context.container == nullptr ? nullptr
                                      : context.container->getDocument();
}

// The climb ends at the document, or wherever the ancestry does: a build that
// failed to materialise its tree leaves nodes with no parent, and the
// reference list still names them. Such a site sees the global scope alone.
bool climbing(const AstNode* scope, const Document* document) {
  return scope != nullptr && scope != document;
}

// A name the site looked for past its own document: a later export of it may
// change what the site resolves to.
void record_import(const Document& document, std::uint64_t nameHash) {
  if (auto* references = document.parseResult.references.get())
    references->importedNames.record(nameHash);
}

// Null for a site that narrows nothing, which `isSubtype` admits everything
// against.
const AstTypeTag* narrowed_to(const AstReflection& reflection,
                              const ReferenceInfo& context) {
  return reflection.referenceNarrowTarget(context.reference, context.narrow);
}

}  // namespace

bool DefaultScopeProvider::visitScopeEntries(
    const ReferenceInfo& context,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const Document* const document = scope_of(context);
  if (document == nullptr) return true;

  // Resolved once: what the reference accepts does not change across the walk,
  // and the reflection that reads it is this language's -- a description
  // carries a process-wide tag, the bitset is over our own indices.
  const AstReflection& reflection = *services.astReflection;
  const AcceptSet accepts = reflection.referenceAccepts(context.reference);
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);

  for (const AstNode* scope = context.container; climbing(scope, document);
       scope = scope->parent) {
    for (const AstNodeDescription& entry :
         document->localSymbols.forContainer(scope)) {
      // A candidate the reference cannot hold is not in its scope, however
      // close it sits.
      if (!reflection.acceptsTag(accepts, entry.type)) continue;
      if (!reflection.isSubtype(entry.type, narrowed)) continue;
      if (!visitor(entry)) return false;
    }
  }
  return visitGlobalScopeEntries(context, accepts, reflection, visitor);
}

// An empty text is the caller asking for the whole visible scope, which every
// exported symbol answers.
bool DefaultScopeProvider::visitGlobalScopeEntries(
    const ReferenceInfo& context, AcceptSet accepts,
    const AstReflection& reflection,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  // The no-name door enumerates: what the site currently spells is a fact
  // about the text, not a filter on what may stand there -- filtering by it
  // would hide every other completion.
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);
  return services.shared.indexManager->visitElements(
      nullptr, {}, [&](const AstNodeDescription& e) {
        return reflection.acceptsTag(accepts, e.type) &&
                       reflection.isSubtype(e.type, narrowed)
                   ? visitor(e)
                   : true;
      });
}

bool DefaultScopeProvider::visitGlobalScopeEntries(
    const ReferenceInfo& context, std::string_view name, AcceptSet accepts,
    const AstReflection& reflection, bool caseSensitive,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);
  if (narrowed == nullptr)
    return services.shared.indexManager->visitByName(
        name, caseSensitive, accepts, reflection, visitor);
  return services.shared.indexManager->visitByName(
      name, caseSensitive, accepts, reflection,
      [&](const AstNodeDescription& e) {
        return reflection.isSubtype(e.type, narrowed) ? visitor(e) : true;
      });
}

bool DefaultScopeProvider::visitScopeEntries(
    const ReferenceInfo& context, std::string_view name,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const Document* const document = scope_of(context);
  if (name.empty() || document == nullptr) return true;

  const AstReflection& reflection = *services.astReflection;
  const AcceptSet accepts = reflection.referenceAccepts(context.reference);
  const bool caseSensitive = isCaseSensitive(context);
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);

  // The chain a level answers is the folded class: an exact policy filters
  // it, entry by entry -- against the class that is almost always its one
  // spelling.
  const std::uint64_t nameHash = LocalSymbols::hash_name(name);
  for (const AstNode* scope = context.container; climbing(scope, document);
       scope = scope->parent) {
    if (!document->localSymbols.find(scope, name, nameHash)
             .for_each([&](const AstNodeDescription& entry) {
               if (caseSensitive && entry.name != name) return true;
               return !reflection.acceptsTag(accepts, entry.type) ||
                      !reflection.isSubtype(entry.type, narrowed) ||
                      visitor(entry);
             }))
      return false;
  }
  record_import(*document, nameHash);
  return visitGlobalScopeEntries(context, name, accepts, reflection,
                                 caseSensitive, visitor);
}

const AstNodeDescription* DefaultScopeProvider::getScopeEntry(
    const ReferenceInfo& context, std::string_view name) const {
  const Document* const document = scope_of(context);
  if (name.empty() || document == nullptr) return nullptr;

  const AstReflection& reflection = *services.astReflection;
  const AcceptSet accepts = reflection.referenceAccepts(context.reference);
  const bool caseSensitive = isCaseSensitive(context);
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);

  // Hashed once for the whole climb: the name does not change from one
  // ancestor to the next.
  const std::uint64_t nameHash = LocalSymbols::hash_name(name);
  for (const AstNode* scope = context.container; climbing(scope, document);
       scope = scope->parent) {
    const AstNodeDescription* const found = document->localSymbols.firstNamed(
        scope, name, nameHash, caseSensitive,
        [&](const AstNodeDescription& entry) {
          return reflection.acceptsTag(accepts, entry.type) &&
                 reflection.isSubtype(entry.type, narrowed);
        });
    if (found != nullptr) return found;
  }
  // Local first and global last, so a declaration in reach always wins over a
  // homonym across the workspace.
  record_import(*document, nameHash);
  return getGlobalScopeEntry(context, name, accepts, reflection, caseSensitive);
}

const AstNodeDescription* DefaultScopeProvider::getGlobalScopeEntry(
    const ReferenceInfo& context, std::string_view name, AcceptSet accepts,
    const AstReflection& reflection, bool caseSensitive) const {
  if (narrowed_to(reflection, context) == nullptr)
    return services.shared.indexManager->findByName(name, caseSensitive,
                                                    accepts, reflection);
  const AstNodeDescription* found = nullptr;
  (void)visitGlobalScopeEntries(context, name, accepts, reflection,
                                caseSensitive,
                                [&](const AstNodeDescription& e) {
                                  found = &e;
                                  return false;
                                });
  return found;
}

}  // namespace cursiv
