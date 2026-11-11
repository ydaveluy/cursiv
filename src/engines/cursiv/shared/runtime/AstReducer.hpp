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

#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstBuildCtx.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/Pending.hpp"
#include "cursiv/shared/runtime/ast_write.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
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

namespace cursiv::engine {

// Where a directive's value is: its record has one visible child, the
// payload's own record. A childless record is its own payload, and so is one
// whose visible children are a collapsed data rule's parts.
inline NodeId payload_node(const Cst& c, NodeId id, const CstNode& n) {
  assert(&c[id] == &n && n.subtreeSize != 0 && n.subtreeSize <= id + 1u);
  if (n.subtreeSize == 1) return id;
  NodeId only = kNoNode;
  for (const NodeId child : c.reverse_children(id)) {
    if (c[child].hidden) continue;
    if (only != kNoNode) return id;
    only = child;
  }
  return only != kNoNode ? only : id;
}

// Nodes the root does not reach leave the list; their references stay as
// tombstones so the other ranks stay put. A chain longer than the list is a
// cycle.
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
  // Checked before any node is freed: the walk above reads their parents.
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

class AstReducer {
 public:
  // The tree moves once, into the heap cell the result owns, where every view
  // the AST records points. The entry root's extent is checked; the records
  // under it are trusted to be well formed, as an engine writes them.
  template <StampLayout L>
  static ParseResult build(const AstActions& actions, Cst&& tree,
                           const CancellationToken& cancelToken = kNoCancel) {
    throw_if_cancelled(cancelToken);
    ParseResult out(std::move(tree));
    out.references = std::make_unique<DefaultDocumentReferences>(*actions.meta);
    out.accepted = out.cst().accepted();
    const auto repairs = out.cst().diagnostics();
    out.diagnostics.assign(repairs.begin(), repairs.end());

    // Only a denied entry is fatal: a document rejected after its entry rule
    // matched still has an AST, which completion needs.
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
    // the room past the need is never written.
    out._nodes.reserve(cst.size());
    if (actions.meta->referenceBound != 0)
      out.references->handles.reserve(cst.size() / 2u);

    AstReducer self(actions, out, L, cancelToken);
    NodeId errorNode = kNoNode;
    try {
      self.run<L>();
    } catch (const BrokenTape& tape) {
      out.error = AstBuildError::malformed_cst;
      errorNode = tape.id;
    }
    self._ctx.list_made();
    if (out.error == AstBuildError::none) {
      out.value = self.result(*self._ctx.cst);
      // On a repaired tape, the repairs dropped every node, and their
      // diagnostics say why.
      if (out.value == nullptr)
        out.error = self._ctx.repaired ? AstBuildError::no_entry
                                       : AstBuildError::malformed_cst;
    }
    if (out.error != AstBuildError::none) {
      if (out.error == AstBuildError::malformed_cst)
        self._ctx.diag("the AST could not be materialised from this node",
                       errorNode);
    } else if (!out.accepted) {
      out.error = AstBuildError::not_accepted;
    }
    // The root's parent is the document's to set: a stale parent from a partial
    // reduction would read as a real container.
    if (out.value != nullptr) out.value->parent = nullptr;
    // Every node created is on the list and at most one member takes it: if
    // none was detached and the list holds one node more than the members took,
    // that node is the root and every node reaches it.
    if (out.value != nullptr &&
        (self._ctx.detached || out._nodes.size() != self._ctx.attached + 1u))
      prune_orphans(
          out._nodes, out.references->handles, out.value,
          [&self] { self._ctx.stop.poll(); },
          [](AstNode* gone) noexcept { gone->~AstNode(); });
    // Nest wrappers are created out of scan order; without a nest or a
    // re-anchored node the list is sorted already.
    if (actions.anyNest || self._moved) sort_by_record(out._nodes);
    assert(std::is_sorted(out._nodes.begin(), out._nodes.end(), by_record) &&
           "the node list is read by search, so it is sorted");
    throw_if_cancelled(cancelToken);
    return out;
  }

