// A postorder reducer driven by data: a rule's whole payload has been visited
// when its own record closes, so the reduction is one linear scan of the
// records, one function of `AstActions` per run.
#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstArena.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/Pending.hpp"
#include "cursiv/shared/runtime/ast_write.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/StampLayout.hpp"

namespace cursiv {

namespace detail {

// Where a directive's value is: its record has one visible child, the
// payload's own record. A childless record is its own payload, and so is one
// whose visible children are a collapsed data rule's parts.
inline NodeId payload_node(const Cst& c, NodeId id) {
  assert(c[id].subtreeSize != 0 && c[id].subtreeSize <= id + 1u);
  if (c.childless(id)) return id;
  NodeId only = kNoNode;
  for (const NodeId child : c.reverse_children(id)) {
    if (c[child].hidden) continue;
    if (only != kNoNode) return id;
    only = child;
  }
  return only != kNoNode ? only : id;
}

// Nodes the root no longer reaches leave the list; their references stay as
// tombstones so the surviving ranks stay put. Each node's answer is kept, read
// from the list's end: it is in creation order, and a node is made before the
// one it is wired to, so a parent is mostly answered before its children. A
// chain longer than the list is a cycle, which no root ends.
template <class Check, class Drop>
void prune_orphans(std::pmr::vector<AstNode*>& nodes,
                   std::vector<RefHandle>& handles, const AstNode* root,
                   Check check, Drop drop) {
  const auto key = [](const AstNode* node) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(node));
  };
  FlatMap<std::uint8_t> reaches;
  reaches.emplace(key(root)).first = 1;
  std::vector<const AstNode*> path;
  const auto reached = [&](const AstNode* node) {
    path.clear();
    bool ok = false;
    for (const AstNode* at = node; at != nullptr && path.size() <= nodes.size();
         at = at->parent) {
      check();
      if (const std::uint8_t* known = reaches.find(key(at))) {
        ok = *known != 0;
        break;
      }
      path.push_back(at);
    }
    for (const AstNode* visited : path)
      reaches.emplace(key(visited)).first = ok;
    return ok;
  };
  std::vector<bool> kept(nodes.size());
  for (std::size_t i = nodes.size(); i-- > 0;) kept[i] = reached(nodes[i]);
  // Answered before anything dies: the walk above reads their parents.
  for (RefHandle& handle : handles)
    if (handle.owner != nullptr && !reached(handle.owner))
      handle.owner = nullptr;
  std::size_t n = 0;
  for (std::size_t i = 0; i < nodes.size(); ++i)
    if (kept[i])
      nodes[n++] = nodes[i];
    else
      drop(nodes[i]);
  nodes.resize(n);
}

}  // namespace detail

