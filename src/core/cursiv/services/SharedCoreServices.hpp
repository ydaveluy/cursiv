#pragma once

#include <memory>
#include <string_view>

#include "cursiv/services/ServiceSlot.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The container of the whole workspace, every language. Non-movable, as what
// it installs binds to it. Stop all readers before replacing or destroying a
// service.

namespace cursiv {

class TaskScheduler;
class Documents;
class DocumentFactory;
class FileSystemProvider;
class DocumentBuilder;
class IndexManager;
class ServiceRegistry;
class WorkspaceLock;
class WorkspaceManager;
class ObservabilitySink;

struct SharedCoreServices {
  SharedCoreServices() noexcept;
  virtual ~SharedCoreServices() noexcept;
  SharedCoreServices(const SharedCoreServices&) = delete;
  SharedCoreServices& operator=(const SharedCoreServices&) = delete;

  // Where the runtime reports problems no request answers. Declared first, so
  // it is destroyed last: a service reporting from its own teardown still has
  // a sink.
  std::unique_ptr<ObservabilitySink> observabilitySink;
  // The workspace's one pool, replaceable, sized by `CURSIV_WORKERS` or the
  // hardware. Declared early so every service using it is gone before it stops.
  // `TaskScheduler::parallelFor` may be called from a task.
  std::unique_ptr<TaskScheduler> taskScheduler;
  std::unique_ptr<Documents> documents;
  std::unique_ptr<DocumentFactory> documentFactory;
  std::unique_ptr<IndexManager> indexManager;
  std::unique_ptr<DocumentBuilder> documentBuilder;
  // After the builder, on purpose: members die in reverse order, and a
  // language's services may hold a cache subscribed to the builder.
  std::unique_ptr<ServiceRegistry> serviceRegistry;
  // Always installed; a workspace without a disk uses the empty provider.
  std::unique_ptr<FileSystemProvider> fileSystemProvider;
  std::unique_ptr<WorkspaceLock> workspaceLock;
  std::unique_ptr<WorkspaceManager> workspaceManager;

  virtual void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;

  [[nodiscard]] std::string_view firstMissingService() const;
};

// Fills the empty slots, like `install_default_core_services`.
void install_default_shared_core_services(SharedCoreServices& shared);

}  // namespace cursiv
