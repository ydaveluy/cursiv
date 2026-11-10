#pragma once

#include <memory>
#include <string_view>

#include "cursiv/services/LanguageMetaData.hpp"
#include "cursiv/services/ServiceSlot.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The container of one language; non-movable, as defaults point back to it.
// Slots are abstract interfaces, so one workspace holds several grammars. Each
// slot is listed in `visitServices`.

namespace cursiv {

struct SharedCoreServices;
class Parser;
class ParserErrorMessageProvider;
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

  [[nodiscard]] SharedCoreServices& shared() const noexcept {
    return *_shared;
  }

  LanguageMetaData languageMetaData;

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
  // The reverse of the linker: what points at a node, and what is under a
  // caret.
  std::unique_ptr<const References> references;
  // The documentation shown for a node, and the raw comment written above it.
  std::unique_ptr<const DocumentationProvider> documentationProvider;
  std::unique_ptr<const CommentProvider> commentProvider;
  // The messages of the syntax errors the parser reports.
  std::unique_ptr<const ParserErrorMessageProvider> parserErrorMessageProvider;
  std::unique_ptr<ValidationRegistry> validationRegistry;
  // Runs the registered checks. A language's own validator class is a slot of
  // the language's derived container.
  std::unique_ptr<const DocumentValidator> documentValidator;

  // `firstMissingService` checks exactly what this yields; a derived container
  // adds its own slots.
  virtual void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;

  [[nodiscard]] std::string_view firstMissingService() const;

 protected:
  // Lets the most derived class skip this virtual base: the intermediate base
  // then calls `bindShared`.
  CoreServices() noexcept;
  void bindShared(SharedCoreServices& sharedServices) noexcept {
    _shared = std::addressof(sharedServices);
  }

 private:
  SharedCoreServices* _shared = nullptr;
};

// Fills the slots no grammar decides, keeping any already set.
// `languageMetaData`, `parser` and `astReflection` are the language's own.
void install_default_core_services(CoreServices& services);

}  // namespace cursiv