class AstReducer {
 public:
  // The tree moves once, into the heap cell the result owns: every view the AST
  // records points there, and that address never moves again.
  template <StampLayout L>
  static ParseResult build(const AstActions& actions, Cst&& tree,
                           const CancellationToken& cancelToken = kNoCancel) {
    throw_if_cancelled(cancelToken);
    ParseResult out(std::move(tree));
    out.references = std::make_unique<DefaultDocumentReferences>(*actions.meta);
    out.accepted = out.cst().accepted();
    const auto repairs = out.cst().diagnostics();
    out.diagnostics.assign(repairs.begin(), repairs.end());

    // Only a denied entry is fatal: a document rejected after its entry
    // rule matched still has an AST, and completion needs it. Said as a
    // diagnostic too, like every other build problem. `has_entry()` asks "not
    // kNoNode" and nothing else, so a hand-built tape could hand `run()` a
    // root the tape has no record for.
    const Cst& cst = out.cst();
    const NodeId root = cst.entry_root();
    const char* const refused =
        !cst.has_entry()     ? "the entry rule matched nothing to build from"
        : root >= cst.size() ? "the entry root is outside the tape"
        : cst[root].subtreeSize == 0 || cst[root].subtreeSize > root + 1u
            ? "the entry subtree is outside the tape"
            : nullptr;
    if (refused != nullptr) {
      out.error = cst.has_entry() ? AstBuildError::malformed_cst
                                  : AstBuildError::no_entry;
      out.diagnostics.push_back(
          {.kind = DiagnosticKind::conversion_error, .message = refused});
      throw_if_cancelled(cancelToken);
      return out;
    }
    // A node costs at least one record, so the tape's count bounds the list;
    // what is reserved past the need is never written, so never resident.
    out._nodes.reserve(cst.size());
    if (actions.meta->referenceBound != 0)
      out.references->handles.reserve(cst.size() / 2u);

    AstReducer self(actions, out, L, cancelToken);
    self.run<L>();
    self._ctx.list_made();
    out.error = self._ctx.error;
    if (out.error == AstBuildError::none) {
      out.value = self.result(*self._ctx.cst);
      // On a repaired tape, the repairs gave up every node: their own
      // diagnostics say why.
      if (out.value == nullptr)
        out.error = self._ctx.repaired ? AstBuildError::no_entry
                                       : AstBuildError::malformed_cst;
    }
    if (out.error != AstBuildError::none) {
      if (out.error == AstBuildError::malformed_cst)
        self._ctx.diag("the AST could not be materialised from this node",
                       self._ctx.errorNode);
    } else if (!out.accepted) {
      out.error = AstBuildError::not_accepted;
    }
    // The root's parent is the document's to set: a stale parent from a partial
    // reduction would read as a real container.
    if (out.value != nullptr) out.value->parent = nullptr;
    // Every node made is on the list, and one member at most takes it: where
    // none gave a node up, one node more than the members took is the root
    // alone, and every node reaches it.
    if (out.value != nullptr &&
        (self._ctx.detached || out._nodes.size() != self._ctx.attached + 1u))
      detail::prune_orphans(
          out._nodes, out.references->handles, out.value,
          [&self] { self._stop.poll(); },
          [](AstNode* gone) noexcept { gone->~AstNode(); });
    // Nest wrappers are created out of scan order; without a nest or a
    // re-anchored node the list is sorted already.
    if (actions.anyNest || self._moved) sort_by_record(out._nodes);
    assert(std::is_sorted(out._nodes.begin(), out._nodes.end(), by_record) &&
           "the node list is read by search, so it is sorted");
    throw_if_cancelled(cancelToken);
    return out;
  }

  // A stop or a failure may leave nodes not yet listed: the list owns them, and
  // what it has no room for dies here.
  ~AstReducer() {
    try {
      _ctx.list_made();
    } catch (...) {
      ParseResult::destroy_detached({_ctx.made, _ctx.madeCount});
    }
    ::operator delete(_stack);
  }
  AstReducer(const AstReducer&) = delete;
  AstReducer& operator=(const AstReducer&) = delete;

  bool failed() const { return _ctx.failed(); }

  // A text payload is converted only when the owner closes.
  CURSIV_ALWAYS_INLINE Pending text_write(NodeId id, std::uint16_t field,
                                          std::uint16_t conv, std::uint8_t nar,
                                          ActKind kind) {
    const auto text = detail::payload_node(*_ctx.cst, id);
    return Pending{id, text, detail::kNoIndex, field, conv, kind, nar, nullptr};
  }
  // A node payload comes off the stack now, before whatever consumes the
  // region gets to it.
  CURSIV_ALWAYS_INLINE Pending node_write(NodeId id, std::uint16_t field,
                                          std::uint8_t nar, ActKind kind) {
    const Pending got = take_node_for(field, id);
    return effect(id, got.type, field, kind, got.value, nar);
  }

  // Returns the outermost node, since a nest region re-wraps it. `Nest` says
  // whether any region of the grammar can wrap. Not forced inline: the
  // compiler may keep one copy per class for every record that creates it.
  template <detail::MakeFn Mk, AstApplyFn Ap, bool Nest>
  Made create(std::uint16_t type, NodeId id) {
    const std::size_t first = suffix_begin(id);
    const Made made = consume<Ap, Nest>(Made{Mk(_ctx, id), type}, first, id, id);
    // The factory anchors the new node; only a nest can return another one.
    if constexpr (Nest)
      if (made.value != nullptr) anchor(made.value, id);
    return made;
  }

