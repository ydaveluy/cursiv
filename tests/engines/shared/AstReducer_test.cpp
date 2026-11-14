#include "cursiv/shared/runtime/AstReducer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstConvThunks.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/Check.hpp"

namespace {

void only_retained_owners_keep_active_reference_ranks() {
  std::array<cursiv::AstNode, 11> tree{};
  for (unsigned i = 1; i <= 4; ++i) tree[i].parent = &tree[i - 1];
  tree[6].parent = &tree[7];
  tree[7].parent = &tree[6];
  tree[8].parent = &tree[5];
  tree[9].parent = &tree[8];
  tree[10].parent = &tree[9];
  std::pmr::vector<cursiv::AstNode*> nodes;
  std::vector<cursiv::detail::RefHandle> handles;
  for (auto& node : tree) {
    nodes.push_back(&node);
    handles.push_back({.owner = &node});
  }
  handles.push_back({.owner = nullptr});
  cursiv::engine::prune_orphans(
      nodes, handles, &tree[0], [] {}, [](cursiv::AstNode*) {});
  CHECK(nodes.size() == 5u);
  CHECK(handles.size() == 12);
  for (unsigned i = 0; i < handles.size(); ++i)
    CHECK(handles[i].owner == (i < 5 ? &tree[i] : nullptr));
  for (unsigned i = 0; i < nodes.size(); ++i) CHECK(nodes[i] == &tree[i]);
}

constexpr cursiv::engine::StampLayout kStamp{};

struct Child : cursiv::Extends<cursiv::AstNode> {};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Child* child = nullptr;
};
using ChildField = cursiv::detail::field_tag<&Root::child>;
constexpr std::uint64_t derives[] = {1, 2};
constexpr std::uint16_t targets[] = {1};
constexpr cursiv::detail::FieldDesc fields[] = {
    {"child", cursiv::detail::FieldContent::node,
     cursiv::detail::FieldStorage::direct, 1, 0, targets, 1}};
constexpr cursiv::detail::AstMetaData meta{.derivesBits = derives,
                                           .typeCount = 2,
                                           .typeWords = 1,
                                           .fields = fields,
                                           .referenceFields = {},
                                           .fieldCount = 1};
constexpr cursiv::engine::MakeFn makes[] = {&cursiv::engine::make_node<Root>,
                                            &cursiv::engine::make_node<Child>};
// The one member takes the child's family.
constexpr std::uint64_t takeBits[] = {2};

unsigned writes = 0;
bool root_writer(cursiv::engine::AstReducer& b,
                 const cursiv::engine::Pending& p,
                 const cursiv::engine::Made& m) {
  if (p.field != 0) return false;
  ++writes;
  b.write<ChildField, nullptr>(0, p, m);
  return true;
}
bool child_writer(cursiv::engine::AstReducer&, const cursiv::engine::Pending&,
                  const cursiv::engine::Made&) {
  return false;
}
constexpr cursiv::engine::AstApplyFn applies[] = {root_writer, child_writer};

void root(cursiv::engine::AstReducer& b, cursiv::NodeId id,
          const cursiv::CstNode& n) {
  b.create<makes[0], root_writer, false>(id, n, 0, cursiv::detail::kNoField,
                                         cursiv::detail::ActKind::create);
}
void adopt(cursiv::engine::AstReducer& b, cursiv::NodeId id,
           const cursiv::CstNode& n) {
  b.adopt<false>(id, n, 1, 0, cursiv::detail::ActKind::assign);
}
void child(cursiv::engine::AstReducer& b, cursiv::NodeId id,
           const cursiv::CstNode& n) {
  b.create<makes[1], child_writer, false>(id, n, 1, cursiv::detail::kNoField,
                                          cursiv::detail::ActKind::create);
}
void direct(cursiv::engine::AstReducer& b, cursiv::NodeId id,
            const cursiv::CstNode& n) {
  const auto h = b.node_write(id, n, 0, cursiv::kNoNarrow,
                              cursiv::detail::ActKind::assign);
  const auto m = b.create<makes[0], root_writer, false>(
      id, n, 0, cursiv::detail::kNoField, cursiv::detail::ActKind::create);
  ++writes;
  b.fill<ChildField, nullptr, false>(0, h, m);
}
void direct_twice(cursiv::engine::AstReducer& b, cursiv::NodeId id,
                  const cursiv::CstNode& n) {
  const auto first = b.node_write(id, n, 0, cursiv::kNoNarrow,
                                  cursiv::detail::ActKind::assign);
  const auto second = b.node_write(id, n, 0, cursiv::kNoNarrow,
                                   cursiv::detail::ActKind::assign);
  const auto m = b.create<makes[0], root_writer, false>(
      id, n, 0, cursiv::detail::kNoField, cursiv::detail::ActKind::create);
  ++writes;
  b.fill<ChildField, nullptr, false>(0, first, m);
  ++writes;
  b.fill<ChildField, nullptr, false>(0, second, m);
}
constexpr cursiv::engine::AstEntryFn entries[] = {root, adopt, child, direct,
                                                  direct_twice};
