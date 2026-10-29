#include "cursiv/core/syntax-tree/DefaultDocumentReferences.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/core/references/Linker.hpp"
#include "cursiv/core/references/LinkingResult.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"

namespace cursiv {

namespace {

// A provider that throws fails the reference it was asked for, as a lookup
// that found nothing does: the empty answer is returned, and a provider that
// read this reference goes on. Only cancellation leaves the read.
template <class Lookup>
auto contained(Lookup lookup) -> decltype(lookup()) {
  try {
    return lookup();
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
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

  const AstNodeDescriptionOrError found = contained([&] {
    return l->getCandidate(info(h, ops.text(*h.owner, h.index), narrow));
  });
  if (!found.has_value() || *found == nullptr) return failed();

  AstNode* const target = (*found)->node;
  if (target == nullptr) return failed();
  // A target the site cannot hold is not an answer: the scope decided it was
  // visible, the site -- its narrowing included -- decides it fits.
  const std::uint8_t which = RefFilter{&ops, narrow}.branch(target->type);
  if (which == kNoBranch) return failed();
  // Named: a braced return zeroes the padding through the stack first.
  const Answer resolved{target, RefState::Resolved, which};
  return resolved;
}

void DefaultDocumentReferences::forceAll(const CancellationToken& cancelToken) {
  detail::force_all(handles, _f.refOps, cancelToken);
}

void DefaultDocumentReferences::forceAt(const Cst& cst, TextOffset offset,
                                        const CancellationToken& cancelToken) {
  std::vector<RefHandle> at;
  for (const RefHandle& h : handles) {
    if (h.owner == nullptr) continue;
    const NodeId named = _f.refOps[h.field].name_id(*h.owner, h.index);
    if (named != kNoNode && cst[named].begin <= offset &&
        offset <= cst[named].end)
      at.push_back(h);
  }
  detail::force_all(at, _f.refOps, cancelToken);
}

void DefaultDocumentReferences::visitResolved(
    const Cst& cst, ResolvedReferenceVisitor visitor) const {
  for (const RefHandle& h : handles) {
    if (h.owner == nullptr) continue;
    const RefOps& ops = _f.refOps[h.field];
    // A single reference, the common site, is read in one call.
    if (!ops.multi) {
      const SingleRefRead read = ops.single(*h.owner, h.index);
      if (read.target == nullptr || read.name == kNoNode) continue;
      const CstNode& n = cst[read.name];
      visitor(n.begin, n.end, *read.target, false);
      continue;
    }
    const std::size_t count = ops.size(*h.owner, h.index);
    if (count == 0) continue;
    const NodeId named = ops.name_id(*h.owner, h.index);
    if (named == kNoNode) continue;
    const CstNode& n = cst[named];
    for (std::size_t k = 0; k < count; ++k)
      if (const AstNode* const target = ops.at(*h.owner, h.index, k))
        visitor(n.begin, n.end, *target, ops.multi);
  }
}

bool DefaultDocumentReferences::anyUnresolved() const noexcept {
  return std::ranges::any_of(handles, [&](const RefHandle& h) {
    return h.owner != nullptr &&
           has_error(_f.refOps[h.field].state(*h.owner, h.index));
  });
}

void DefaultDocumentReferences::visitUnresolved(
    const Cst& cst, UnresolvedReferenceVisitor visitor) const {
  for (const RefHandle& h : handles) {
    if (h.owner == nullptr) continue;
    const RefOps& ops = _f.refOps[h.field];
    const RefState why = ops.state(*h.owner, h.index);
    if (!has_error(why)) continue;
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
}

void DefaultDocumentReferences::reset() noexcept {
  for (const RefHandle& h : handles)
    if (h.owner != nullptr) _f.refOps[h.field].reset(*h.owner, h.index);
  const std::lock_guard lock(_targetsLock);
  _targets.release();
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
  const AstNodeDescriptionsOrError found = contained([&] {
    return l->getCandidates(info(h, ops.text(*h.owner, h.index), narrow));
  });
  if (!found.has_value()) return RefState::ErrorNotFound;

  const RefFilter filter{&ops, narrow};
  std::vector<MultiRefItem> found_targets;
  for (const AstNodeDescription* const entry : *found) {
    if (entry == nullptr || entry->node == nullptr) continue;
    const std::uint8_t which = filter.branch(entry->node->type);
    if (which != kNoBranch) found_targets.push_back({entry->node, which});
  }
  if (found_targets.empty()) return RefState::ErrorNotFound;
  const std::lock_guard lock(_targetsLock);
  auto* const slice = static_cast<MultiRefItem*>(_targets.allocate(
      found_targets.size() * sizeof(MultiRefItem), alignof(MultiRefItem)));
  std::memcpy(slice, found_targets.data(),
              found_targets.size() * sizeof(MultiRefItem));
  out.data = slice;
  out.count = static_cast<std::uint32_t>(found_targets.size());
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
