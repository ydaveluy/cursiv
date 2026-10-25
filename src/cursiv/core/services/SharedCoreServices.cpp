#include "cursiv/core/services/SharedCoreServices.hpp"

#include <memory>
#include <string_view>

#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/observability/StderrObservabilitySink.hpp"
#include "cursiv/core/services/DefaultServiceRegistry.hpp"
#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/DefaultDocuments.hpp"
#include "cursiv/core/workspace/DefaultIndexManager.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceLock.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceManager.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"

namespace cursiv {

SharedCoreServices::SharedCoreServices() noexcept = default;
SharedCoreServices::~SharedCoreServices() noexcept = default;

// Declaration order, exactly: `firstMissingService` reports the first hole,
// so the order here is a surface, and two orders would be two answers.
void SharedCoreServices::visitServices(
    cursiv::function_ref<void(const ServiceSlot&)> yield) const {
  yield({"observabilitySink", true, observabilitySink != nullptr});
  yield({"taskScheduler", true, taskScheduler != nullptr});
  yield({"documents", true, documents != nullptr});
  yield({"documentFactory", true, documentFactory != nullptr});
  yield({"indexManager", true, indexManager != nullptr});
  yield({"documentBuilder", true, documentBuilder != nullptr});
  yield({"serviceRegistry", true, serviceRegistry != nullptr});
  yield({"fileSystemProvider", true, fileSystemProvider != nullptr});
  yield({"workspaceLock", true, workspaceLock != nullptr});
  yield({"workspaceManager", true, workspaceManager != nullptr});
}

std::string_view SharedCoreServices::firstMissingService() const {
  return first_missing_service(*this);
}

void install_default_shared_core_services(SharedCoreServices& shared) {
  if (shared.documents == nullptr)
    shared.documents = std::make_unique<DefaultDocuments>(shared);
  if (shared.serviceRegistry == nullptr)
    shared.serviceRegistry = std::make_unique<DefaultServiceRegistry>(shared);
  if (shared.documentFactory == nullptr)
    shared.documentFactory = std::make_unique<DefaultDocumentFactory>(shared);
  if (shared.taskScheduler == nullptr)
    shared.taskScheduler = std::make_unique<TaskScheduler>();
  if (shared.indexManager == nullptr)
    shared.indexManager = std::make_unique<DefaultIndexManager>(shared);
  if (shared.documentBuilder == nullptr)
    shared.documentBuilder = std::make_unique<DefaultDocumentBuilder>(shared);
  // The default is the empty provider: reading a disk is a decision the host
  // states by installing the local one -- never a side effect of forgetting.
  if (shared.fileSystemProvider == nullptr)
    shared.fileSystemProvider = std::make_unique<EmptyFileSystemProvider>();
  if (shared.workspaceLock == nullptr)
    shared.workspaceLock = std::make_unique<DefaultWorkspaceLock>();
  if (shared.workspaceManager == nullptr)
    shared.workspaceManager = std::make_unique<DefaultWorkspaceManager>(shared);
  // stderr and not stdout: stdout is the protocol's.
  if (shared.observabilitySink == nullptr)
    shared.observabilitySink = std::make_unique<StderrObservabilitySink>();
}

}  // namespace cursiv