const cursiv::engine::AstActions actions{.meta = &meta,
                                         .makes = makes,
                                         .entries = entries,
                                         .entryCount = 5,
                                         .applies = applies,
                                         .takeBits = takeBits};

// The tree closed on `top` and accepted, reduced with the write count reset.
cursiv::ParseResult reduce(cursiv::Cst&& c, cursiv::detail::CstBuilder& b,
                           cursiv::NodeId top,
                           const cursiv::engine::AstActions& with = actions) {
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  return cursiv::engine::AstReducer::build<kStamp>(with, std::move(c));
}

// An adoption takes the node its body built or none: a repair excuses an
// effect that does not apply, never a node that was not built.
void an_adoption_takes_the_node_its_body_built(bool node, bool ownRepair,
                                               bool parentRepair,
                                               bool childRepair,
                                               bool unrelatedRepair,
                                               bool badExtent = false) {
  cursiv::Cst c("xx");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto opened = b.enter();
  if (unrelatedRepair) {
    const auto unrelated = b.leaf(0, 0, kStamp.make(cursiv::kNoElement, 0));
    b.stream().at(unrelated).recovered = true;
  }
  const auto body = b.enter();
  if (node || childRepair) {
    const auto leaf =
        b.leaf(0, 1, kStamp.make(node ? 2 : cursiv::kNoElement, 0));
    b.stream().at(leaf).recovered = childRepair;
  }
  const auto missing = b.exit(body, 0, kStamp.make(1, 0));
  b.stream().at(missing).recovered = ownRepair;
  if (badExtent) b.stream().at(missing).subtreeSize = 0;
  const auto top = b.exit(opened, 0, kStamp.make(0, 0));
  b.stream().at(top).recovered = parentRepair;
  if (ownRepair || parentRepair || childRepair || unrelatedRepair)
    b.note_repair(cursiv::DiagnosticKind::Deleted, cursiv::kNoElement, 0, 0);
  const auto result = reduce(std::move(c), b, top);
  const bool good = node && !badExtent;
  CHECK((result.error == cursiv::AstBuildError::None) == good);
  CHECK((result.value != nullptr) == good);
  if (good) {
    const auto* r = cursiv::ast_cast<Root>(result.value);
    CHECK(r != nullptr && r->child != nullptr && r->child->parent == r);
    CHECK(result.value->parent == nullptr);
    CHECK(result.nodes().size() == 2);
  }
  CHECK(writes == (good ? 1u : 0u));
}

// A node member given no node is a broken tree, repaired or not.
void a_direct_write_without_its_node_is_broken(bool repaired) {
  cursiv::Cst c("");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto top = b.leaf(0, 0, kStamp.make(3, 0));
  b.stream().at(top).recovered = repaired;
  if (repaired)
    b.note_repair(cursiv::DiagnosticKind::Inserted, cursiv::kNoElement, 0, 0);
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::MalformedCst);
  CHECK(result.value == nullptr && writes == 1);
}

void repeated_node_writes_keep_source_order_and_skip_other_families() {
  cursiv::Cst c("abcd");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto opened = b.enter();
  b.leaf(0, 1, kStamp.make(2, 0));
  b.leaf(1, 2, kStamp.make(0, 0));
  const auto second = b.leaf(2, 3, kStamp.make(2, 0));
  b.leaf(3, 4, kStamp.make(2, 0));
  const auto top = b.exit(opened, 0, kStamp.make(4, 0));
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::None);
  const auto* r = result.valueAs<Root>();
  CHECK(r != nullptr && r->child != nullptr);
  if (r == nullptr || r->child == nullptr) return;
  CHECK(r->child->cstNodeId == second);
  CHECK(r->child->parent == r && r->parent == nullptr);
  CHECK(result.nodes().size() == 2);
  CHECK(writes == 2);
}

void a_node_no_member_takes_leaves_the_node_list() {
  cursiv::Cst c("ab");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto opened = b.enter();
  const auto taken = b.leaf(0, 1, kStamp.make(2, 0));
  b.leaf(1, 2, kStamp.make(2, 0));
  const auto top = b.exit(opened, 0, kStamp.make(3, 0));
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::None);
  const auto* r = result.valueAs<Root>();
  CHECK(r != nullptr && r->child != nullptr && r->child->cstNodeId == taken);
  CHECK(result.nodes().size() == 2);
  for (const cursiv::AstNode* node : result.nodes())
    CHECK(node == r || node->parent == r);
  CHECK(writes == 1);
}