  // Take the node the body built, never fabricate one: a missing node is a
  // broken tree, and only a recovered region may carry none.
  template <bool Nest>
  Made adopt(std::uint16_t type, NodeId id) {
    const std::size_t first = suffix_begin(id);
    Pending owner = take_node_if(
        first, [this, type](std::uint16_t vt) { return _m.derives(type, vt); });
    // The class a close names is the rule's result, which a nest still
    // pending in the region may be what builds: then the node the body made
    // is taken as it is, and the family is answered once wrapped.
    const bool wrapped = owner.value == nullptr && Nest;
    if (wrapped)
      owner = take_node_if(first, [](std::uint16_t) { return true; });
    if (owner.value == nullptr) [[unlikely]] {
      broken(id);
      return Made{};
    }
    // Keep the adopted node's prefix until the effects have been applied:
    // a later nest may make it a child, whose span ends before that nest.
    const NodeId origin = owner.value->cstNodeId;
    const Made made = consume<nullptr, Nest>(Made{owner.value, owner.type},
                                             first, origin, id);
    if (wrapped && !_m.derives(type, made.type)) [[unlikely]] {
      broken(id);
      return Made{};
    }
    // Only the final result spans the whole construction.
    if (made.value != nullptr) anchor(made.value, id);
    return made;
  }

  // A fused close after a creation or adoption of the same record: a nest may
  // have returned a wrapper, checked as `adopt` does.
  CURSIV_ALWAYS_INLINE void close_made(std::uint16_t type, NodeId id,
                                       const Made& m) {
    assert(m.value != nullptr);
    anchor(m.value, id);
    if (!_m.derives(type, m.type)) broken(id);
  }

  // A flag lands on the node this run has in hand, rather than owed: the
  // region above may open a second node and claim it.
  CURSIV_ALWAYS_INLINE void flag(NodeId id, std::uint16_t field,
                                 const Made& m) {
    const Pending p =
        effect(id, detail::kNoIndex, field, ActKind::flag, nullptr);
    if (!_f.applies[m.type](*this, p, m) && !under_repair(id)) broken(id);
  }

  // A row the record does not apply itself, from a run that opened no node.
  CURSIV_ALWAYS_INLINE void owe_write(NodeId id, std::uint16_t field,
                                      std::uint16_t conv, std::uint8_t nar,
                                      ActKind kind) {
    push_raw(conv != detail::kNoIndex ? text_write(id, field, conv, nar, kind)
                                      : node_write(id, field, nar, kind));
  }
  // The entry checked its creation or adoption before carrying it here.
  CURSIV_ALWAYS_INLINE void owe_write(NodeId id, std::uint16_t field,
                                      ActKind kind, const Made& m) {
    assert(m.value != nullptr);
    push_raw(effect(id, m.type, field, kind, m.value));
  }

  // A nest's pending carries the class it builds: nothing else reads `type` on
  // an effect.
  CURSIV_ALWAYS_INLINE void owe_effect(NodeId id, std::uint16_t type,
                                       std::uint16_t field, ActKind kind) {
    if (kind == ActKind::nest) ++_nests;
    push_raw(effect(id, type, field, kind, nullptr));
  }

  // The plain creation a member above takes off the stack.
  CURSIV_ALWAYS_INLINE void push_made(NodeId id, const Made& m) {
    if (m.value != nullptr)
      push_raw(effect(id, m.type, kNoField, ActKind::create, m.value));
  }

  // `Variant` alone asks which alternative the node's own class fills.
  template <class F, const detail::ConvSlot* Cv, bool Variant>
  CURSIV_ALWAYS_INLINE void write(std::uint16_t field, const Pending& p,
                                  const Made& owner) {
    std::uint16_t alt = 0;
    if constexpr (Variant)
      if (p.value != nullptr) alt = _m.field_alt_of(_m.fields[field], p.type);
    // A list's first element: the region says how many it will hold, so a list
    // allocates once.
    if constexpr (F::storage == detail::FieldStorage::vector) {
      auto& member =
          detail::ast_cast_unchecked<typename F::owner>(owner.value)->*F::ptr;
      if (p.kind != ActKind::assign && member.capacity() == 0)
        member.reserve(elements_from(p, field));
    }
    write_into<F>(_ctx, p, owner.value, Cv, field, p.kind, alt);
  }

