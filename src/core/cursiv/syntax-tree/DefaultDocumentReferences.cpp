#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/references/Linker.hpp"
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

// What a provider threw, said in words.
std::string what_of(const std::exception_ptr& failure) noexcept {
  try {
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception& thrown) {
      return thrown.what();
    } catch (...) {  // NOLINT(bugprone-empty-catch): the answer below says it
    }
    return "unknown exception";
  } catch (...) {  // NOLINT(bugprone-empty-catch): nothing left to say it with
    return {};
  }
}

// A throwing provider is the language's bug, not the document's: reported to
// the observability sink of the document's language, if it has one.
void say_provider_threw(const Document* document, std::string_view name,
                        std::string_view what) noexcept {
  if (document == nullptr || document->language == nullptr) return;
  const CoreServices& language = *document->language;
  try {
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

}  // namespace

DocumentReferences::Answer DefaultDocumentReferences::resolve(
    std::uint32_t rank) const try {
  if (rank >= handles.size()) return {};
  const detail::RefHandle& h = handles[rank];
  if (h.owner == nullptr) return {};
  // Before the document reaches `ComputedScopes`, a failure is not kept: the
  // reference stays `Unresolved` and is asked again later.
  const Document* at = document.load(std::memory_order_acquire);
  if (at == nullptr) at = h.owner->getDocument();
  const bool early = at != nullptr && at->state.load(std::memory_order_acquire) <
                                          DocumentState::ComputedScopes;
  const auto failed = [this, early](Answer failure = {}) {
    if (early) return Answer{nullptr, RefState::Unresolved, 0};
    _anyFailed.store(true, std::memory_order_release);
    return failure;
  };
  const Linker* const l = linker.load(std::memory_order_acquire);
  if (l == nullptr) return failed();
  const detail::RefOps& ops = _f.refOps[h.field];
  // Read once for the site and the filter, and only when the member declares
  // narrowings, so the common case skips the indirect call.
  const std::uint8_t narrow =
      ops.narrow_count != 0 ? ops.narrow_of(*h.owner, h.index) : kNoNarrow;

  const auto name = ops.text(*h.owner, h.index);
  const AstNodeDescription* found;
  try {
    found = l->getCandidate(info(h, name, narrow));
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    keepFailure(rank, at, name);
    return failed({nullptr, RefState::ErrorThrown, 0});
  }
  if (found == nullptr) return failed();

  AstNode* const target = found->node;
  if (target == nullptr) return failed();
  assert(found->type == target->type &&
         "a description carries the type of its node");
  // A target the site cannot hold is not an answer: the scope decided it was
  // visible, the site -- its narrowing included -- decides it fits, by the type
  // the description carries: the node itself is not read.
  const std::uint8_t which =
      detail::RefFilter{&ops, narrow}.branch(found->type);
  if (which == detail::kNoBranch) return failed();
  // Named: a braced return zeroes the padding through the stack first.
  const Answer resolved{target, RefState::Resolved, which};
  return resolved;
} catch (const OperationCancelled&) {
  throw;
} catch (...) {
  // What leaves here is published as a failure by the resolution guard.
  _anyFailed.store(true, std::memory_order_release);
  throw;
}

void DefaultDocumentReferences::forceAt(const Cst& cst, TextOffset offset,
                                        const CancellationToken& cancelToken) {
  // Each rank is a run of one in the work list.
  for (const std::uint32_t rank : ranksIn(cst, offset, offset))
    force(std::span(handles).subspan(rank, 1), cancelToken);
}

void DefaultDocumentReferences::visitResolved(
    const Cst& cst, ResolvedReferenceVisitor visitor) const {
  for (const detail::RefHandle& h : handles) forEachResolved(cst, h, visitor);
}

void DefaultDocumentReferences::forEachResolved(
    const Cst& cst, const detail::RefHandle& h,
    ResolvedReferenceVisitor visitor) const {
  if (h.owner == nullptr) return;
  _f.refOps[h.field].visit(*h.owner, h.index, cst, visitor);
}

bool DefaultDocumentReferences::anyUnresolved() const noexcept {
  return _anyFailed.load(std::memory_order_acquire);
}

void DefaultDocumentReferences::visitUnresolved(
    const Cst& cst, UnresolvedReferenceVisitor visitor) const {
  if (!anyUnresolved()) return;
  for (const detail::RefHandle& h : handles) forEachUnresolved(cst, h, visitor);
}

void DefaultDocumentReferences::forEachUnresolved(
    const Cst& cst, const detail::RefHandle& h,
    UnresolvedReferenceVisitor visitor) const {
  if (h.owner == nullptr) return;
  const detail::RefOps& ops = _f.refOps[h.field];
  const RefState why = ops.state(*h.owner, h.index);
  if (!hasError(why)) return;
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
  ReferenceInfo site = info(h, ops.text(*h.owner, h.index), narrow);
  if (why == RefState::ErrorThrown) {
    const std::scoped_lock lock(_failuresLock);
    const auto rank = static_cast<std::uint32_t>(&h - handles.data());
    if (const auto at = _failures.find(rank); at != _failures.end())
      site.failure = at->second;
  }
  visitor(why, begin, end, site);
}

void DefaultDocumentReferences::keepFailure(std::uint32_t rank,
                                            const Document* document,
                                            std::string_view name) const {
  auto what = what_of(std::current_exception());
  say_provider_threw(document, name, what);
  try {
    const std::scoped_lock lock(_failuresLock);
    _failures.insert_or_assign(rank, std::move(what));
  } catch (...) {  // NOLINT(bugprone-empty-catch): the diagnostic says less
  }
}

void DefaultDocumentReferences::visitIn(
    const Cst& cst, TextOffset begin, TextOffset end,
    ResolvedReferenceVisitor resolved,
    UnresolvedReferenceVisitor unresolved) const {
  const std::vector<std::uint32_t> at = ranksIn(cst, begin, end);
  for (const std::uint32_t rank : at)
    forEachResolved(cst, handles[rank], resolved);
  for (const std::uint32_t rank : at)
    forEachUnresolved(cst, handles[rank], unresolved);
}

std::vector<std::uint32_t> DefaultDocumentReferences::ranksIn(
    const Cst& cst, TextOffset begin, TextOffset end) const {
  if (!_indexed.load(std::memory_order_acquire)) [[unlikely]]
    indexNames(cst);
  std::vector<std::uint32_t> at;
  // Every name that begins at or before `end` lies before `from`. Walking
  // back, a reach before `begin` says no earlier name ends inside the window.
  auto from = std::ranges::upper_bound(_names, end, {}, &NameAt::begin);
  while (from != _names.begin() && (--from)->reach >= begin) {
    const detail::RefHandle& h = handles[from->rank];
    if (cst[_f.refOps[h.field].name_id(*h.owner, h.index)].end >= begin)
      at.push_back(from->rank);
  }
  std::ranges::sort(at);
  return at;
}

void DefaultDocumentReferences::indexNames(const Cst& cst) const {
  const std::scoped_lock lock(_indexing);
  if (_indexed.load(std::memory_order_relaxed)) return;
  // Cleared, not appended to: a build that threw left it half-written.
  _names.clear();
  _names.reserve(handles.size());
  for (std::uint32_t rank = 0; rank < handles.size(); ++rank) {
    const detail::RefHandle& h = handles[rank];
    if (h.owner == nullptr) continue;
    const NodeId at = _f.refOps[h.field].name_id(*h.owner, h.index);
    if (at != kNoNode) _names.push_back({cst[at].begin, cst[at].end, rank});
  }
  // By begin, then by rank: names that begin together keep the list's order.
  std::ranges::sort(_names, {}, [](const NameAt& name) {
    return std::pair(name.begin, name.rank);
  });
  // Each entry was made with its own end, and becomes the furthest end so far.
  TextOffset reach = 0;
  for (NameAt& name : _names) {
    reach = std::max(reach, name.reach);
    name.reach = reach;
  }
  _indexed.store(true, std::memory_order_release);
}

void DefaultDocumentReferences::reset() noexcept {
  for (const detail::RefHandle& h : handles)
    if (h.owner != nullptr) _f.refOps[h.field].reset(*h.owner, h.index);
  _anyFailed.store(false, std::memory_order_release);
  const std::scoped_lock lock(_failuresLock);
  _failures.clear();
}

RefState DefaultDocumentReferences::resolveAll(std::uint32_t rank,
                                               detail::MultiRefItems& out) const
    try {
  out.clear();
  if (rank >= handles.size()) return RefState::ErrorNotFound;
  const detail::RefHandle& h = handles[rank];
  if (h.owner == nullptr) return RefState::ErrorNotFound;
  const auto failed = [this](RefState how) {
    _anyFailed.store(true, std::memory_order_release);
    return how;
  };
  const Linker* const l = linker.load(std::memory_order_acquire);
  if (l == nullptr) return failed(RefState::ErrorNotFound);
  const detail::RefOps& ops = _f.refOps[h.field];

  const std::uint8_t narrow =
      ops.narrow_count != 0 ? ops.narrow_of(*h.owner, h.index) : kNoNarrow;
  const Document* at = document.load(std::memory_order_acquire);
  if (at == nullptr) at = h.owner->getDocument();
  const auto name = ops.text(*h.owner, h.index);
  std::vector<const AstNodeDescription*> found;
  try {
    found = l->getCandidates(info(h, name, narrow));
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    keepFailure(rank, at, name);
    return failed(RefState::ErrorThrown);
  }

  // Filled in place: a relink reuses the storage the reset kept.
  const detail::RefFilter filter{&ops, narrow};
  for (const AstNodeDescription* const entry : found) {
    if (entry == nullptr || entry->node == nullptr) continue;
    assert(entry->type == entry->node->type &&
           "a description carries the type of its node");
    const std::uint8_t which = filter.branch(entry->type);
    if (which != detail::kNoBranch) out.push_back({entry->node, which});
  }
  return out.empty() ? failed(RefState::ErrorNotFound) : RefState::Resolved;
} catch (const OperationCancelled&) {
  throw;
} catch (...) {
  _anyFailed.store(true, std::memory_order_release);
  throw;
}

ReferenceInfo DefaultDocumentReferences::info(const detail::RefHandle& h,
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
