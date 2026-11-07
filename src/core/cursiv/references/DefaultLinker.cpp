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
#include "cursiv/references/LinkingError.hpp"
#include "cursiv/references/LinkingResult.hpp"
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

// The fewest references a worker forces at once: enough that handing them out
// costs nothing next to the lookups they make, few enough that a document of a
// few thousand references already spreads over the pool.
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

// It names what is expected -- the site's narrowing when it has one, every
// type the member declares otherwise -- and the name read, so the reader
// learns what the site wanted. Appended, not formatted: written for every
// failed reference, twice -- for the linker's own error and for the
// diagnostic.
std::string not_found_message(const AstReflection& reflection,
                              std::uint16_t reference, std::uint8_t narrow,
                              std::string_view text) {
  constexpr std::string_view kHead = "Could not resolve reference to ";
  constexpr std::string_view kExpecting = "Expecting ";
  constexpr std::string_view kNamed = " named '";
  constexpr std::string_view kTail = "'.";
  const AstTypeTag* const narrowed =
      reflection.referenceNarrowTarget(reference, narrow);
  std::string message;
  message.reserve(kHead.size() + 32 + kNamed.size() + text.size() +
                  kTail.size());
  message.append(narrowed != nullptr ? kExpecting : kHead);
  if (narrowed != nullptr)
    message.append(narrowed->name);
  else
    append_target_names(message, reflection, reference);
  message.append(kNamed).append(text).append(kTail);
  return message;
}

}  // namespace

LinkingError DefaultLinker::createLinkingError(
    const ReferenceInfo& reference, RefState why,
    const AstNodeDescription* description) const {
  // A lookup that failed below `ComputedScopes` ran on a linker planted
  // before the scopes, as a direct `link` does: said at the sink, a single
  // reference being read again and a plural one keeping its failure. The
  // document is climbed to when the site does not carry it: a hand-filled
  // site still deserves the warning.
  const Document* document = reference.document;
  if (document == nullptr && reference.container != nullptr)
    document = reference.container->getDocument();
  if (document != nullptr && document->state.load(std::memory_order_relaxed) <
                                 DocumentState::ComputedScopes)
    shared.observabilitySink->publish(
        {.severity = ObservationSeverity::Warning,
         .code = ObservationCode::ScopeReadBeforeComputed,
         .message = "reference resolution before scopes were computed",
         .uri = document->uri,
         .languageId = {}});
  if (why == RefState::ErrorCycle)
    return {std::format("Cyclic reference resolution detected for '{}'.",
                        reference.referenceText),
            reference, description};
  return {not_found_message(*services.astReflection, reference.reference,
                            reference.narrow, reference.referenceText),
          reference, description};
}

void DefaultLinker::link(Document& document,
                         const CancellationToken& cancelToken) const {
  DocumentReferences* const table = document.parseResult.references.get();
  if (table == nullptr) return;  // no tree, no work list, nothing to link
  DocumentReferences& references = *table;
  // The builder binds every home before linking starts. Leave those shared
  // pointers untouched while another document may resolve through this one.
  // Direct callers can still link a document without using the builder.
  if (references.linker != this) references.linker = this;
  if (references.document != &document) references.document = &document;
  // A long list is forced in runs spread over the pool, so a large document
  // does not link on one thread while the others wait for it.
  const auto force = [&](std::span<const RefHandle> run) {
    references.force(run, cancelToken);
  };
  shared.taskScheduler->parallelFor(cancelToken, std::span(references.handles),
                                    kLinkRun, force);
}

// A document with no tree has nothing to drop, and it is reached: a reset to
// `Changed` drops the parse result, and `resetToState` unlinks below `Linked`
// whatever the document was carrying -- a text installed in place arrives here
// already emptied.
void DefaultLinker::unlink(Document& document) const {
  DocumentReferences* const table = document.parseResult.references.get();
  if (table == nullptr) return;
  DocumentReferences& references = *table;
  references.reset();
  references.importedNames.clear();
  references.linker = nullptr;
  references.document = nullptr;
}

AstNodeDescriptionOrError DefaultLinker::getCandidate(
    const ReferenceInfo& reference) const {
  // A reference that spells nothing names nothing: an error here, not an empty
  // scope -- worded like any failed lookup, so one override rewords both.
  if (reference.referenceText.empty())
    return createLinkingError(reference);

  if (const AstNodeDescription* const found =
          services.scopeProvider->getScopeEntry(reference,
                                                reference.referenceText);
      found != nullptr)
    return found;

  return createLinkingError(reference);
}

AstNodeDescriptionsOrError DefaultLinker::getCandidates(
    const ReferenceInfo& reference) const {
  if (reference.referenceText.empty())
    return createLinkingError(reference);

  // Every visible homonym the member can hold, in declaration order. What
  // counts as "named that" is the scope provider's answer -- the case policy
  // lives there, and a comparison here would resurrect the default.
  std::vector<const AstNodeDescription*> candidates;
  // One answer per declaration: an outer scope re-surfaces what an inner one
  // already gave, and a multi-reference must not hold a target twice. The key
  // is (document, symbol) and the node: two nodes built on one CST record
  // share a symbol and are still two targets. A few are compared in place;
  // past them, a set keeps a thousand homonyms linear.
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
  (void)services.scopeProvider->visitScopeEntries(
      reference, reference.referenceText, [&](const AstNodeDescription& entry) {
        if (fresh(entry)) candidates.push_back(&entry);
        return true;
      });
  if (candidates.empty()) return createLinkingError(reference);
  return candidates;
}

}  // namespace cursiv
