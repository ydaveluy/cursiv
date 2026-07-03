#include <pegium/core/references/DefaultLinker.hpp>

#include <cassert>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <pegium/core/execution/TaskScheduler.hpp>
#include <pegium/core/observability/ObservabilitySink.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/workspace/AstDescriptions.hpp>
#include <pegium/core/workspace/Document.hpp>

namespace pegium::references {

namespace {

void log_reference_resolution_problem(
    const pegium::SharedCoreServices &shared,
    const workspace::Document *document, std::string message,
    std::optional<workspace::DocumentState> state = std::nullopt) {
  observability::Observation observation{
      .severity = observability::ObservationSeverity::Warning,
      .code = observability::ObservationCode::ReferenceResolutionProblem,
      .message = std::move(message),
      .state = state};
  if (document != nullptr) {
    observation.uri = document->uri;
    observation.languageId = document->textDocument().languageId();
    observation.documentId = document->id;
    if (!observation.state.has_value()) {
      observation.state = document->state;
    }
  }
  shared.observabilitySink->publish(observation);
}

} // namespace

DefaultLinker::DefaultLinker(const pegium::CoreServices &services)
    : pegium::DefaultCoreService(services) {}

void DefaultLinker::link(workspace::Document &document,
                         const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);

  if (!document.hasAst()) {
    return;
  }

  // Resolve references in parallel only when this build runs standalone (on a
  // non-worker thread). A multi-document build already saturates the pool via
  // its cross-document parallelism, so nested dispatch there is pure overhead;
  // running on a worker signals exactly that case, and a maximal minimum keeps
  // parallelFor inline. forceResolve reads the immutable post-scope index
  // without locking and writes only the reference's own target, so distinct
  // references resolve concurrently without contention.
  auto &scheduler = *services.shared.taskScheduler;
  constexpr std::size_t kMinParallelRefs = 32;
  const std::size_t minParallelCount =
      scheduler.onWorkerThread() ? std::numeric_limits<std::size_t>::max()
                                 : kMinParallelRefs;
  scheduler.parallelFor(
      cancelToken, document.parseResult.references,
      [](const auto &handle) { handle.get()->forceResolve(); }, minParallelCount);

  utils::throw_if_cancelled(cancelToken);
}

void DefaultLinker::unlink(workspace::Document &document) const {
  for (const auto &handle : document.parseResult.references) {
    handle.get()->clearLinkState();
  }
}

workspace::LinkingError
DefaultLinker::createLinkingError(const ReferenceInfo &reference) const {
  const auto *document = reference.container == nullptr
                             ? nullptr
                             : std::addressof(getDocument(*reference.container));
  const bool retryable =
      document != nullptr &&
      document->state < workspace::DocumentState::ComputedScopes;
  if (retryable) {
    log_reference_resolution_problem(
        services.shared, document,
        "Attempted reference resolution before document reached "
        "ComputedScopes state (" +
            document->uri + ").",
        document->state);
  }
  return workspace::LinkingError{
      .info = reference,
      .kind = retryable ? workspace::LinkingErrorKind::Retryable
                        : workspace::LinkingErrorKind::NotFound};
}

workspace::LinkingError DefaultLinker::createExceptionLinkingError(
    const ReferenceInfo &reference, const std::string &message) const {
  log_reference_resolution_problem(
      services.shared,
      reference.container == nullptr
          ? nullptr
          : std::addressof(getDocument(*reference.container)),
      "An error occurred while resolving reference to '" +
          std::string(reference.referenceText) + "': " + message);
  return workspace::LinkingError{.info = reference,
                                 .kind = workspace::LinkingErrorKind::Exception};
}

workspace::AstNodeDescriptionOrError
DefaultLinker::getCandidate(const ReferenceInfo &reference) const {
  const auto *scopeProvider = services.references.scopeProvider.get();
  if (const auto *candidate = scopeProvider->getScopeEntry(reference);
      candidate != nullptr) {
    return candidate;
  }
  return createLinkingError(reference);
}