  // The write a record applies to the node it made itself. `Checked` where the
  // node's class is only known at runtime, an adoption.
  template <class F, const detail::ConvSlot* Cv, bool Variant, bool Checked>
  CURSIV_ALWAYS_INLINE void fill(std::uint16_t field, const Pending& p,
                                 const Made& owner) {
    if constexpr (Checked) {
      if (owner.value == nullptr ||
          !owner.value->type->derivesFrom(&ast_type_tag<typename F::owner>))
          [[unlikely]] {
        // Under a recovered record the owner may be the reduction the repair
        // abandoned: the effect is dropped, the diagnostic already stands.
        if (!under_repair(p.root)) broken(p.root);
        return;
      }
    }
    write<F, Cv, Variant>(field, p, owner);
  }

 private:
  // Every creation but a nest appends at the largest id so far: the few
  // entries out of order are set aside and merged back.
  static bool by_record(const AstNode* a, const AstNode* b) {
    return a->cstNodeId < b->cstNodeId;
  }
  static void sort_by_record(std::pmr::vector<AstNode*>& nodes) {
    // The room first: a throw half way would lose nodes and repeat others, and
    // what is reserved past the need is never written.
    std::vector<AstNode*> stray;
    stray.reserve(nodes.size());
    std::size_t kept = 0;
    for (AstNode* const entry : nodes) {
      if (kept != 0 && entry->cstNodeId < nodes[kept - 1]->cstNodeId)
        stray.push_back(entry);
      else
        nodes[kept++] = entry;
    }
    if (stray.empty()) return;
    nodes.resize(kept);
    std::sort(stray.begin(), stray.end(), by_record);
    nodes.insert(nodes.end(), stray.begin(), stray.end());
    std::inplace_merge(nodes.begin(), nodes.begin() + kept, nodes.end(),
                       by_record);
  }

  // The stack starts as raw storage: a vector's `resize` would
  // value-initialise it.
  AstReducer(const AstActions& actions, ParseResult& out, StampLayout stamp,
             const CancellationToken& cancelToken)
      : _stack(static_cast<Pending*>(
            ::operator new(kPendingStart * sizeof(Pending)))),
        _cap(kPendingStart),
        _f(actions),
        _m(*actions.meta),
        _ctx{&out.cst(),
             &out._arena,
             &out.references->handles,
             out.references.get(),
             &out.diagnostics,
             &out._nodes,
             &actions,
             AstBuildError::none,
             kNoNode,
             out.cst().recovered(),
             false,
             stamp},
        _stop(cancelToken) {}

  // The writes of `field` from `p` up to the next nest, itself included: past
  // it the region belongs to another node, and each node reads its own stretch
  // once. Too few only grows the list. A pending a nest made stands alone, off
  // the stack.
  std::size_t elements_from(const Pending& p, std::uint16_t field) const {
    const Pending* const end = _stack + _depth;
    if (&p < _stack || &p >= end) return 1;
    std::size_t count = 0;
    for (const Pending* at = &p; at != end && at->kind != ActKind::nest; ++at)
      if (at->field == field) ++count;
    return count;
  }

  // One pending nest, keyed by where the region it wraps begins.
  struct WrapAt {
    NodeId begin = kNoNode;
    NodeId repair = kNoNode;
    std::size_t at = 0;
  };
  struct WrapAbove {
    NodeId root;
    NodeId above = kNoNode;
  };

  static constexpr std::size_t kPendingStart = 4096 / sizeof(Pending);