namespace nest {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  boolean flag = false;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
};
using FlagField = cursiv::detail::field_tag<&Num::flag>;
using CalleeField = cursiv::detail::field_tag<&Call::callee>;
constexpr std::uint64_t derives[] = {7, 2, 4};
constexpr std::uint16_t targets[] = {0};
constexpr cursiv::detail::FieldDesc fields[] = {
    {"flag", cursiv::detail::FieldContent::value,
     cursiv::detail::FieldStorage::direct, cursiv::detail::kNoIndex, 1, targets,
     0},
    {"callee", cursiv::detail::FieldContent::node,
     cursiv::detail::FieldStorage::direct, 0, 2, targets, 1}};
constexpr cursiv::detail::AstMetaData meta{.derivesBits = derives,
                                           .typeCount = 3,
                                           .typeWords = 1,
                                           .fields = fields,
                                           .referenceFields = {},
                                           .fieldCount = 2};
constexpr cursiv::engine::MakeFn makes[] = {&cursiv::engine::make_node<Expr>,
                                            &cursiv::engine::make_node<Num>,
                                            &cursiv::engine::make_node<Call>};
constexpr std::uint64_t takeBits[] = {0, 7};

bool expr_writer(cursiv::engine::AstReducer&, const cursiv::engine::Pending&,
                 const cursiv::engine::Made&) {
  return false;
}
bool num_writer(cursiv::engine::AstReducer& b, const cursiv::engine::Pending& p,
                const cursiv::engine::Made& m) {
  if (p.field != 0) return false;
  b.write<FlagField, nullptr>(0, p, m);
  return true;
}
bool call_writer(cursiv::engine::AstReducer& b,
                 const cursiv::engine::Pending& p,
                 const cursiv::engine::Made& m) {
  if (p.field != 1) return false;
  b.write<CalleeField, nullptr>(1, p, m);
  return true;
}
constexpr cursiv::engine::AstApplyFn applies[] = {expr_writer, num_writer,
                                                  call_writer};

// The receiver, then its flag set on whatever the region is when it applies.
void number(cursiv::engine::AstReducer& b, cursiv::NodeId id,
            const cursiv::CstNode& n) {
  b.create<makes[1], num_writer, true>(id, n, 1, cursiv::detail::kNoField,
                                       cursiv::detail::ActKind::create);
  b.defer_effect(id, 1, 0, cursiv::detail::ActKind::flag);
}
void turn(cursiv::engine::AstReducer& b, cursiv::NodeId id,
          const cursiv::CstNode&) {
  b.defer_effect(id, 2, 1, cursiv::detail::ActKind::nest);
}
void close(cursiv::engine::AstReducer& b, cursiv::NodeId id,
           const cursiv::CstNode& n) {
  b.adopt<true>(id, n, 0, cursiv::detail::kNoField,
                cursiv::detail::ActKind::create);
}
constexpr cursiv::engine::AstEntryFn entries[] = {number, turn, close};
constexpr std::uint8_t turns[] = {0, 1, 0};
constexpr cursiv::engine::AstActions byState{.meta = &meta,
                                             .makes = makes,
                                             .entries = entries,
                                             .entryCount = 3,
                                             .applies = applies,
                                             .takeBits = takeBits,
                                             .anyNest = true,
                                             .turnsByState = true};
constexpr cursiv::engine::AstActions byRow{.meta = &meta,
                                           .makes = makes,
                                           .entries = entries,
                                           .entryCount = 3,
                                           .applies = applies,
                                           .takeBits = takeBits,
                                           .anyNest = true,
                                           .turns = turns};

// `1(` read as a call turn over its receiver, closed as an expression; a
// repair may have given the turn up, its receiver with it.
std::string reduce_turn(const cursiv::engine::AstActions& actions,
                        bool stateSays, bool abandoned,
                        unsigned badExtent = 0) {
  cursiv::Cst c("1(");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto all = b.enter();
  const auto region = b.enter();
  const auto turned = b.enter();
  b.leaf(0, 1, kStamp.make(0, 0));
  const auto token = b.leaf(1, 2, kStamp.make(cursiv::kNoElement, 0));
  b.exit(turned, 0, kStamp.make(1, stateSays ? 1 : 0));
  const auto gaveUp = b.exit(region, 0, kStamp.make(cursiv::kNoElement, 0));
  b.stream().at(gaveUp).recovered = abandoned;
  const auto top = b.exit(all, 0, kStamp.make(2, 0));
  if (badExtent != 0)
    b.stream().at(token).subtreeSize = badExtent == 1 ? 0 : cursiv::kNoNode;
  if (abandoned)
    b.note_repair(cursiv::DiagnosticKind::Incomplete, cursiv::kNoElement, 0, 2);
  const auto r = reduce(std::move(c), b, top, actions);
  if (r.error != cursiv::AstBuildError::None || r.value == nullptr)
    return "error";
  const auto* call = cursiv::ast_cast<Call>(r.value);
  const auto* num =
      cursiv::ast_cast<Num>(call != nullptr ? call->callee : r.value);
  std::string out = num == nullptr ? "?" : num->flag ? "Num!" : "Num";
  if (call != nullptr) out = "Call(" + out + ")";
  return out + " nodes=" + std::to_string(r.nodes().size());
}

}  // namespace nest

