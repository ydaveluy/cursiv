#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/references/Linker.hpp"
#include "cursiv/references/LinkingResult.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentState.hpp"

namespace cursiv {

namespace {

// A throwing provider is the language's bug, not the document's: said to the
// observability sink of the document's language, if it has one.
void say_provider_threw(const Document* document, std::string_view name,
                        const std::exception_ptr& failure) noexcept {
  if (document == nullptr || document->language == nullptr) return;
  const CoreServices& language = *document->language;
  try {
    std::string what = "unknown exception";
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception& thrown) {
      what = thrown.what();
    } catch (...) {  // NOLINT(bugprone-empty-catch): `what` already says it
    }
    language.shared().observabilitySink->publish(
        {.severity = ObservationSeverity::Warning,
         .code = ObservationCode::ReferenceResolutionThrew,
         .message = std::format("resolving the reference to '{}' threw: {}",
                                name, what),
         .uri = document->uri,
         .languageId = std::string(language.languageMetaData.languageId)});
  } catch (...) {  // NOLINT(bugprone-empty-catch): nothing left to tell it to
  }
}

// A provider that throws fails the reference it was asked for, as a lookup
// that found nothing does: the empty answer is returned, and a provider that
// read this reference goes on. Only cancellation leaves the read.
template <class Lookup>
auto contained(const Document* document, std::string_view name, Lookup lookup)
    -> decltype(lookup()) {
  try {
    return lookup();
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    say_provider_threw(document, name, std::current_exception());
    return {};
  }
}

}  // namespace

DocumentReferences::Answer DefaultDocumentReferences::resolve(
    std::uint32_t rank) const {
  if (rank >= handles.size()) return {};
  const RefHandle& h = handles[rank];
  if (h.owner == nullptr) return {};
  // The rung before the linker, and the rung judges: a document seen at
  // `ComputedScopes` has published its linker, and one it reaches during the
  // lookup must not keep a failure that began below it.
  const Document* at = document.load(std::memory_order_acquire);
  if (at == nullptr) at = h.owner->getDocument();
  const bool early = at != nullptr && at->state.load(std::memory_order_acquire) <
                                          DocumentState::ComputedScopes;
  const auto failed = [early](Answer failure = {}) {
    return early ? Answer{nullptr, RefState::Unresolved, 0} : failure;
  };
  const Linker* const l = linker.load(std::memory_order_acquire);
  if (l == nullptr) return failed();
  const RefOps& ops = _f.refOps[h.field];
  // Read once for the site and the filter both -- and only when the member
  // declares narrowings at all: the common field pays a predictable branch,
  // never the indirect call.
  const std::uint8_t narrow =
      ops.narrow_count != 0 ? ops.narrow_of(*h.owner, h.index) : kNoNarrow;

  const auto name = ops.text(*h.owner, h.index);
  const AstNodeDescriptionOrError found = contained(
      at, name, [&] { return l->getCandidate(info(h, name, narrow)); });
  if (!found.has_value() || *found == nullptr) return failed();

  AstNode* const target = (*found)->node;
  if (target == nullptr) return failed();
  // A target the site cannot hold is not an answer: the scope decided it was
  // visible, the site -- its narrowing included -- decides it fits.
  const std::uint8_t which =
      detail::RefFilter{&ops, narrow}.branch(target->type);
  if (which == kNoBranch) return failed();
  // Named: a braced return zeroes the padding through the stack first.
  const Answer resolved{target, RefState::Resolved, which};
  return resolved;
}

void DefaultDocumentReferences::forceAt(const Cst& cst, TextOffset offset,
                                        const CancellationToken& cancelToken) {
  std::vector<RefHandle> at;
  for (const std::uint32_t rank : ranks_in(cst, offset, offset))
    at.push_back(handles[rank]);
  force(at, cancelToken);
}

void DefaultDocumentReferences::visitResolved(
    const Cst& cst, ResolvedReferenceVisitor visitor) const {
  for (const RefHandle& h : handles) visit_resolved(cst, h, visitor);
}

void DefaultDocumentReferences::visit_resolved(
    const Cst& cst, const RefHandle& h,
    ResolvedReferenceVisitor visitor) const {
  if (h.owner == nullptr) return;
  const RefOps& ops = _f.refOps[h.field];
  // A single reference, the common site, is read in one call.
  if (!ops.multi) {
    const detail::SingleRefRead read = ops.single(*h.owner, h.index);
    if (read.target == nullptr || read.name == kNoNode) return;
    const CstNode& n = cst[read.name];
    visitor(n.begin, n.end, *read.target, false);
    return;
  }
  const std::size_t count = ops.size(*h.owner, h.index);
  if (count == 0) return;
  const NodeId named = ops.name_id(*h.owner, h.index);
  if (named == kNoNode) return;
  const CstNode& n = cst[named];
  for (std::size_t k = 0; k < count; ++k)
    if (const AstNode* const target = ops.at(*h.owner, h.index, k))
      visitor(n.begin, n.end, *target, ops.multi);
}

bool DefaultDocumentReferences::anyUnresolved() const noexcept {
  return std::ranges::any_of(handles, [&](const RefHandle& h) {
    return h.owner != nullptr &&
           has_error(_f.refOps[h.field].state(*h.owner, h.index));
  });
}

void DefaultDocumentReferences::visitUnresolved(
    const Cst& cst, UnresolvedReferenceVisitor visitor) const {
  for (const RefHandle& h : handles) visit_unresolved(cst, h, visitor);
}