  // A stop or a failure may leave nodes not yet listed: they go to the list,
  // and those it cannot take are destroyed here.
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

  // A text payload is converted only when the owner closes.
  CURSIV_ALWAYS_INLINE Pending text_write(NodeId id, const CstNode& n,
                                          std::uint16_t field,
                                          std::uint16_t conv, std::uint8_t nar,
                                          ActKind kind) {
    const auto text = payload_node(*_ctx.cst, id, n);
    return Pending{id, text, kNoIndex, field, conv, kind, nar, nullptr};
  }
  // A node payload comes off the stack now, before whatever consumes the
  // region gets to it.
  CURSIV_ALWAYS_INLINE Pending node_write(NodeId id, const CstNode& n,
                                          std::uint16_t field, std::uint8_t nar,
                                          ActKind kind) {
    const Made got = take_node_for(field, id, n);
    return effect(id, got.type, field, kind, got.value, nar);
  }

  // Leaves the outermost node, a nest region re-wrapping it, to the region
  // above as the write of `field`, and returns it. `Nest`: a region of the
  // grammar can wrap. Not forced inline: one copy per class serves its records.
  template <MakeFn Mk, AstApplyFn Ap, bool Nest>
  Made create(NodeId id, const CstNode& n, std::uint16_t type,
              std::uint16_t field, ActKind kind) {
    Pending* const first = suffix_begin(id, n);
    const Made made = consume<Ap, Nest>(Made{Mk(_ctx, id), type}, first, id, id);
    // The factory anchors the new node; only a nest can return another one.
    if constexpr (Nest) anchor(made.value, id);
    push_raw(effect(id, made.type, field, kind, made.value));
    return made;
  }

  // Takes the node the body built and never creates one: a missing node is a
  // broken tape. The node is left to the region above as `create` leaves its
  // own.
  template <bool Nest>
  Made adopt(NodeId id, const CstNode& n, std::uint16_t type,
             std::uint16_t field, ActKind kind) {
    Pending* const first = suffix_begin(id, n);
    Made owner = take_node_if(
        first, [this, type](std::uint16_t vt) { return _m.derives(type, vt); });
    // The class a close names is the rule's result, which a nest still pending
    // in the region may build: then the body's node is taken as it is, and its
    // class is checked once wrapped.
    const bool wrapped = owner.value == nullptr && Nest;
    if (wrapped)
      owner = take_node_if(first, [](std::uint16_t) { return true; });
    if (owner.value == nullptr) [[unlikely]]
      broken_tape(id);
    // Keep the adopted node's prefix until the effects have been applied:
    // a later nest may make it a child, whose span ends before that nest.
    const NodeId origin = owner.value->cstNodeId;
    const Made made = consume<nullptr, Nest>(Made{owner.value, owner.type},
                                             first, origin, id);
    if (wrapped && !_m.derives(type, made.type)) [[unlikely]]
      broken_tape(id);
    // Only the final result spans the whole construction.
    anchor(made.value, id);
    push_raw(effect(id, made.type, field, kind, made.value));
    return made;
  }

  // A fused close after a creation or adoption of the same record: a nest may
  // have returned a wrapper, checked as `adopt` does.
  CURSIV_ALWAYS_INLINE void close_made(std::uint16_t type, NodeId id,
                                       const Made& m) {
    assert(m.value != nullptr);
    anchor(m.value, id);
    if (!_m.derives(type, m.type)) broken_tape(id);
  }

  // A flag is written at once to the node this run holds: deferred, it could go
  // to a second node the region above opens.
  CURSIV_ALWAYS_INLINE void flag(NodeId id, std::uint16_t field,
                                 const Made& m) {
    const Pending p = effect(id, kNoIndex, field, ActKind::flag, nullptr);
    if (!_f.applies[m.type](*this, p, m) && !under_repair(id)) broken_tape(id);
  }