// Both signals of a turn's receiver read the same tree, and a nest whose turn
// a repair gave up with its receiver wraps nothing.
void a_turn_reads_the_same_by_state_or_by_row() {
  using nest::reduce_turn;
  for (const bool abandoned : {false, true}) {
    const std::string want = abandoned ? "Num! nodes=1" : "Call(Num!) nodes=2";
    CHECK(reduce_turn(nest::byState, true, abandoned) == want);
    CHECK(reduce_turn(nest::byRow, false, abandoned) == want);
  }
  // The signal is what orders the receiver's own effects before the wrap.
  CHECK(reduce_turn(nest::byState, false, false) == "error");
  for (unsigned bad : {1u, 2u}) {
    CHECK(reduce_turn(nest::byState, true, false, bad) == "error");
    CHECK(reduce_turn(nest::byRow, false, false, bad) == "error");
  }
}

// `count` calls down to a flagged number, each the parent of its callee.
void a_chain_of(const cursiv::ParseResult& r, unsigned count) {
  CHECK(r.error == cursiv::AstBuildError::None);
  CHECK(r.nodes().size() == count + 1);
  unsigned depth = 0;
  const cursiv::AstNode* current = r.value;
  while (const auto* call = cursiv::ast_cast<nest::Call>(current)) {
    CHECK(call->callee != nullptr && call->callee->parent == call);
    current = call->callee;
    ++depth;
  }
  const auto* num = cursiv::ast_cast<nest::Num>(current);
  CHECK(depth == count && num != nullptr && num->flag);
}

void unrelated_repairs_leave_a_long_nest_chain_intact() {
  constexpr unsigned count = 2048;
  cursiv::Cst c("1(");
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto all = b.enter();
  b.leaf(0, 1, kStamp.make(0, 0));
  for (unsigned i = 0; i < count; ++i) b.leaf(1, 2, kStamp.make(1, 0));
  for (unsigned i = 0; i < count; ++i) {
    const auto leaf = b.leaf(2, 2, kStamp.make(cursiv::kNoElement, 0));
    b.stream().at(leaf).recovered = true;
  }
  const auto top = b.exit(all, 0, kStamp.make(2, 0));
  b.note_repair(cursiv::DiagnosticKind::Inserted, cursiv::kNoElement, 2, 2);
  a_chain_of(reduce(std::move(c), b, top, nest::byState), count);
}

void a_deep_repair_keeps_turns_over_an_earlier_receiver() {
  constexpr unsigned count = 4096;
  std::string source(count + 1, '(');
  source[0] = '1';
  cursiv::Cst c(source);
  cursiv::detail::CstBuilder b(c);
  b.bind_stream();
  const auto all = b.enter();
  b.leaf(0, 1, kStamp.make(0, 0));
  std::vector<cursiv::detail::CstBuilder::Open> open;
  for (unsigned i = 0; i < count; ++i) open.push_back(b.enter());
  const auto bad = b.enter();
  for (unsigned i = 0; i < count; ++i)
    b.leaf(i + 1, i + 2, kStamp.make(1, 0));
  const auto repaired = b.exit(bad, 1, kStamp.make(cursiv::kNoElement, 0));
  b.stream().at(repaired).recovered = true;
  for (unsigned i = count; i-- > 0;)
    b.exit(open[i], 1, kStamp.make(cursiv::kNoElement, 0));
  const auto top = b.exit(all, 0, kStamp.make(2, 0));
  b.note_repair(cursiv::DiagnosticKind::Incomplete, cursiv::kNoElement, 1,
                count + 1);
  const auto start = std::chrono::steady_clock::now();
  const auto r = reduce(std::move(c), b, top, nest::byState);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  a_chain_of(r, count);
}

void the_first_repair_below_the_consumer_decides_the_turn() {
  for (bool outerRepair : {false, true}) {
    cursiv::Cst c("1(");
    cursiv::detail::CstBuilder b(c);
    b.bind_stream();
    const auto all = b.enter();
    const auto outer = b.enter();
    b.leaf(0, 1, kStamp.make(0, 0));
    const auto inner = b.enter();
    b.leaf(1, 2, kStamp.make(1, 0));
    const auto r1 = b.exit(inner, 1, kStamp.make(cursiv::kNoElement, 0));
    b.stream().at(r1).recovered = true;
    const auto r2 = b.exit(outer, 0, kStamp.make(cursiv::kNoElement, 0));
    b.stream().at(r2).recovered = outerRepair;
    const auto top = b.exit(all, 0, kStamp.make(2, 0));
    // A repair on the consumer itself does not give up its own effects.
    b.stream().at(top).recovered = true;
    b.note_repair(cursiv::DiagnosticKind::Incomplete, cursiv::kNoElement, 0, 2);
    const auto r = reduce(std::move(c), b, top, nest::byState);
    CHECK(r.error == cursiv::AstBuildError::None);
    CHECK(r.nodes().size() == (outerRepair ? 1u : 2u));
    CHECK((cursiv::ast_cast<nest::Num>(r.value) != nullptr) == outerRepair);
    CHECK((cursiv::ast_cast<nest::Call>(r.value) != nullptr) == !outerRepair);
  }
}