  // Held as (buffer, depth) and not as a vector, whose `size()` divides by the
  // pending width: the depth is read several times per record.
  Pending* _stack;
  std::size_t _depth = 0;
  std::size_t _cap;
  const AstActions& _f;
  const AstMetaData& _m;
  AstBuildCtx _ctx;
  // A node was re-anchored: the list is out of record order.
  bool _moved = false;
  // The nearest recovered ancestor, excluding the record itself.
  mutable std::vector<NodeId> _repairedBy;
  // Repaired roots in postorder, with a minimum-start tree over their runs.
  // It finds the outermost repair below a consumer without walking ancestors.
  mutable std::vector<NodeId> _repairRoots;
  mutable std::vector<NodeId> _repairBegins;
  // Scratch for `consume`: nothing `consume` calls re-enters the reducer, so
  // one use is live at a time.
  std::vector<WrapAt> _wraps;
  std::vector<WrapAbove> _wrapAncestors;
  // Nest pendings on the stack: a region with none above it collects nothing.
  std::size_t _nests = 0;
  // Polled by the cold walks only, never by the record scan.
  CancellationPoll _stop;

  CURSIV_ALWAYS_INLINE void push_raw(const Pending& p) {
    if (_depth == _cap) [[unlikely]]
      grow_stack();
    _stack[_depth++] = p;
  }

  // Doubling, not sizing to the tape, whose page faults cost more.
  CURSIV_COLD CURSIV_NOINLINE void grow_stack() {
    const std::size_t cap = _cap * 2;
    Pending* const q =
        static_cast<Pending*>(::operator new(cap * sizeof(Pending)));
    std::copy_n(_stack, _depth, q);
    ::operator delete(_stack);
    _stack = q;
    _cap = cap;
  }

  // A reading whose repairs left several pendings hands back the widest: every
  // service reading a document under an edit wants a tree.
  AstNode* result(const Cst& c) const {
    if (_depth == 1) return _stack[0].value;
    AstNode* best = nullptr;
    TextOffset widest = 0;
    for (std::size_t i = 0; i < _depth; ++i) {
      const Pending& p = _stack[i];
      if (p.value == nullptr) continue;
      const TextOffset span = c[p.root].end - c[p.root].begin;
      if (best != nullptr && span <= widest) continue;
      best = p.value;
      widest = span;
    }
    return best;
  }

  // Every tape-shape guard: `CstBuilder` is public, so a caller can build a
  // tape the table never wrote.
  CURSIV_COLD CURSIV_NOINLINE void broken(NodeId id) {
    _ctx.fail(AstBuildError::malformed_cst, id);
  }

  CURSIV_COLD void index_repairs() const {
    const Cst& c = *_ctx.cst;
    if (!_repairedBy.empty()) return;
    const NodeId root = c.entry_root();
    _repairedBy.assign(root + 1u, kNoNode);
    std::vector<NodeId> open;
    for (NodeId r = root + 1u; r-- > 0;) {
      _stop.poll();
      while (!open.empty() && c.subtree_begin(open.back()) > r) {
        _stop.poll();
        open.pop_back();
      }
      if (!open.empty()) _repairedBy[r] = open.back();
      if (c[r].recovered && c.subtree_begin(r) <= r) {
        open.push_back(r);
        _repairRoots.push_back(r);
      }
    }
    std::ranges::reverse(_repairRoots);
    const std::size_t n = _repairRoots.size();
    const std::size_t base = std::bit_ceil(n);
    _repairBegins.assign(base * 2, kNoNode);
    for (std::size_t i = 0; i < n; ++i)
      _repairBegins[base + i] = c.subtree_begin(_repairRoots[i]);
    for (std::size_t i = base; i-- > 1;)
      _repairBegins[i] =
          std::min(_repairBegins[i * 2], _repairBegins[i * 2 + 1]);
  }

  // The last repaired root before `owner` containing `nest`: the first repair
  // a descent from that consumer would reach. Search prefix blocks from right
  // to left, then descend only the one containing that ancestor.
  CURSIV_COLD NodeId repair_between(NodeId nest, NodeId owner) const {
    const std::size_t base = _repairBegins.size() / 2;
    std::size_t at = std::lower_bound(_repairRoots.begin(), _repairRoots.end(),
                                    owner) - _repairRoots.begin();
    if (at == 0) return kNoNode;
    at += base;
    while (at > 1) {
      _stop.poll();
      --at;
      while (at > 1 && (at & 1u) != 0) at /= 2;
      if (_repairBegins[at] <= nest) {
        while (at < base) {
          _stop.poll();
          at = at * 2 + 1;
          if (_repairBegins[at] > nest) --at;
        }
        const NodeId root = _repairRoots[at - base];
        return root > nest ? root : kNoNode;
      }
      if (std::has_single_bit(at)) break;
    }
    return kNoNode;
  }

