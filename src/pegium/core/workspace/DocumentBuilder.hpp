#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/utils/Event.hpp>
#include <pegium/core/validation/ValidationOptions.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>
#include <pegium/core/workspace/Documents.hpp>
#include <pegium/core/workspace/IndexManager.hpp>

namespace pegium {
class ServiceRegistry;
}

namespace pegium::workspace {

/// Options controlling how a document build should proceed.
struct BuildOptions {
  std::optional<bool> eagerLinking;
  validation::BuildValidationOption validation;
};

/// Returns the workspace-default build options: validation enabled for the
/// built-in and fast categories.
[[nodiscard]] inline BuildOptions makeDefaultBuildOptions() {
  BuildOptions options;
  options.validation = validation::ValidationOptions{
      .categories = {std::string(validation::kBuiltInValidationCategory),
                     std::string(validation::kFastValidationCategory)}};
  return options;
}

/// Summary of one document update cycle.
struct DocumentUpdateEvent {
  std::vector<DocumentId> changedDocumentIds;
  std::vector<DocumentId> deletedDocumentIds;
};

/// Notification emitted after a batch reaches one build phase.
struct DocumentBuildPhaseEvent {
  DocumentState targetState = DocumentState::Changed;
  /// `builtDocuments` only contains non-null managed documents.
  std::vector<std::shared_ptr<Document>> builtDocuments;
  utils::CancellationToken cancelToken;
};

/// Notification emitted after one document reaches one build phase.
struct DocumentPhaseEvent {
  DocumentState targetState = DocumentState::Changed;
  /// `builtDocument` is always a non-null managed document.
  std::shared_ptr<Document> builtDocument;
  utils::CancellationToken cancelToken;
};

/// Persistent build state tracked for one document.
struct DocumentBuildState {
  bool completed = false;
  BuildOptions options{};
  struct Result {
    std::vector<std::string> validationChecks;
  };
  std::optional<Result> result;
};

/// Orchestrates parse, index, link, and validation phases for documents.
class DocumentBuilder {
public:
  virtual ~DocumentBuilder() noexcept = default;

  /// Returns the default options used by `update(...)`.
  [[nodiscard]] virtual BuildOptions &updateBuildOptions() noexcept = 0;
  [[nodiscard]] virtual const BuildOptions &
  updateBuildOptions() const noexcept = 0;

  /// Builds the provided documents up to the phases requested by `options`.
  ///
  /// `documents` must only contain non-null managed documents with a
  /// normalized non-empty URI.
  /// `downgradeLock`, when set, is invoked once the document model is fully
  /// linked and before validation runs, so callers holding an exclusive lock
  /// can release it for the validation phase.
  virtual void build(std::span<const std::shared_ptr<Document>> documents,
                     const BuildOptions &options = {},
                     utils::CancellationToken cancelToken = {},
                     const std::function<void()> &downgradeLock = {}) const = 0;

  /// Rebuilds documents affected by change and deletion notifications.
  virtual void update(std::span<const DocumentId> changedDocumentIds,
                      std::span<const DocumentId> deletedDocumentIds,
                      utils::CancellationToken cancelToken = {},
                      const std::function<void()> &downgradeLock = {}) const = 0;

  /// Subscribes to document update notifications.
  virtual utils::ScopedDisposable
  onUpdate(std::function<void(std::span<const DocumentId> changedDocumentIds,
                              std::span<const DocumentId> deletedDocumentIds)>
               listener) const = 0;
  /// Subscribes to notifications fired after a batch reaches `targetState`.
  virtual utils::ScopedDisposable
  onBuildPhase(DocumentState targetState,
               std::function<void(
                   std::span<const std::shared_ptr<Document>> builtDocuments,
                   utils::CancellationToken cancelToken)>
                   listener) const = 0;
  /// Subscribes to notifications fired after one document reaches `targetState`.
  ///
  /// Listeners receive a non-null managed document.
  virtual utils::ScopedDisposable
  onDocumentPhase(DocumentState targetState,
                  std::function<void(const std::shared_ptr<Document> &,
                                     utils::CancellationToken cancelToken)>
                      listener) const = 0;

  /// Waits until the workspace has reached `state`.
  ///
  /// This is the synchronization primitive for callers that require a stable
  /// analysis phase. Use it when `WorkspaceManager::ready()` is too weak
  /// because readiness only guarantees startup document discovery, not
  /// completion of the initial build.
  virtual void waitUntil(DocumentState state,
                         utils::CancellationToken cancelToken = {}) const = 0;
  /// Waits until the given document has reached `state`.
  ///
  /// This is the synchronization primitive for callers that require a stable
  /// phase for one document even when newer writes may supersede older build
  /// work.
  [[nodiscard]] virtual DocumentId
  waitUntil(DocumentState state, DocumentId documentId,
            utils::CancellationToken cancelToken = {}) const = 0;

  /// Invalidates `document` back to `state` so later phases can be recomputed.
  virtual void resetToState(Document &document, DocumentState state) const = 0;
};

} // namespace pegium::workspace