void only_a_nest_outside_the_repair_changes_its_receiver() {
  for (bool outside : {false, true}) {
    cursiv::Cst c("1(");
    cursiv::detail::CstBuilder b(c);
    b.bind_stream();
    const auto all = b.enter();
    b.leaf(0, 1, kStamp.make(0, 0));
    const auto outer = b.enter();
    const auto inner = b.enter();
    b.leaf(1, 2, kStamp.make(1, 0));
    const auto a = b.exit(inner, 1,
                          kStamp.make(outside ? cursiv::kNoElement : 1, 0));
    const auto z = b.exit(outer, 1,
                          kStamp.make(outside ? 1 : cursiv::kNoElement, 0));
    b.stream().at(outside ? a : z).recovered = true;
    const auto top = b.exit(all, 0, kStamp.make(2, 0));
    b.note_repair(cursiv::DiagnosticKind::Incomplete, cursiv::kNoElement, 1, 2);
    const auto r = reduce(std::move(c), b, top, nest::byState);
    CHECK(r.error == cursiv::AstBuildError::None);
    CHECK(r.nodes().size() == (outside ? 2u : 3u));
    const auto* call = cursiv::ast_cast<nest::Call>(r.value);
    CHECK(call != nullptr);
    if (call == nullptr) continue;
    CHECK(call->cstNodeId == top);
    if (outside) CHECK(cursiv::ast_cast<nest::Num>(call->callee) != nullptr);
    else CHECK(cursiv::ast_cast<nest::Call>(call->callee) != nullptr);
  }
}