  CURSIV_COLD bool under_repair(NodeId id) const {
    index_repairs();
    const Cst& c = *_ctx.cst;
    return _repairedBy[id] != kNoNode ||
           (c[id].recovered && c.subtree_begin(id) <= id);
  }

  template <StampLayout L>
  void run() {
    const Cst& c = *_ctx.cst;
    const NodeId root = c.entry_root();
    NodeId id = c.subtree_begin(root);
    const CstNode* node = &c[id];
    // Held here: a run is opaque, so the table would be reloaded per record.
    const auto* const entries = _f.entries;
    // No entry is `kNoElement`, so one test parts the records that owe an
    // entry from the rest.
    const std::uint32_t entryCount =
        std::min<std::uint32_t>(_f.entryCount, kNoElement);
    // The stop is read between runs of records: the inner bound replaces the
    // root, so a record pays no more than it did.
    for (; id <= root; _stop.check()) {
      const NodeId last =
          root - id < kStopRecords ? root : id + (kStopRecords - 1);
      for (; id <= last; ++id, ++node) {
        if (node->hidden) continue;
        // A record that owes nothing names no entry, and a hand-built tape
        // may name one past the table.
        const ElementId e = L.row(*node);
        if (e >= entryCount) {
          if (e == kNoElement) continue;
          broken(id);
          return;
        }
        entries[e](*this, id);
        if (_ctx.failed()) [[unlikely]]
          return;
      }
    }
  }
  // About a millisecond of materialisation between two reads of the stop.
  static constexpr NodeId kStopRecords = 64 * 1024;

  static Pending effect(NodeId id, std::uint16_t type, std::uint16_t field,
                        ActKind kind, AstNode* value,
                        std::uint8_t nar = kNoNarrow) {
    return Pending{id, id, type, field, detail::kNoIndex, kind, nar, value};
  }
  void anchor(AstNode* node, NodeId id) {
    if (node->cstNodeId == id) return;
    node->cstNodeId = id;
    _moved = true;
  }
  // The pendings produced inside `root`. Roots are monotone in postorder, so
  // the run to take is a suffix.
  std::size_t suffix_begin(NodeId root) const {
    const NodeId begin = _ctx.cst->subtree_begin(root);
    const Pending* const stack = _stack;
    std::size_t first = _depth;
    while (first != 0 && stack[first - 1].root >= begin) --first;
    assert(std::all_of(stack + first, stack + _depth,
                       [begin](const Pending& p) { return p.root >= begin; }) &&
           "roots are monotone in postorder, so the walk stops at the region");
    return first;
  }

  // The first creation at or after `first` that `accept` takes. Taken and not
  // erased: `consume` relies on the order, and every reader tests `value`.
  template <class Accept>
  Pending take_node_if(std::size_t first, Accept accept) {
    Pending* const stack = _stack;
    const std::size_t last = _depth;
    for (std::size_t i = first; i < last; ++i) {
      Pending& p = stack[i];
      if (p.value == nullptr || p.kind != ActKind::create || !accept(p.type))
        continue;
      const Pending taken = p;
      p.value = nullptr;
      return taken;
    }
    return Pending{};
  }

  // The node a member takes: any of the families it names, one bit each.
  Pending take_node_for(std::uint16_t field, NodeId root) {
    const std::uint64_t* const bits =
        _f.takeBits + std::size_t{field} * _m.typeWords;
    return take_node_if(suffix_begin(root), [bits](std::uint16_t vt) {
      return ((bits[vt >> 6] >> (vt & 63u)) & 1u) != 0;
    });
  }

  bool reaches_receiver(NodeId root) const {
    const Cst& c = *_ctx.cst;
    return _f.turnsByState ? (_ctx.stamp.state(c[root]) & 1u) != 0
                           : _f.turn(_ctx.stamp.row(c[root]));
  }