  // A write the record defers, from a run that opened no node.
  CURSIV_ALWAYS_INLINE void defer_write(NodeId id, const CstNode& n,
                                        std::uint16_t field, std::uint16_t conv,
                                        std::uint8_t nar, ActKind kind) {
    push_raw(conv != kNoIndex ? text_write(id, n, field, conv, nar, kind)
                              : node_write(id, n, field, nar, kind));
  }
  // One more write of the node a creation or an adoption returned.
  CURSIV_ALWAYS_INLINE void defer_write(NodeId id, std::uint16_t field,
                                        ActKind kind, const Made& m) {
    push_raw(effect(id, m.type, field, kind, m.value));
  }

  // A nest's pending carries the class it builds: nothing else reads `type` on
  // an effect.
  CURSIV_ALWAYS_INLINE void defer_effect(NodeId id, std::uint16_t type,
                                         std::uint16_t field, ActKind kind) {
    if (kind == ActKind::nest) ++_nests;
    push_raw(effect(id, type, field, kind, nullptr));
  }

  // A variant member alone asks which alternative the node's own class fills.
  template <class F, const ConvSlot* Cv>
  CURSIV_ALWAYS_INLINE void write(std::uint16_t field, const Pending& p,
                                  const Made& owner) {
    std::uint16_t alt = 0;
    if constexpr (F::content == FieldContent::variant)
      if (p.value != nullptr) alt = _m.field_alt_of(_m.fields[field], p.type);
    // A list's first element: the region says how many it will hold, so a list
    // allocates once.
    if constexpr (F::storage == FieldStorage::vector) {
      auto& member =
          ast_cast_unchecked<typename F::owner>(owner.value)->*F::ptr;
      if (p.kind != ActKind::assign && member.capacity() == 0)
        member.reserve(elements_from(p, field));
    }
    write_into<F>(_ctx, p, owner.value, Cv, field, alt);
  }

  // The write a record applies to the node it made itself. `Checked` where the
  // node's class is only known at runtime, an adoption.
  template <class F, const ConvSlot* Cv, bool Checked>
  CURSIV_ALWAYS_INLINE void fill(std::uint16_t field, const Pending& p,
                                 const Made& owner) {
    if constexpr (Checked) {
      if (owner.value == nullptr ||
          !owner.value->type->derivesFrom(&ast_type_tag<typename F::owner>))
          [[unlikely]] {
        // Under a recovered record the owner may be the reduction the repair
        // abandoned: the effect is dropped, the diagnostic already stands.
        if (!under_repair(p.root)) broken_tape(p.root);
        return;
      }
    }
    write<F, Cv>(field, p, owner);
  }

 private:
  // Every creation but a nest appends at the largest id so far: the few
  // entries out of order are set aside and merged back.
  static bool by_record(const AstNode* a, const AstNode* b) {
    return a->cstNodeId < b->cstNodeId;
  }
  static void sort_by_record(std::pmr::vector<AstNode*>& nodes) {
    // The sorted prefix stays in place, and a sorted list allocates nothing.
    // Reserve first: a throw half way would lose nodes and duplicate others.
    const auto sorted =
        std::is_sorted_until(nodes.begin(), nodes.end(), by_record);
    if (sorted == nodes.end()) return;
    std::vector<AstNode*> unsorted;
    unsorted.reserve(static_cast<std::size_t>(nodes.end() - sorted));
    auto kept = static_cast<std::size_t>(sorted - nodes.begin());
    for (auto at = sorted; at != nodes.end(); ++at) {
      if ((*at)->cstNodeId < nodes[kept - 1]->cstNodeId)
        unsorted.push_back(*at);
      else
        nodes[kept++] = *at;
    }
    nodes.resize(kept);
    std::sort(unsorted.begin(), unsorted.end(), by_record);
    nodes.insert(nodes.end(), unsorted.begin(), unsorted.end());
    std::inplace_merge(nodes.begin(), nodes.begin() + kept, nodes.end(),
                       by_record);
  }