namespace raw_text_cases {

template <bool Text>
void raw_text_writes_keep_the_storage_contract() {
  for (const bool repaired : {false, true})
    for (const bool recovered : {false, true})
      for (const bool empty : {false, true}) {
        cursiv::Cst tree("abc");
        cursiv::detail::CstBuilder builder(tree);
        builder.bind_stream();
        const auto id = builder.leaf(0, empty ? 0 : 3, kStamp.make(0, 0));
        builder.stream().at(id).recovered = recovered;
        cursiv::engine::AstBuildCtx ctx;
        ctx.cst = &tree;
        ctx.repaired = repaired;
        std::vector<cursiv::ParseDiagnostic> diagnostics;
        ctx.diags = &diagnostics;
        constexpr auto directSlot =
            cursiv::engine::conv_slot<void, std::string_view, 0, false, Text>();
        constexpr auto optionalSlot = cursiv::engine::conv_slot<
            void, std::optional<std::string_view>, 0, false, Text>();
        constexpr auto vectorSlot = cursiv::engine::conv_slot<
            void, std::vector<std::string_view>, 0, false, Text>();
        std::string_view direct = "before";
        std::optional<std::string_view> optional;
        std::optional<std::string_view> held = "before";
        std::vector<std::string_view> list{"before"};
        const bool present = !repaired || !recovered || !empty;
        CHECK(cursiv::engine::run_conv(ctx, id, directSlot, direct) == present);
        CHECK(cursiv::engine::run_conv(ctx, id, optionalSlot, optional) == present);
        CHECK(cursiv::engine::run_conv(ctx, id, optionalSlot, held) == present);
        CHECK(cursiv::engine::run_conv(ctx, id, vectorSlot, list) == present);
        CHECK(direct == (present ? tree.text(id) : "before"));
        CHECK(optional.has_value() == present);
        CHECK(held && *held == (present ? tree.text(id) : "before"));
        CHECK(list.size() == (present ? 2u : 1u));
        CHECK(list.front() == "before");
        if (present) {
          CHECK(direct.data() == tree.text(id).data());
          CHECK(optional && *optional == tree.text(id));
          CHECK(optional && optional->data() == direct.data());
          CHECK(list.back() == tree.text(id));
          CHECK(list.back().data() == direct.data());
        }
        CHECK(diagnostics.empty());
      }
}

struct Target : cursiv::Extends<cursiv::AstNode> {};
struct Names : cursiv::Extends<cursiv::AstNode> {
  cursiv::reference<Target> one;
  optional<cursiv::reference<Target>> maybe;
  vector<cursiv::reference<Target>> many;
};

void an_inserted_name_leaves_references_and_handles_untouched() {
  cursiv::Cst tree("abc");
  cursiv::detail::CstBuilder builder(tree);
  builder.bind_stream();
  const auto missing = builder.leaf(0, 0, kStamp.make(0, 0));
  builder.stream().at(missing).recovered = true;
  const auto written = builder.leaf(0, 3, kStamp.make(0, 0));
  const cursiv::detail::AstMetaData meta{};
  cursiv::DefaultDocumentReferences references(meta);
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.refs = &references.handles;
  ctx.resolver = &references;
  ctx.repaired = true;
  std::vector<cursiv::ParseDiagnostic> diagnostics;
  ctx.diags = &diagnostics;
  constexpr auto slot =
      cursiv::engine::conv_slot<void, std::string_view, 0, false>();
  Names names;
  cursiv::detail::RefAccess::bind(
      names.one, cursiv::kNoNode, std::string_view("before"));
  const auto write = [&](cursiv::NodeId id) {
    const cursiv::engine::Pending p{.root = id,
                                    .text = id,
                                    .kind = cursiv::detail::ActKind::assign,
                                    .narrow = 3};
    cursiv::engine::write_into<cursiv::detail::field_tag<&Names::one>>(
        ctx, p, &names, &slot, 0, 0);
    cursiv::engine::write_into<cursiv::detail::field_tag<&Names::maybe>>(
        ctx, p, &names, &slot, 1, 0);
    cursiv::engine::write_into<cursiv::detail::field_tag<&Names::many>>(
        ctx, p, &names, &slot, 2, 0);
  };
  write(missing);
  CHECK(names.one.refText == "before");
  CHECK(!names.maybe && names.many.empty());
  CHECK(references.handles.empty());
  write(written);
  CHECK(names.one.refText == "abc");
  CHECK(names.maybe && names.maybe->refText == "abc");
  CHECK(names.many.size() == 1 && names.many[0].refText == "abc");
  CHECK(references.handles.size() == 3);
  for (std::size_t i = 0; i < references.handles.size(); ++i) {
    CHECK(references.handles[i].owner == &names);
    CHECK(references.handles[i].field == i);
    CHECK(references.handles[i].index == 0);
  }
  CHECK(cursiv::detail::RefAccess::node_id(names.one) == written);
  CHECK(cursiv::detail::RefAccess::narrow(names.one) == 3);
  write(missing);
  CHECK(names.one.refText == "abc");
  CHECK(names.maybe && names.maybe->refText == "abc");
  CHECK(names.many.size() == 1 && names.many[0].refText == "abc");
  CHECK(references.handles.size() == 3);
  CHECK(diagnostics.empty());

  // A genuinely empty match is still a written name, unlike an insertion.
  builder.stream().at(missing).recovered = false;
  Names empty;
  const cursiv::engine::Pending p{.root = missing,
                                  .text = missing,
                                  .kind = cursiv::detail::ActKind::assign};
  cursiv::engine::write_into<cursiv::detail::field_tag<&Names::one>>(
      ctx, p, &empty, &slot, 0, 0);
  CHECK(empty.one.refText.empty());
  CHECK(empty.one.refText.data() == tree.text(missing).data());
  CHECK(references.handles.size() == 4);
  CHECK(references.handles.back().owner == &empty);
}

unsigned customCalls = 0;
struct Custom {
  static std::string_view convert(std::string_view text) {
    ++customCalls;
    return text.substr(1);
  }
};

void custom_and_constant_conversions_keep_their_meaning() {
  cursiv::Cst tree("abc");
  cursiv::detail::CstBuilder builder(tree);
  builder.bind_stream();
  const auto id = builder.leaf(0, 3, kStamp.make(0, 0));
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  constexpr auto custom =
      cursiv::engine::conv_slot<Custom, std::string_view, 0, false>();
  constexpr auto constant = cursiv::engine::conv_slot<
      cursiv::detail::value_conv<true>, bool, 0, false>();
  std::string_view text = "before";
  bool flag = false;
  customCalls = 0;
  CHECK(cursiv::engine::run_conv(ctx, id, custom, text));
  CHECK(text == "bc" && customCalls == 1);
  CHECK(cursiv::engine::run_conv(ctx, id, constant, flag));
  CHECK(flag);
  builder.stream().at(id).end = 0;
  builder.stream().at(id).recovered = true;
  ctx.repaired = true;
  text = "before";
  flag = false;
  CHECK(!cursiv::engine::run_conv(ctx, id, custom, text));
  CHECK(!cursiv::engine::run_conv(ctx, id, constant, flag));
  CHECK(text == "before" && customCalls == 1 && !flag);
}

void a_data_rule_still_joins_its_visible_parts() {
  cursiv::Cst tree("a b");
  cursiv::detail::CstBuilder builder(tree);
  builder.bind_stream();
  const auto opened = builder.enter();
  builder.leaf(0, 1, kStamp.make(0, 0));
  builder.hidden(1, 2, 0);
  builder.leaf(2, 3, kStamp.make(0, 0));
  const auto id = builder.exit(opened, 0, kStamp.make(0, 0));
  const cursiv::engine::AstActions actions{};
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &actions;
  constexpr auto slot =
      cursiv::engine::conv_slot<void, std::string_view, 0, true, false>();
  std::string_view value;
  CHECK(cursiv::engine::run_conv(ctx, id, slot, value));
  CHECK(value == "ab");
}

void run() {
  raw_text_writes_keep_the_storage_contract<true>();
  raw_text_writes_keep_the_storage_contract<false>();
  an_inserted_name_leaves_references_and_handles_untouched();
  custom_and_constant_conversions_keep_their_meaning();
  a_data_rule_still_joins_its_visible_parts();
}

}  // namespace raw_text_cases