workspace::AstNodeDescriptionsOrError
DefaultLinker::getCandidates(const ReferenceInfo &reference) const {
  const auto *scopeProvider = services.references.scopeProvider.get();
  std::vector<const workspace::AstNodeDescription *> descriptions;
  std::unordered_set<workspace::NodeKey, workspace::NodeKeyHash> seen;
  const auto collectDescription =
      [&descriptions, &seen](const workspace::AstNodeDescription &candidate) {
        if (!seen.insert(workspace::NodeKey::of(candidate)).second) {
          return true;
        }
        descriptions.push_back(std::addressof(candidate));
        return true;
      };
  (void)scopeProvider->visitScopeEntries(
      reference,
      utils::function_ref<bool(const workspace::AstNodeDescription &)>(
          collectDescription));
  if (descriptions.empty()) {
    return createLinkingError(reference);
  }
  return descriptions;
}

workspace::ResolvedAstNodeDescriptionOrError
DefaultLinker::getLinkedNode(const ReferenceInfo &reference,
                             const workspace::Document &currentDocument) const {
  const auto candidate = getCandidate(reference);
  if (const auto *error = std::get_if<workspace::LinkingError>(&candidate)) {
    return *error;
  }
  const auto *description =
      std::get<const workspace::AstNodeDescription *>(candidate);
  return workspace::ResolvedAstNodeDescription{
      .node = std::addressof(workspace::resolve_ast_node(
          *services.shared.workspace.documents, *description, currentDocument)),
      .description = description};
}

workspace::ResolvedAstNodeDescriptionsOrError
DefaultLinker::getLinkedNodes(const ReferenceInfo &reference,
                              const workspace::Document &currentDocument) const {
  auto candidates = getCandidates(reference);
  if (const auto *error = std::get_if<workspace::LinkingError>(&candidates)) {
    return *error;
  }
  const auto &descriptions =
      std::get<std::vector<const workspace::AstNodeDescription *>>(candidates);
  std::vector<workspace::ResolvedAstNodeDescription> resolved;
  resolved.reserve(descriptions.size());
  for (const auto *description : descriptions) {
    resolved.push_back(
        {.node = std::addressof(workspace::resolve_ast_node(
             *services.shared.workspace.documents, *description,
             currentDocument)),
         .description = description});
  }
  return resolved;
}

template <typename Fn>
auto DefaultLinker::withReferenceInfo(const AbstractReference &reference,
                                     Fn &&body) const
    -> std::invoke_result_t<Fn &, const ReferenceInfo &,
                            const workspace::Document &> {
  const auto info = makeReferenceInfo(reference);
  try {
    assert(reference.getContainer() != nullptr);
    const auto &currentDocument = getDocument(*reference.getContainer());
    return body(info, currentDocument);
  } catch (const CyclicReferenceResolution &cycle) {
    return workspace::LinkingError{
        .info = makeReferenceInfo(cycle.reference()),
        .kind = workspace::LinkingErrorKind::Cycle};
  } catch (const utils::OperationCancelled &) {
    // Cooperative cancellation must propagate, not be swallowed into a
    // per-reference LinkingError (which would let the build report success).
    throw;
  } catch (const std::exception &error) {
    return createExceptionLinkingError(info, error.what());
  }
}

workspace::ResolvedAstNodeDescriptionOrError
DefaultLinker::resolve(const AbstractSingleReference &reference) const {
  return withReferenceInfo(
      reference, [this](const ReferenceInfo &info,
                        const workspace::Document &currentDocument) {
        return getLinkedNode(info, currentDocument);
      });
}

workspace::ResolvedAstNodeDescriptionsOrError
DefaultLinker::resolveAll(const AbstractMultiReference &reference) const {
  return withReferenceInfo(
      reference, [this](const ReferenceInfo &info,
                        const workspace::Document &currentDocument) {
        return getLinkedNodes(info, currentDocument);
      });
}

} // namespace pegium::references