  // Where a turn's effects begin: past its first child, the receiver, when
  // its checkpoint says it reached back to one.
  NodeId turn_begin(NodeId root) {
    const Cst& c = *_ctx.cst;
    if (c[root].subtreeSize == 0 || c[root].subtreeSize > root + 1u) {
      broken(root);
      return kNoNode;
    }
    const NodeId begin = c.subtree_begin(root);
    const bool turned = reaches_receiver(root);
    if (!turned) return begin;
    NodeId first = root - 1;
    for (NodeId x = root; x > begin;) {
      const NodeCount size = c[x - 1].subtreeSize;
      if (size == 0 || size > x - begin) {
        broken(root);
        return kNoNode;
      }
      first = x - 1;
      x -= size;
    }
    return first + 1;
  }

  // A nest wraps where its turn begins, its record closing after the effects
  // it wraps. The list is sorted by that start, an order a nest written
  // inside another's turn does not arrive in.
  void collect_wraps(std::size_t first, NodeId owner) {
    _wraps.clear();
    const Pending* const stack = _stack;
    const std::size_t last = _depth;
    for (std::size_t i = first; i < last; ++i) {
      if (stack[i].kind != ActKind::nest) continue;
      const NodeId begin = turn_begin(stack[i].root);
      if (_ctx.failed()) return;
      _wraps.push_back(WrapAt{.begin = begin, .at = i});
    }
    if (_ctx.repaired) index_wrap_repairs(owner);
    // Equal starts: the enclosing (later) pending first.
    const auto before = [](const WrapAt& a, const WrapAt& b) {
      return a.begin != b.begin ? a.begin < b.begin : a.at > b.at;
    };
    if (!std::is_sorted(_wraps.begin(), _wraps.end(), before))
      std::sort(_wraps.begin(), _wraps.end(), before);
  }

  // Only a pending nest outside the repair can change the receiver before a
  // descent reaches that repair. Merge the repaired roots with the nests in
  // reverse postorder to find their closest enclosing pending nest once.
  CURSIV_COLD void index_wrap_repairs(NodeId owner) {
    index_repairs();
    _wrapAncestors.clear();
    for (WrapAt& w : _wraps) {
      w.repair = repair_between(_stack[w.at].root, owner);
      if (w.repair != kNoNode) _wrapAncestors.push_back({w.repair});
    }
    std::ranges::sort(_wrapAncestors, {}, &WrapAbove::root);
    const auto repeats =
        std::ranges::unique(_wrapAncestors, {}, &WrapAbove::root);
    _wrapAncestors.erase(repeats.begin(), repeats.end());
    const Cst& c = *_ctx.cst;
    std::vector<NodeId> open;
    std::size_t w = _wraps.size();
    for (std::size_t i = _wrapAncestors.size(); i-- > 0;) {
      _stop.poll();
      WrapAbove& query = _wrapAncestors[i];
      while (w != 0 && _stack[_wraps[w - 1].at].root > query.root) {
        _stop.poll();
        const NodeId root = _stack[_wraps[--w].at].root;
        while (!open.empty() && c.subtree_begin(open.back()) > root) {
          _stop.poll();
          open.pop_back();
        }
        if (open.empty() || open.back() != root) open.push_back(root);
      }
      while (!open.empty() && c.subtree_begin(open.back()) > query.root) {
        _stop.poll();
        open.pop_back();
      }
      if (!open.empty()) query.above = open.back();
    }
  }

  // Whether a repair gave up on the rule a nest belongs to: a recovered record
  // stands between the nest and the consumer, and the node in hand -- the
  // owner, or the wrapper of a nest holding this one -- was not built before
  // it. The repair and its pending nest ancestor were indexed before sorting
  // the wraps into application order.
  CURSIV_COLD bool stranded(const WrapAt& wrap, NodeId origin,
                           NodeId owner) const {
    if (wrap.repair == kNoNode) return false;
    if (origin >= _ctx.cst->subtree_begin(wrap.repair)) return true;
    return std::ranges::lower_bound(_wrapAncestors, wrap.repair, {},
                                    &WrapAbove::root)
               ->above < owner;
  }

