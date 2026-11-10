#include "cursiv/references/DefaultScopeProvider.hpp"

#include <cstdint>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/IndexManager.hpp"

namespace cursiv {

namespace {

// The site's document, carried by the reference table rather than walked up to,
// or found from the container for a site outside a linked document.
const Document* scope_of(const ReferenceInfo& context) {
  if (context.document != nullptr) return context.document;
  return context.container == nullptr ? nullptr
                                      : context.container->getDocument();
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
  return context.narrow == kNoNarrow
             ? nullptr
             : reflection.referenceNarrowTarget(context.reference, context.narrow);
}

}  // namespace

bool DefaultScopeProvider::visitScopeEntries(
    const ReferenceInfo& context,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  const Document* const document = scope_of(context);
  if (document == nullptr) return true;

  // Computed once for the walk, with this language's reflection: a description
  // carries a process-wide tag, and the set is over this language's indices.
  const AstReflection& reflection = *services.astReflection;
  const AcceptSet accepts = reflection.referenceAccepts(context.reference);
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);

  const LocalSymbols& symbols = document->localSymbols;
  const auto stopsAt = [&](const AstNode* scope) {
    for (const AstNodeDescription& entry : symbols.forContainer(scope)) {
      // A candidate the reference cannot hold is not in its scope, however
      // close it sits.
      if (!reflection.acceptsTag(accepts, entry.type)) continue;
      if (!reflection.isSubtype(entry.type, narrowed)) continue;
      if (!visitor(entry)) return true;
    }
    return false;
  };
  if (symbols.walkScopes(context.container, document, stopsAt)) return false;
  return visitGlobalScopeEntries(context, accepts, reflection, visitor);
}

// An empty text is the caller asking for the whole visible scope, which every
// exported symbol answers.
bool DefaultScopeProvider::visitGlobalScopeEntries(
    const ReferenceInfo& context, AcceptSet accepts,
    const AstReflection& reflection,
    cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const {
  // With no name, list everything: filtering by what the site currently spells
  // would hide every other completion.
  const AstTypeTag* const narrowed = narrowed_to(reflection, context);
  return shared.indexManager->visitElements(
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
    return shared.indexManager->visitByName(
        name, caseSensitive, accepts, reflection, visitor);
  return shared.indexManager->visitByName(
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

  // Each level gives the entries whose folded name matches; a case-sensitive
  // policy then checks each spelling.
  const std::uint64_t nameHash = LocalSymbols::hash_name(name);
  const LocalSymbols& symbols = document->localSymbols;
  const auto stopsAt = [&](const AstNode* scope) {
    return !symbols.find(scope, name, nameHash)
                .for_each([&](const AstNodeDescription& entry) {
                  if (caseSensitive && entry.name != name) return true;
                  return !reflection.acceptsTag(accepts, entry.type) ||
                         !reflection.isSubtype(entry.type, narrowed) ||
                         visitor(entry);
                });
  };
  if (symbols.walkScopes(context.container, document, stopsAt)) return false;
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

  // Hashed once for the whole walk up: the name does not change from one
  // ancestor to the next.
  const std::uint64_t nameHash = LocalSymbols::hash_name(name);
  const LocalSymbols::NameQuery query{name, nameHash, caseSensitive};
  const LocalSymbols& symbols = document->localSymbols;
  const AstNodeDescription* found = nullptr;
  const auto stopsAt = [&](const AstNode* scope) {
    found =
        symbols.firstNamed(scope, query, [&](const AstNodeDescription& entry) {
          return reflection.acceptsTag(accepts, entry.type) &&
                 (narrowed == nullptr ||
                  reflection.isSubtype(entry.type, narrowed));
        });
    return found != nullptr;
  };
  if (symbols.walkScopes(context.container, document, stopsAt)) return found;
  // Local first and global last, so a declaration in reach always wins over a
  // homonym across the workspace.
  record_import(*document, nameHash);
  return getGlobalScopeEntry(context, name, accepts, reflection, caseSensitive);
}

const AstNodeDescription* DefaultScopeProvider::getGlobalScopeEntry(
    const ReferenceInfo& context, std::string_view name, AcceptSet accepts,
    const AstReflection& reflection, bool caseSensitive) const {
  if (narrowed_to(reflection, context) == nullptr)
    return shared.indexManager->findByName(name, caseSensitive,
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