void DefaultDocumentReferences::visit_unresolved(
    const Cst& cst, const RefHandle& h,
    UnresolvedReferenceVisitor visitor) const {
  if (h.owner == nullptr) return;
  const RefOps& ops = _f.refOps[h.field];
  const RefState why = ops.state(*h.owner, h.index);
  if (!has_error(why)) return;
  // Anchored on the name and not on the node that holds it: the word the
  // editor underlines is the one the text was read from.
  TextOffset begin = 0;
  TextOffset end = 0;
  if (const NodeId at = ops.name_id(*h.owner, h.index); at != kNoNode) {
    begin = cst[at].begin;
    end = cst[at].end;
  }
  const std::uint8_t narrow =
      ops.narrow_count != 0 ? ops.narrow_of(*h.owner, h.index) : kNoNarrow;
  visitor(why, begin, end, info(h, ops.text(*h.owner, h.index), narrow));
}

void DefaultDocumentReferences::visitIn(
    const Cst& cst, TextOffset begin, TextOffset end,
    ResolvedReferenceVisitor resolved,
    UnresolvedReferenceVisitor unresolved) const {
  const std::vector<std::uint32_t> at = ranks_in(cst, begin, end);
  for (const std::uint32_t rank : at)
    visit_resolved(cst, handles[rank], resolved);
  for (const std::uint32_t rank : at)
    visit_unresolved(cst, handles[rank], unresolved);
}

std::vector<std::uint32_t> DefaultDocumentReferences::ranks_in(
    const Cst& cst, TextOffset begin, TextOffset end) const {
  if (!_indexed.load(std::memory_order_acquire)) [[unlikely]]
    index_names(cst);
  std::vector<std::uint32_t> at;
  // Every name that begins at or before `end` lies before `from`. Walking
  // back, a reach before `begin` says no earlier name ends inside the window.
  auto from = std::ranges::upper_bound(_names, end, {}, &NameAt::begin);
  while (from != _names.begin() && (--from)->reach >= begin) {
    const RefHandle& h = handles[from->rank];
    if (cst[_f.refOps[h.field].name_id(*h.owner, h.index)].end >= begin)
      at.push_back(from->rank);
  }
  std::ranges::sort(at);
  return at;
}

void DefaultDocumentReferences::index_names(const Cst& cst) const {
  const std::scoped_lock lock(_indexing);
  if (_indexed.load(std::memory_order_relaxed)) return;
  // Cleared, not appended to: a build that threw left it half-written.
  _names.clear();
  _names.reserve(handles.size());
  for (std::uint32_t rank = 0; rank < handles.size(); ++rank) {
    const RefHandle& h = handles[rank];
    if (h.owner == nullptr) continue;
    const NodeId at = _f.refOps[h.field].name_id(*h.owner, h.index);
    if (at != kNoNode) _names.push_back({cst[at].begin, cst[at].end, rank});
  }
  // Stable, so names that begin together keep the list's order.
  std::ranges::stable_sort(_names, {}, &NameAt::begin);
  // Each entry was made with its own end, and becomes the furthest end so far.
  TextOffset reach = 0;
  for (NameAt& name : _names) {
    reach = std::max(reach, name.reach);
    name.reach = reach;
  }
  _indexed.store(true, std::memory_order_release);
}

void DefaultDocumentReferences::reset() noexcept {
  for (const RefHandle& h : handles)
    if (h.owner != nullptr) _f.refOps[h.field].reset(*h.owner, h.index);
}

RefState DefaultDocumentReferences::resolveAll(std::uint32_t rank,
                                               MultiRefItems& out) const {
  out.clear();
  const Linker* const l = linker.load(std::memory_order_acquire);
  if (l == nullptr || rank >= handles.size()) return RefState::ErrorNotFound;
  const RefHandle& h = handles[rank];
  if (h.owner == nullptr) return RefState::ErrorNotFound;
  const RefOps& ops = _f.refOps[h.field];

  const std::uint8_t narrow =
      ops.narrow_count != 0 ? ops.narrow_of(*h.owner, h.index) : kNoNarrow;
  const Document* at = document.load(std::memory_order_acquire);
  if (at == nullptr) at = h.owner->getDocument();
  const auto name = ops.text(*h.owner, h.index);
  const AstNodeDescriptionsOrError found = contained(
      at, name, [&] { return l->getCandidates(info(h, name, narrow)); });
  if (!found.has_value()) return RefState::ErrorNotFound;

  const detail::RefFilter filter{&ops, narrow};
  std::vector<MultiRefItem> found_targets;
  for (const AstNodeDescription* const entry : *found) {
    if (entry == nullptr || entry->node == nullptr) continue;
    const std::uint8_t which = filter.branch(entry->node->type);
    if (which != kNoBranch) found_targets.push_back({entry->node, which});
  }
  if (found_targets.empty()) return RefState::ErrorNotFound;
  out = std::move(found_targets);
  return RefState::Resolved;
}

ReferenceInfo DefaultDocumentReferences::info(const RefHandle& h,
                                              std::string_view text,
                                              std::uint8_t narrow) const {
  const detail::FieldDesc& field = _f.fields[h.field];
  return {.container = h.owner,
          .document = document.load(std::memory_order_acquire),
          .referenceText = text,
          .property = field.name,
          .index = field.storage == detail::FieldStorage::vector
                       ? std::optional<std::uint32_t>(h.index)
                       : std::nullopt,
          .reference = h.field,
          .narrow = narrow};
}

}  // namespace cursiv
