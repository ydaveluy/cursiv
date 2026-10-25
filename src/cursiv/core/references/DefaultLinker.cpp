#include "cursiv/core/references/DefaultLinker.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/references/LinkingError.hpp"
#include "cursiv/core/references/LinkingResult.hpp"
#include "cursiv/core/references/ScopeProvider.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"

namespace cursiv {

namespace {

// The name of what the member declares first, and `?` whenever anything on the
// way is absent.
std::string_view first_target_name(const AstReflection& reflection,
                                   std::uint16_t reference) {
  const std::span<const std::uint16_t> targets =
      reflection.referenceTargets(reference);
  if (targets.empty()) return "?";
  const AstTypeTag* const tag = reflection.typeTagOf(targets.front());
  return tag != nullptr ? tag->name : "?";
}

}  // namespace

// Appended, not formatted: written for every failed reference, twice -- by the
// linker and by the validator.
std::string unresolved_reference_message(const AstReflection& reflection,
                                         std::uint16_t reference,
                                         std::uint8_t narrow,
                                         std::string_view text) {
  constexpr std::string_view kHead = "Could not resolve reference to ";
  constexpr std::string_view kExpecting = "Expecting ";
  constexpr std::string_view kNamed = " named '";
  constexpr std::string_view kTail = "'.";
  const AstTypeTag* const narrowed =
      reflection.referenceNarrowTarget(reference, narrow);
  const std::string_view target =
      narrowed != nullptr ? narrowed->name
                          : first_target_name(reflection, reference);
  std::string message;
  message.reserve(kHead.size() + target.size() + kNamed.size() + text.size() +
                  kTail.size());
  message.append(narrowed != nullptr ? kExpecting : kHead)
      .append(target)
      .append(kNamed)
      .append(text)
      .append(kTail);
  return message;
}

LinkingError DefaultLinker::createLinkingError(
    const ReferenceInfo& reference,
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
    services.shared.observabilitySink->publish(
        {.severity = ObservationSeverity::Warning,
         .code = ObservationCode::ScopeReadBeforeComputed,
         .message = "reference resolution before scopes were computed",
         .uri = document->uri,
         .languageId = {}});
  return {
      unresolved_reference_message(*services.astReflection, reference.reference,
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
  references.forceAll(cancelToken);
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

  const AstNodeDescription* const found =
      services.scopeProvider->getScopeEntry(reference, reference.referenceText);
  if (found != nullptr) return found;

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
  // One answer per symbol, keyed as a description names it -- (document,
  // symbol): an outer scope re-surfaces what an inner one already gave, and a
  // multi-reference must not hold a target twice. A few are compared in
  // place; past them, a set keeps a thousand homonyms linear.
  constexpr std::size_t kCompared = 16;
  const auto key = [](const AstNodeDescription& entry) {
    return (static_cast<std::uint64_t>(entry.documentId) << 32) |
           entry.symbolId;
  };
  std::unordered_set<std::uint64_t> seen;
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
