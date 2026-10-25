#pragma once

#include <memory>
#include <string_view>

#include "cursiv/core/services/LanguageMetaData.hpp"
#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// The container of one language. Non-movable, since an installed default
// holds a back-reference to it. Slots are erased interfaces, not templates,
// since one workspace carries several grammars at once; a service adds
// itself once, in `visitServices`. Slot types are declared, never included,
// which is why the destructor is out-of-line.

namespace cursiv {

struct SharedCoreServices;
class Parser;
struct AstReflection;
class NameProvider;
class AstNodeLocator;
class AstNodeDescriptionProvider;
class ReferenceDescriptionProvider;
class ScopeComputation;
class ScopeProvider;
class GrammarSource;
class Linker;
class References;
class DocumentationProvider;
class CommentProvider;
class ValidationRegistry;
class DocumentValidator;

struct CoreServices {
  explicit CoreServices(SharedCoreServices& sharedServices) noexcept;
  virtual ~CoreServices() noexcept;
  CoreServices(const CoreServices&) = delete;
  CoreServices& operator=(const CoreServices&) = delete;

  SharedCoreServices& shared;

  LanguageMetaData languageMetaData;

  // `const` in the pointee, so a const container hands out a const engine.
  std::unique_ptr<const Parser> parser;

  // Null by default: a language serves its grammar's text only when its
  // module hands one over.
  std::unique_ptr<const GrammarSource> grammarSource;

  std::unique_ptr<const AstReflection> astReflection;
  std::unique_ptr<const NameProvider> nameProvider;
  std::unique_ptr<const AstNodeLocator> astNodeLocator;
  std::unique_ptr<const AstNodeDescriptionProvider> astNodeDescriptionProvider;
  std::unique_ptr<const ReferenceDescriptionProvider>
      referenceDescriptionProvider;
  std::unique_ptr<const ScopeComputation> scopeComputation;
  std::unique_ptr<const ScopeProvider> scopeProvider;
  std::unique_ptr<const Linker> linker;
  // The inverse direction of the linker's edge: the linker answers what a name
  // points at, this answers what points at a node and what is under a caret.
  std::unique_ptr<const References> references;
  // What the grammar declares about its own vocabulary, and -- its own service,
  // because it answers a different question -- the comment an author wrote
  // above a declaration.
  std::unique_ptr<const DocumentationProvider> documentationProvider;
  std::unique_ptr<const CommentProvider> commentProvider;
  std::unique_ptr<ValidationRegistry> validationRegistry;
  // The pass that runs the checks. A language's own validator -- the class
  // whose methods are checks -- is a slot of that language's container, not of
  // this one.
  std::unique_ptr<const DocumentValidator> documentValidator;

  // `first_missing_service` scans exactly what this yields, and a storey above
  // appends its own rows.
  virtual void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;

  [[nodiscard]] std::string_view firstMissingService() const;
};

// Fills the slots no grammar decides, fill-if-empty: a language module that
// pre-installed one keeps it. `languageMetaData`, `parser` and `astReflection`
// are deliberately left out -- the first two are the language's own, and the
// reflection comes from its generated module.
void install_default_core_services(CoreServices& services);

}  // namespace cursiv
