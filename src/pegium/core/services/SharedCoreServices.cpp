#include <pegium/core/services/SharedCoreServices.hpp>

#include <cassert>

#include <pegium/core/observability/ObservabilitySinks.hpp>
#include <pegium/core/services/DefaultServiceRegistry.hpp>
#include <pegium/core/workspace/DefaultConfigurationProvider.hpp>
#include <pegium/core/workspace/DefaultDocumentBuilder.hpp>
#include <pegium/core/workspace/DefaultDocumentFactory.hpp>
#include <pegium/core/workspace/DefaultDocuments.hpp>
#include <pegium/core/workspace/DefaultIndexManager.hpp>
#include <pegium/core/workspace/DefaultWorkspaceLock.hpp>
#include <pegium/core/workspace/DefaultWorkspaceManager.hpp>

namespace pegium {

bool SharedCoreServices::isComplete() const noexcept {
  return observabilitySink && astReflection && taskScheduler &&
         serviceRegistry && workspace.configurationProvider &&
         workspace.fileSystemProvider && workspace.documentFactory &&
         workspace.documents && workspace.indexManager &&
         workspace.documentBuilder && workspace.workspaceLock &&
         workspace.workspaceManager;
}

void installDefaultSharedCoreServices(SharedCoreServices &sharedServices) {
  if (!sharedServices.observabilitySink) {
    sharedServices.observabilitySink =
        std::make_shared<observability::StderrObservabilitySink>();
  }
  if (!sharedServices.astReflection) {
    sharedServices.astReflection = std::make_unique<AstReflection>();
  }
  if (!sharedServices.taskScheduler) {
    sharedServices.taskScheduler =
        std::make_shared<execution::TaskScheduler>();
  }
  if (!sharedServices.serviceRegistry) {
    sharedServices.serviceRegistry =
        std::make_unique<DefaultServiceRegistry>(sharedServices);
  }
  if (!sharedServices.workspace.configurationProvider) {
    sharedServices.workspace.configurationProvider =
        std::make_shared<workspace::DefaultConfigurationProvider>(
            sharedServices);
  }
  if (!sharedServices.workspace.fileSystemProvider) {
    sharedServices.workspace.fileSystemProvider =
        std::make_shared<workspace::LocalFileSystemProvider>();
  }
  if (!sharedServices.workspace.documentFactory) {
    sharedServices.workspace.documentFactory =
        std::make_unique<workspace::DefaultDocumentFactory>(sharedServices);
  }
  if (!sharedServices.workspace.documents) {
    sharedServices.workspace.documents =
        std::make_unique<workspace::DefaultDocuments>(sharedServices);
  }
  if (!sharedServices.workspace.indexManager) {
    sharedServices.workspace.indexManager =
        std::make_unique<workspace::DefaultIndexManager>(sharedServices);
  }
  if (!sharedServices.workspace.documentBuilder) {
    sharedServices.workspace.documentBuilder =
        std::make_unique<workspace::DefaultDocumentBuilder>(sharedServices);
  }
  if (!sharedServices.workspace.workspaceLock) {
    sharedServices.workspace.workspaceLock =
        std::make_unique<workspace::DefaultWorkspaceLock>();
  }
  if (!sharedServices.workspace.workspaceManager) {
    sharedServices.workspace.workspaceManager =
        std::make_unique<workspace::DefaultWorkspaceManager>(sharedServices);
  }
  // Defense-in-depth: every slot above is fill-if-empty, so this fires only if a
  // required shared service was added to isComplete() but not installed here.
  // Statically dispatch to the base check: this installer owns only the core
  // shared slots. A virtual call would reach SharedServices::isComplete(), which
  // also requires the LSP slots that installDefaultSharedLspServices(...) installs
  // afterwards — aborting every Debug build of an LSP container.
  assert(sharedServices.SharedCoreServices::isComplete() &&
         "installDefaultSharedCoreServices left the shared container incomplete");
}

} // namespace pegium