namespace text_cases {

std::vector<cursiv::NodeId> converted;
unsigned constantCalls = 0, failedCalls = 0;

std::optional<std::string_view> echo(cursiv::engine::AstBuildCtx& ctx,
                                     cursiv::NodeId id) {
  const auto text = cursiv::engine::joined_parts<true, true>(ctx, id);
  if (text) converted.push_back(id);
  return text;
}
std::optional<std::string_view> constant(cursiv::engine::AstBuildCtx&,
                                         cursiv::NodeId) {
  ++constantCalls;
  return "constant";
}
std::optional<std::string_view> refuse(cursiv::engine::AstBuildCtx& ctx,
                                       cursiv::NodeId id) {
  if (!cursiv::engine::joined_parts<true, true>(ctx, id)) return std::nullopt;
  ++failedCalls;
  return std::nullopt;
}
constexpr cursiv::engine::TextFn conversions[] = {nullptr, echo, constant,
                                                  refuse};
constexpr std::uint8_t inputs[] = {0, 3, 0, 3};
constexpr cursiv::engine::AstActions textActions{.texts = conversions,
                                                 .calls = conversions,
                                                 .entryCount = 4,
                                                 .textInputs = inputs};

void an_independent_query_keeps_its_own_cache_until_it_returns() {
  static constexpr auto child =
      +[](cursiv::engine::AstBuildCtx&,
          cursiv::NodeId) -> std::optional<std::string_view> {
    ++constantCalls;
    return "fresh";
  };
  static constexpr auto root =
      +[](cursiv::engine::AstBuildCtx& ctx, cursiv::NodeId id) {
        return cursiv::engine::evaluate_text(ctx, id, child, false);
      };
  const auto throwing =
      +[](cursiv::engine::AstBuildCtx& ctx,
          cursiv::NodeId id) -> std::optional<std::string_view> {
    (void)root(ctx, id);
    throw 17;
  };
  cursiv::Cst tree("a");
  cursiv::detail::CstBuilder builder(tree);
  const auto id = builder.leaf(0, 1, kStamp.make(0, 0));
  cursiv::engine::TextValues parent;
  parent.calls.try_emplace(id, child, "stale");
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &textActions;
  ctx.textValues = &parent;
  constantCalls = 0;
  for (bool throws : {false, true}) {
    try {
      const auto value = cursiv::engine::evaluate_text(
          ctx, id, throws ? throwing : root, true);
      CHECK(!throws && value && *value == "fresh");
    } catch (int e) {
      CHECK(throws && e == 17);
    }
    CHECK(ctx.textValues == &parent && parent.calls.size() == 1);
    CHECK(parent.calls.at(id).fn == child &&
          parent.calls.at(id).text == "stale");
  }
  CHECK(constantCalls == 2);
}

void deep_conversions_keep_order_and_stop_at_constants() {
  constexpr unsigned depth = 16384;
  cursiv::Cst tree("a");
  cursiv::detail::CstBuilder builder(tree);
  std::vector<cursiv::detail::CstBuilder::Open> open;
  for (unsigned i = 0; i < depth; ++i) open.push_back(builder.enter());
  builder.leaf(0, 1, kStamp.make(0, 0));
  cursiv::NodeId root = 0;
  for (unsigned i = depth; i-- > 0;)
    root = builder.exit(open[i], 0, kStamp.make(1, 0));
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &textActions;
  for (unsigned query = 0; query < 2; ++query) {
    converted.clear();
    const auto value = cursiv::engine::joined_parts(ctx, root);
    CHECK(value && *value == "a");
    CHECK(converted.size() == depth);
    for (unsigned i = 0; i < converted.size(); ++i)
      CHECK(converted[i] == i + 1);
    CHECK(ctx.textValues == nullptr);
  }
  // This converter reads no input: none of the child converters may run.
  builder.stream().at(root).engine = kStamp.make(2, 0);
  converted.clear();
  constantCalls = 0;
  const auto value = cursiv::engine::joined_parts(ctx, root);
  CHECK(value && *value == "constant");
  CHECK(converted.empty() && constantCalls == 1);
}

void a_failed_conversion_stops_before_later_siblings() {
  cursiv::Cst tree("ab");
  cursiv::detail::CstBuilder builder(tree);
  const auto all = builder.enter();
  auto child = builder.enter();
  builder.leaf(0, 1, kStamp.make(0, 0));
  builder.exit(child, 0, kStamp.make(3, 0));
  child = builder.enter();
  builder.leaf(1, 2, kStamp.make(0, 0));
  builder.exit(child, 1, kStamp.make(1, 0));
  const auto root = builder.exit(all, 0, kStamp.make(1, 0));
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &textActions;
  converted.clear();
  failedCalls = 0;
  CHECK(!cursiv::engine::joined_parts(ctx, root));
  CHECK(failedCalls == 1 && converted.empty());
  CHECK(ctx.textValues == nullptr);
}

// What a tree's pool asks of the resource above it.
struct Meter : std::pmr::memory_resource {
  std::size_t bytes = 0;
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    bytes += size;
    return std::pmr::new_delete_resource()->allocate(size, alignment);
  }
  void do_deallocate(void* pointer, std::size_t size,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

void raw_nested_text_is_joined_once_and_parts_keep_their_groups() {
  constexpr unsigned depth = 4096;
  std::string source;
  for (unsigned i = 0; i < depth; ++i) source += "a ";
  source += 'a';
  // The pool takes the default resource as it is built.
  Meter memory;
  std::pmr::memory_resource* const previous =
      std::pmr::set_default_resource(&memory);
  cursiv::Cst tree(source);
  std::pmr::set_default_resource(previous);
  cursiv::detail::CstBuilder builder(tree);
  std::vector<cursiv::detail::CstBuilder::Open> open;
  for (unsigned i = 0; i <= depth; ++i) {
    open.push_back(builder.enter());
    builder.leaf(2 * i, 2 * i + 1, kStamp.make(0, 0));
    if (i != depth) builder.hidden(2 * i + 1, 2 * i + 2, 0);
  }
  cursiv::NodeId root = 0;
  for (unsigned i = depth + 1; i-- > 0;)
    root = builder.exit(open[i], 2 * i, kStamp.make(0, 0));
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &textActions;
  const std::size_t before = memory.bytes;
  const auto value = cursiv::engine::joined_parts<false>(ctx, root);
  CHECK(value && *value == std::string(depth + 1, 'a'));
  // One block of the pool at most; a join per level would ask for about
  // depth * depth / 2 bytes in all.
  CHECK(memory.bytes - before <= 256u * depth);
  const auto parts = cursiv::engine::parts_of<false>(ctx, root);
  CHECK(parts && parts->size() == 2);
  if (parts && parts->size() == 2) {
    CHECK((*parts)[0] == "a");
    CHECK((*parts)[1] == std::string(depth, 'a'));
  }
}

void invented_text_never_runs_its_converter() {
  cursiv::Cst tree("a");
  cursiv::detail::CstBuilder builder(tree);
  const auto open = builder.enter();
  const auto invented = builder.leaf(0, 0, kStamp.make(1, 0));
  builder.stream().at(invented).recovered = true;
  builder.leaf(0, 1, kStamp.make(0, 0));
  const auto root = builder.exit(open, 0, kStamp.make(0, 0));
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.actions = &textActions;
  converted.clear();
  const auto parts = cursiv::engine::parts_of(ctx, root);
  CHECK(parts && parts->size() == 1 && (*parts)[0] == "a");
  CHECK(converted.empty());
}

void run() {
  an_independent_query_keeps_its_own_cache_until_it_returns();
  deep_conversions_keep_order_and_stop_at_constants();
  a_failed_conversion_stops_before_later_siblings();
  raw_nested_text_is_joined_once_and_parts_keep_their_groups();
  invented_text_never_runs_its_converter();
}

}  // namespace text_cases

}  // namespace

int main() {
  raw_text_cases::run();
  text_cases::run();
  only_retained_owners_keep_active_reference_ranks();
  for (bool own : {false, true})
    for (bool parent : {false, true})
      for (bool childRepair : {false, true})
        for (bool unrelated : {false, true}) {
          an_adoption_takes_the_node_its_body_built(false, own, parent,
                                                    childRepair, unrelated);
          an_adoption_takes_the_node_its_body_built(true, own, parent,
                                                    childRepair, unrelated);
        }
  an_adoption_takes_the_node_its_body_built(true, true, true, true, true, true);
  for (bool repaired : {false, true})
    a_direct_write_without_its_node_is_broken(repaired);
  repeated_node_writes_keep_source_order_and_skip_other_families();
  a_node_no_member_takes_leaves_the_node_list();
  a_turn_reads_the_same_by_state_or_by_row();
  unrelated_repairs_leave_a_long_nest_chain_intact();
  a_deep_repair_keeps_turns_over_an_earlier_receiver();
  the_first_repair_below_the_consumer_decides_the_turn();
  only_a_nest_outside_the_repair_changes_its_receiver();
  return cursiv::test::report("AstReducer_test");
}