  // The stack starts as raw storage: a vector's `resize` would
  // value-initialise it.
  AstReducer(const AstActions& actions, ParseResult& out, StampLayout stamp,
             const CancellationToken& cancelToken)
      : _stack(static_cast<Pending*>(
            ::operator new(kPendingStart * sizeof(Pending)))),
        _top(_stack),
        _end(_stack + kPendingStart),
        _f(actions),
        _m(*actions.meta),
        _ctx{&out.cst(),
             &out._arena,
             &out.references->handles,
             out.references.get(),
             &out.diagnostics,
             &out._nodes,
             &actions,
             out.cst().recovered(),
             false,
             stamp,
             CancellationPoll(cancelToken)} {}

  // The writes of `field` from `p` up to the next nest, itself included: past
  // it the region belongs to another node. Too few only grows the list.
  std::size_t elements_from(const Pending& p, std::uint16_t field) const {
    const Pending* const end = _top;
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
    const Pending* at = nullptr;
  };
  struct WrapAbove {
    NodeId root;
    NodeId above = kNoNode;
  };

  static constexpr std::size_t kPendingStart = 4096 / sizeof(Pending);

  // Held as (buffer, top, end): a push reads the top and the end, and no
  // size is computed.
  Pending* _stack;
  Pending* _top;
  Pending* _end;
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

  CURSIV_ALWAYS_INLINE void push_raw(const Pending& p) {
    if (_top == _end) [[unlikely]]
      grow_stack();
    *_top++ = p;
  }

  // Doubling, not sizing to the tape, whose page faults cost more.
  CURSIV_COLD CURSIV_NOINLINE void grow_stack() {
    const auto depth = static_cast<std::size_t>(_top - _stack);
    Pending* const q =
        static_cast<Pending*>(::operator new(2 * depth * sizeof(Pending)));
    std::copy_n(_stack, depth, q);
    ::operator delete(_stack);
    _stack = q;
    _top = q + depth;
    _end = q + 2 * depth;
  }

  // When repairs leave several pendings, returns the widest: every service
  // reading a document under edit wants a tree.
  AstNode* result(const Cst& c) const {
    if (_top == _stack + 1) return _stack->value;
    AstNode* best = nullptr;
    TextOffset widest = 0;
    for (const Pending* at = _stack; at != _top; ++at) {
      const Pending& p = *at;
      if (p.value == nullptr) continue;
      const TextOffset span = c[p.root].end - c[p.root].begin;
      if (best != nullptr && span <= widest) continue;
      best = p.value;
      widest = span;
    }
    return best;
  }

