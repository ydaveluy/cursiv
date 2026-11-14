#include "cursiv/references/DefaultLinker.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentState.hpp"

namespace cursiv {

namespace {

// The fewest references a worker resolves at once: enough that handing them
// out is cheap next to the lookups, few enough that a document of a few
// thousand references already uses the pool.
constexpr std::size_t kLinkRun = 1024;

// What makes two candidates the same answer to a plural lookup.
struct CandidateKey {
  std::uint64_t symbol;  // (document, symbol)
  const AstNode* node;
  bool operator==(const CandidateKey&) const = default;
};
struct CandidateKeyHash {
  std::size_t operator()(const CandidateKey& key) const noexcept {
    return std::hash<std::uint64_t>{}(key.symbol) ^
           std::hash<const AstNode*>{}(key.node);
  }
};

// The name of each type the member declares, in order and joined by ` or `, and
// `?` for anything absent on the way.
void append_target_names(std::string& message, const AstReflection& reflection,
                         std::uint16_t reference) {
  const std::span<const std::uint16_t> targets =
      reflection.referenceTargets(reference);
  if (targets.empty()) message.push_back('?');
  bool first = true;
  for (const std::uint16_t target : targets) {
    if (!first) message.append(" or ");
    first = false;
    const AstTypeTag* const tag = reflection.typeTagOf(target);
    message.append(tag != nullptr ? tag->name : std::string_view("?"));
  }
}

// Names what the site expected, its narrowing or every type the member
// declares, and the name read.
std::string not_found_message(const AstReflection& reflection,
                              std::uint16_t reference, std::uint8_t narrow,
                              std::string_view text) {
  constexpr std::string_view kHead = "Could not resolve reference to ";
  constexpr std::string_view kNamed = " named '";
  constexpr std::string_view kTail = "'.";
  const AstTypeTag* const narrowed =
      reflection.referenceNarrowTarget(reference, narrow);
  std::string message;
  message.reserve(kHead.size() + 32 + kNamed.size() + text.size() +
                  kTail.size());
  message.append(kHead);
  if (narrowed != nullptr)
    message.append(narrowed->name);
  else
    append_target_names(message, reflection, reference);
  message.append(kNamed).append(text).append(kTail);
  return message;
}

// A lookup that failed before its document reached `ComputedScopes`, as a
// direct `link` call can, is a warning to the sink.
void say_if_early(const ReferenceInfo& reference, ObservabilitySink& sink) {
  const Document* const document = detail::document_of(reference);
  if (document != nullptr && document->state.load(std::memory_order_relaxed) <
                                 DocumentState::ComputedScopes)
    sink.publish({.severity = ObservationSeverity::Warning,
                  .code = ObservationCode::ScopeReadBeforeComputed,
                  .message = "reference resolution before scopes were computed",
                  .uri = document->uri,
                  .languageId = {}});
}

}  // namespace

LinkingError DefaultLinker::createLinkingError(
    const ReferenceInfo& reference, LinkingFailure why,
    const AstNodeDescription* targetDescription) const {
  const auto message = [&]() -> std::string {
    using enum LinkingFailure;
    switch (why) {
      case Cycle:
        return std::format("Cyclic reference resolution detected for '{}'.",
                           reference.referenceText);
      case Threw:
        if (reference.failure.empty())
          return std::format(
              "An error occurred while resolving reference to '{}'.",
              reference.referenceText);
        return std::format(
            "An error occurred while resolving reference to '{}': {}",
            reference.referenceText, reference.failure);
      case NotFound:
        break;
    }
    return not_found_message(*services.astReflection, reference.reference,
                             reference.narrow, reference.referenceText);
  };
  return {message(), reference, targetDescription};
}

void DefaultLinker::link(Document& document,
                         const CancellationToken& cancelToken) const {
  DocumentReferences* const table = document.parseResult.references.get();
  if (table == nullptr) return;  // no tree, no work list, nothing to link
  DocumentReferences& references = *table;
  // The builder sets these before linking starts, and they must not be written
  // while another document may resolve through this one; a direct call sets
  // them here.
  if (references.linker != this) references.linker = this;
  if (references.document != &document) references.document = &document;
  // A long list is resolved in runs spread over the pool, so a large document
  // does not link on one thread while the others wait for it.
  const auto force = [&](std::span<const detail::RefHandle> run) {
    references.force(run, cancelToken);
  };
  shared.taskScheduler->parallelFor(cancelToken, std::span(references.handles),
                                    kLinkRun, force);
}

// A document with no tree has nothing to drop, and is reached: a reset to
// `Changed` drops the parse before the unlink.
void DefaultLinker::unlink(Document& document) const {
  DocumentReferences* const table = document.parseResult.references.get();
  if (table == nullptr) return;
  DocumentReferences& references = *table;
  references.reset();
  references.importedNames.clear();
}

const AstNodeDescription* DefaultLinker::getCandidate(
    const ReferenceInfo& reference) const {
  const AstNodeDescription* const found =
      reference.referenceText.empty() ? nullptr
                                      : services.scopeProvider->getScopeEntry(
                                            reference, reference.referenceText);
  if (found == nullptr) say_if_early(reference, *shared.observabilitySink);
  return found;
}

std::vector<const AstNodeDescription*> DefaultLinker::getCandidates(
    const ReferenceInfo& reference) const {
  // Every visible entry of that name the member can hold, in declaration
  // order. The scope provider decides what matches the name: it owns the case
  // policy.
  std::vector<const AstNodeDescription*> candidates;
  // One answer per declaration, keyed by (document, symbol) and node: an outer
  // scope repeats what an inner one gave. A few are compared in place, then a
  // set keeps many homonyms linear.
  constexpr std::size_t kCompared = 16;
  const auto key = [](const AstNodeDescription& entry) {
    return CandidateKey{
        (static_cast<std::uint64_t>(entry.documentId) << 32) | entry.symbolId,
        entry.node};
  };
  std::unordered_set<CandidateKey, CandidateKeyHash> seen;
  const auto fresh = [&](const AstNodeDescription& entry) {
    if (candidates.size() < kCompared)
      return std::ranges::none_of(candidates, [&](const auto* held) {
        return key(*held) == key(entry);
      });
    if (seen.empty())
      for (const auto* held : candidates) seen.insert(key(*held));
    return seen.insert(key(entry)).second;
  };
  if (!reference.referenceText.empty())
    (void)services.scopeProvider->visitScopeEntries(
        reference, reference.referenceText,
        [&](const AstNodeDescription& entry) {
          if (fresh(entry)) candidates.push_back(&entry);
          return true;
        });
  if (candidates.empty()) say_if_early(reference, *shared.observabilitySink);
  return candidates;
}

}  // namespace cursiv