  // Applies and drops every effect of the region, in source order. `Ap` is
  // the owner's class's writer, called direct until a wrap hands the region
  // to another class. Nothing an effect reaches pushes a pending. `origin` is
  // where the owner was built, `id` the record consuming the region.
  template <AstApplyFn Ap, bool Nest>
  CURSIV_ALWAYS_INLINE Made consume(Made owner, std::size_t first,
                                    [[maybe_unused]] NodeId origin,
                                    [[maybe_unused]] NodeId id) {
    Made current = owner;
    [[maybe_unused]] const bool wraps = Nest && _nests != 0;
    if (wraps) {
      collect_wraps(first, id);
      if (_ctx.failed()) return current;
    }
    [[maybe_unused]] std::size_t w = 0;
    const Pending* const stack = _stack;
    const std::size_t last = _depth;
    for (std::size_t i = first; i < last; ++i) {
      const Pending& p = stack[i];
      if (wraps) {
        while (w < _wraps.size() && _wraps[w].begin <= p.root) {
          const WrapAt& wrapAt = _wraps[w];
          const Pending& n = stack[wrapAt.at];
          ++w;
          if (_ctx.repaired && stranded(wrapAt, origin, id)) [[unlikely]]
            continue;
          current = wrap(n, current, wrapAt.begin);
          if (_ctx.failed()) break;
        }
        if (_ctx.failed()) break;
      }
      if (p.kind == ActKind::assign || p.kind == ActKind::append ||
          p.kind == ActKind::flag) {
        // Only a wrap moves the region to another class.
        const bool direct =
            Ap != nullptr && (!Nest || current.type == owner.type);
        const bool landed = direct
                                ? Ap(*this, p, current)
                                : _f.applies[current.type](*this, p, current);
        if (!landed && !under_repair(p.root)) broken(p.root);
      }
      if (_ctx.failed()) [[unlikely]] break;
    }
    assert(_stack == stack && _depth == last &&
           "an effect pushed a pending: the reduction stack moved under "
           "consume");
    if (wraps) _nests -= _wraps.size();
    _depth = first;
    return current;
  }

  // `nest<T, &T::f>`: a T is created, the node built so far moves into T::f,
  // and T is what the rest of the region is. Two nests chain outwards.
  Made wrap(const Pending& p, const Made& current, NodeId begin) {
    assert(p.type != detail::kNoIndex && _f.makes[p.type] != nullptr &&
           p.field != kNoField &&
           "a nest row names a buildable type and a node member of it");
    const detail::FieldDesc& f = _m.fields[p.field];
    assert(f.target != detail::kNoIndex &&
           "a nest's member is of node nature, so it names a family");
    // The one question left is a fact of the tape: what the body built so far
    // must fit the wrapper's member, and after a first nest that node is the
    // previous wrapper. Asked of the node's own type, never of the element
    // that reduced it: a nest hands back a wrapper of another class.
    if (current.value == nullptr || !_m.derives(f.target, current.type))
        [[unlikely]] {
      // Forgiven under a repair, as a write is: the node the member was owed
      // is one the repair dropped.
      if (!under_repair(p.root)) broken(p.root);
      return current;
    }
    // The checkpoint closes before the nest's payload. Its receiver is the
    // complete prefix of the node becoming a child, including writes since
    // that node's previous close. No CST records are changed here.
    if (reaches_receiver(p.root)) {
      // collect_wraps already computed the beginning on this immutable CST.
      if (begin != 0 && begin != kNoNode) anchor(current.value, begin - 1);
    } else {
      // A creation before any text still supplies a current node. Its
      // prefix is the empty checkpoint itself.
      anchor(current.value, p.root);
    }
    const Made wrapper{_f.makes[p.type](_ctx, p.root), p.type};
    const Pending in =
        effect(p.root, current.type, p.field, ActKind::assign, current.value);
    if (!_f.applies[wrapper.type](*this, in, wrapper)) [[unlikely]]
      broken(p.root);
    return wrapper;
  }
};

}  // namespace cursiv