  CURSIV_COLD void index_repairs() const {
    const Cst& c = *_ctx.cst;
    if (!_repairedBy.empty()) return;
    const NodeId root = c.entry_root();
    _repairedBy.assign(root + 1u, kNoNode);
    std::vector<NodeId> open;
    for (NodeId r = root + 1u; r-- > 0;) {
      _ctx.stop.poll();
      while (!open.empty() && c.subtree_begin(open.back()) > r) {
        _ctx.stop.poll();
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
      _ctx.stop.poll();
      --at;
      while (at > 1 && (at & 1u) != 0) at /= 2;
      if (_repairBegins[at] <= nest) {
        while (at < base) {
          _ctx.stop.poll();
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

  // Out of line: the loop over the records is compiled on its own, whatever
  // `build` holds around it.
  template <StampLayout L>
  CURSIV_NOINLINE void run() {
    const Cst& c = *_ctx.cst;
    const NodeId root = c.entry_root();
    NodeId id = c.subtree_begin(root);
    const CstNode* node = &c[id];
    // Held here: a run is opaque, so the table would be reloaded per record.
    const auto* const entries = _f.entries;
    // No entry is `kNoElement`, so one test separates the records with an entry
    // from the rest.
    const std::uint32_t entryCount =
        std::min<std::uint32_t>(_f.entryCount, kNoElement);
    // The stop is read between runs of records: the inner bound replaces the
    // root, so the loop over records costs nothing more.
    for (; id <= root; _ctx.stop.check()) {
      const NodeId last =
          root - id < kStopRecords ? root : id + (kStopRecords - 1);
      for (; id <= last; ++id, ++node) {
        if (node->hidden) continue;
        // A record with no action names no entry, and a hand-built tape may
        // name one past the table.
        const ElementId e = L.row(*node);
        if (e >= entryCount) {
          if (e == kNoElement) continue;
          broken_tape(id);
        }
        entries[e](*this, id, *node);
      }
    }
  }
  // About a millisecond of materialisation between two reads of the stop.
  static constexpr NodeId kStopRecords = 64 * 1024;

  static Pending effect(NodeId id, std::uint16_t type, std::uint16_t field,
                        ActKind kind, AstNode* value,
                        std::uint8_t nar = kNoNarrow) {
    return Pending{id, id, type, field, kNoIndex, kind, nar, value};
  }
  void anchor(AstNode* node, NodeId id) {
    if (node->cstNodeId == id) return;
    node->cstNodeId = id;
    _moved = true;
  }
  // The pendings produced inside record `n` at `id`. Roots are monotone in
  // postorder, so the run to take is a suffix.
  Pending* suffix_begin(NodeId id, const CstNode& n) const {
    const NodeId begin = id - (n.subtreeSize - 1);
    Pending* first = _top;
    while (first != _stack && first[-1].root >= begin) --first;
    assert(std::all_of(first, _top,
                       [begin](const Pending& p) { return p.root >= begin; }) &&
           "roots are monotone in postorder, so the walk stops at the region");
    return first;
  }

  // The first creation at or after `first` that `accept` takes. Taken and not
  // erased: `consume` relies on the order, and every reader tests `value`.
  template <class Accept>
  Made take_node_if(Pending* first, Accept accept) {
    Pending* const last = _top;
    for (Pending* at = first; at != last; ++at) {
      Pending& p = *at;
      if (p.value == nullptr || p.kind != ActKind::create || !accept(p.type))
        continue;
      const Made taken{p.value, p.type};
      p.value = nullptr;
      return taken;
    }
    return Made{};
  }

  // The node a member takes: any of the families it names, one bit each.
  Made take_node_for(std::uint16_t field, NodeId id, const CstNode& n) {
    const std::uint64_t* const bits =
        _f.takeBits + std::size_t{field} * _m.typeWords;
    return take_node_if(suffix_begin(id, n), [bits](std::uint16_t vt) {
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
    if (c[root].subtreeSize == 0 || c[root].subtreeSize > root + 1u)
      broken_tape(root);
    const NodeId begin = c.subtree_begin(root);
    const bool turned = reaches_receiver(root);
    if (!turned) return begin;
    NodeId first = root - 1;
    for (NodeId x = root; x > begin;) {
      const NodeCount size = c[x - 1].subtreeSize;
      if (size == 0 || size > x - begin) broken_tape(root);
      first = x - 1;
      x -= size;
    }
    return first + 1;
  }

  // A nest wraps where its turn begins, its record closing after the effects
  // it wraps. The list is sorted by that start, an order a nest written
  // inside another's turn does not arrive in.
  void collect_wraps(const Pending* first, NodeId owner) {
    _wraps.clear();
    for (const Pending* p = first; p != _top; ++p)
      if (p->kind == ActKind::nest)
        _wraps.push_back(WrapAt{.begin = turn_begin(p->root), .at = p});
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
      w.repair = repair_between(w.at->root, owner);
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
      _ctx.stop.poll();
      WrapAbove& query = _wrapAncestors[i];
      while (w != 0 && _wraps[w - 1].at->root > query.root) {
        _ctx.stop.poll();
        const NodeId root = _wraps[--w].at->root;
        while (!open.empty() && c.subtree_begin(open.back()) > root) {
          _ctx.stop.poll();
          open.pop_back();
        }
        if (open.empty() || open.back() != root) open.push_back(root);
      }
      while (!open.empty() && c.subtree_begin(open.back()) > query.root) {
        _ctx.stop.poll();
        open.pop_back();
      }
      if (!open.empty()) query.above = open.back();
    }
  }

  // Whether a repair gave up on the rule a nest belongs to: a recovered record
  // stands between the nest and the consumer, before the node in hand.
  CURSIV_COLD bool stranded(const WrapAt& wrap, NodeId origin,
                           NodeId owner) const {
    if (wrap.repair == kNoNode) return false;
    if (origin >= _ctx.cst->subtree_begin(wrap.repair)) return true;
    return std::ranges::lower_bound(_wrapAncestors, wrap.repair, {},
                                    &WrapAbove::root)
               ->above < owner;
  }

  // Applies and drops every effect of the region, in source order, with `Ap`
  // until a wrap hands it to another class. `origin` is where the owner was
  // built, `id` the record consuming the region.
  template <AstApplyFn Ap, bool Nest>
  CURSIV_ALWAYS_INLINE Made consume(Made owner, Pending* first,
                                    [[maybe_unused]] NodeId origin,
                                    [[maybe_unused]] NodeId id) {
    Made current = owner;
    [[maybe_unused]] const bool wraps = Nest && _nests != 0;
    if (wraps) collect_wraps(first, id);
    [[maybe_unused]] std::size_t w = 0;
    const Pending* const last = _top;
    for (const Pending* at = first; at != last; ++at) {
      const Pending& p = *at;
      if (wraps) {
        while (w < _wraps.size() && _wraps[w].begin <= p.root) {
          const WrapAt& wrapAt = _wraps[w];
          const Pending& n = *wrapAt.at;
          ++w;
          if (_ctx.repaired && stranded(wrapAt, origin, id)) [[unlikely]]
            continue;
          current = wrap(n, current, wrapAt.begin);
        }
      }
      if (p.kind == ActKind::assign || p.kind == ActKind::append ||
          p.kind == ActKind::flag) {
        // Only a wrap moves the region to another class.
        const bool direct =
            Ap != nullptr && (!Nest || current.type == owner.type);
        const bool applied = direct
                                 ? Ap(*this, p, current)
                                 : _f.applies[current.type](*this, p, current);
        if (!applied && !under_repair(p.root)) broken_tape(p.root);
      }
    }
    assert(_top == last &&
           "an effect pushed a pending: the reduction stack moved under "
           "consume");
    if (wraps) _nests -= _wraps.size();
    _top = first;
    return current;
  }

  // `nest<T, &T::f>`: a T is created, the node built so far moves into T::f,
  // and T is what the rest of the region is. Two nests chain outwards.
  Made wrap(const Pending& p, const Made& current, NodeId begin) {
    assert(p.type != kNoIndex && _f.makes[p.type] != nullptr &&
           p.field != kNoField &&
           "a nest row names a buildable type and a node member of it");
    const FieldDesc& f = _m.fields[p.field];
    assert(f.target != kNoIndex &&
           "a nest's member holds a node, so it names a family");
    // What the body built so far must fit the wrapper's member: asked of the
    // node's own type, as a nest hands back a wrapper of another class.
    if (current.value == nullptr || !_m.derives(f.target, current.type))
        [[unlikely]] {
      // Allowed under a repair, as for a write: the repair dropped the node the
      // member expected.
      if (!under_repair(p.root)) broken_tape(p.root);
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
      broken_tape(p.root);
    return wrapper;
  }
};

// The default layout's build is compiled once, in the shared runtime.
extern template ParseResult AstReducer::build<StampLayout{}>(
    const AstActions&, Cst&&, const CancellationToken&);

}  // namespace cursiv::engine
