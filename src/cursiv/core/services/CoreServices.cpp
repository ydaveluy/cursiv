#include "cursiv/core/services/CoreServices.hpp"

#include <memory>
#include <string_view>

#include "cursiv/core/documentation/DefaultCommentProvider.hpp"
#include "cursiv/core/documentation/DefaultDocumentationProvider.hpp"
#include "cursiv/core/grammar/GrammarSource.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultLinker.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/references/DefaultReferences.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/references/DefaultScopeProvider.hpp"
#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/validation/DefaultDocumentValidator.hpp"
#include "cursiv/core/validation/DefaultValidationRegistry.hpp"
#include "cursiv/core/workspace/DefaultAstNodeDescriptionProvider.hpp"
#include "cursiv/core/workspace/DefaultAstNodeLocator.hpp"
#include "cursiv/core/workspace/DefaultReferenceDescriptionProvider.hpp"

namespace cursiv {

CoreServices::CoreServices(SharedCoreServices& sharedServices) noexcept
    : shared(sharedServices) {}

CoreServices::~CoreServices() noexcept = default;

void CoreServices::visitServices(
    cursiv::function_ref<void(const ServiceSlot&)> yield) const {
  yield({"languageMetaData", true, !languageMetaData.languageId.empty()});
  yield({"parser", true, parser != nullptr});
  yield({"grammarSource", false, grammarSource != nullptr});
  yield({"astReflection", true, astReflection != nullptr});
  yield({"nameProvider", true, nameProvider != nullptr});
  yield({"astNodeLocator", true, astNodeLocator != nullptr});
  yield({"astNodeDescriptionProvider", true,
         astNodeDescriptionProvider != nullptr});
  yield({"referenceDescriptionProvider", true,
         referenceDescriptionProvider != nullptr});
  yield({"scopeComputation", true, scopeComputation != nullptr});
  yield({"scopeProvider", true, scopeProvider != nullptr});
  yield({"linker", true, linker != nullptr});
  yield({"references", true, references != nullptr});
  yield({"documentationProvider", true, documentationProvider != nullptr});
  yield({"commentProvider", true, commentProvider != nullptr});
  yield({"validationRegistry", true, validationRegistry != nullptr});
  yield({"documentValidator", true, documentValidator != nullptr});
}

std::string_view CoreServices::firstMissingService() const {
  return first_missing_service(*this);
}

void install_default_core_services(CoreServices& services) {
  if (services.astNodeLocator == nullptr)
    services.astNodeLocator = std::make_unique<DefaultAstNodeLocator>(services);
  if (services.astNodeDescriptionProvider == nullptr)
    services.astNodeDescriptionProvider =
        std::make_unique<DefaultAstNodeDescriptionProvider>(services);
  if (services.scopeProvider == nullptr)
    services.scopeProvider = std::make_unique<DefaultScopeProvider>(services);
  if (services.nameProvider == nullptr)
    services.nameProvider = std::make_unique<DefaultNameProvider>(services);
  if (services.validationRegistry == nullptr)
    services.validationRegistry =
        std::make_unique<DefaultValidationRegistry>(services);
  if (services.documentValidator == nullptr)
    services.documentValidator =
        std::make_unique<DefaultDocumentValidator>(services);
  if (services.linker == nullptr)
    services.linker = std::make_unique<DefaultLinker>(services);
  if (services.scopeComputation == nullptr)
    services.scopeComputation =
        std::make_unique<DefaultScopeComputation>(services);
  if (services.referenceDescriptionProvider == nullptr)
    services.referenceDescriptionProvider =
        std::make_unique<DefaultReferenceDescriptionProvider>(services);
  if (services.references == nullptr)
    services.references = std::make_unique<DefaultReferences>(services);
  if (services.documentationProvider == nullptr)
    services.documentationProvider =
        std::make_unique<DefaultDocumentationProvider>(services);
  if (services.commentProvider == nullptr)
    services.commentProvider =
        std::make_unique<DefaultCommentProvider>(services);
}

}  // namespace cursiv
