#pragma once

#include <memory>
#include <string_view>

#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// The container of the whole workspace, every language. Non-movable, since
// what it installs binds to it by reference. The service graph is fixed once
// concurrent processing begins: quiesce the readers before replacing a
// service or destroying this.

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

  // Where the runtime says what went wrong when no client is owed an answer.
  // First, so it dies last: a service reporting from its own teardown still
  // has a sink.
  std::unique_ptr<ObservabilitySink> observabilitySink;
  // The one pool a workspace shares, replaceable by the host, sized by
  // `CURSIV_WORKERS` or the hardware. Early for the same reason: every
  // service that fans out on it is gone before it stops. One consumer today
  // (the builder): a second fan-out on the same pool must first prove it
  // cannot deadlock a run already inside it.
  std::unique_ptr<TaskScheduler> taskScheduler;
  std::unique_ptr<Documents> documents;
  std::unique_ptr<DocumentFactory> documentFactory;
  std::unique_ptr<IndexManager> indexManager;
  std::unique_ptr<DocumentBuilder> documentBuilder;
  // After the builder, and the order is load-bearing. Members die in reverse
  // declaration order, and a language's services -- which this registry owns --
  // may hold a cache subscribed to the builder. Declared before it, the builder
  // would die first and every such cache would unsubscribe from freed memory.
  std::unique_ptr<ServiceRegistry> serviceRegistry;
  // Always installed: a workspace with no disk under it is an installer's
  // choice of provider, never an absent slot.
  std::unique_ptr<FileSystemProvider> fileSystemProvider;
  std::unique_ptr<WorkspaceLock> workspaceLock;
  std::unique_ptr<WorkspaceManager> workspaceManager;

  virtual void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const;

  [[nodiscard]] std::string_view firstMissingService() const;
};

// Fill-if-empty, like its language-level counterpart. Nothing shared knows a
// grammar, so nothing here is templated.
void install_default_shared_core_services(SharedCoreServices& shared);

}  // namespace cursiv
