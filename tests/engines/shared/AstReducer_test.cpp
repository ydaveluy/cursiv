#include "cursiv/engines/shared/runtime/AstReducer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/shared/runtime/AstActions.hpp"
#include "cursiv/engines/shared/runtime/AstConvThunks.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/test/Check.hpp"

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
  std::vector<cursiv::RefHandle> handles;
  for (auto& node : tree) {
    nodes.push_back(&node);
    handles.push_back({.owner = &node});
  }
  handles.push_back({.owner = nullptr});
  cursiv::detail::prune_orphans(nodes, handles, &tree[0], [] {});
  CHECK(nodes.size() == 5u);
  CHECK(handles.size() == 12);
  for (unsigned i = 0; i < handles.size(); ++i)
    CHECK(handles[i].owner == (i < 5 ? &tree[i] : nullptr));
  for (unsigned i = 0; i < nodes.size(); ++i) CHECK(nodes[i] == &tree[i]);
}

constexpr cursiv::StampLayout kStamp{};

struct Child : cursiv::Extends<cursiv::AstNode> {};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Child* child = nullptr;
};
using ChildField = cursiv::detail::field_tag<&Root::child>;
constexpr std::uint64_t derives[] = {1, 2};
constexpr std::uint16_t targets[] = {1};
constexpr cursiv::detail::FieldDesc fields[] = {
    {"child", cursiv::detail::FieldNature::node,
     cursiv::detail::FieldStorage::direct, 1, 0, targets, 1}};
constexpr cursiv::AstMetaData meta{.derivesBits = derives,
                                   .typeCount = 2,
                                   .typeWords = 1,
                                   .fields = fields,
                                   .referenceFields = {},
                                   .fieldCount = 1};
constexpr cursiv::detail::MakeFn makes[] = {&cursiv::detail::make_node<Root>,
                                            &cursiv::detail::make_node<Child>};
// The one member takes the child's family.
constexpr std::uint64_t takeBits[] = {2};

unsigned writes = 0;
bool root_writer(cursiv::AstReducer& b, const cursiv::Pending& p,
                 const cursiv::Made& m) {
  if (p.field != 0) return false;
  ++writes;
  b.write<ChildField, nullptr, false>(0, p, m);
  return true;
}
bool child_writer(cursiv::AstReducer&, const cursiv::Pending&,
                  const cursiv::Made&) {
  return false;
}
constexpr cursiv::AstApplyFn applies[] = {root_writer, child_writer};

void root(cursiv::AstReducer& b, cursiv::NodeId id) {
  b.push_made(id, b.create<makes[0], root_writer, false>(0, id));
}
void adopt(cursiv::AstReducer& b, cursiv::NodeId id) {
  const auto m = b.adopt<false>(1, id);
  if (!b.failed()) b.owe_write(id, 0, cursiv::ActKind::assign, m);
}
void child(cursiv::AstReducer& b, cursiv::NodeId id) {
  b.push_made(id, b.create<makes[1], child_writer, false>(1, id));
}
void direct(cursiv::AstReducer& b, cursiv::NodeId id) {
  const auto h =
      b.node_write(id, 0, cursiv::kNoNarrow, cursiv::ActKind::assign);
  const auto m = b.create<makes[0], root_writer, false>(0, id);
  if (b.failed()) return;
  ++writes;
  b.fill<ChildField, nullptr, false, false>(0, h, m);
  if (b.failed()) return;
  b.push_made(id, m);
}
void direct_twice(cursiv::AstReducer& b, cursiv::NodeId id) {
  const auto first =
      b.node_write(id, 0, cursiv::kNoNarrow, cursiv::ActKind::assign);
  const auto second =
      b.node_write(id, 0, cursiv::kNoNarrow, cursiv::ActKind::assign);
  const auto m = b.create<makes[0], root_writer, false>(0, id);
  if (b.failed()) return;
  ++writes;
  b.fill<ChildField, nullptr, false, false>(0, first, m);
  if (b.failed()) return;
  ++writes;
  b.fill<ChildField, nullptr, false, false>(0, second, m);
  if (b.failed()) return;
  b.push_made(id, m);
}
constexpr cursiv::AstEntryFn entries[] = {root, adopt, child, direct,
                                          direct_twice};
const cursiv::AstActions actions{.meta = &meta,
                                 .makes = makes,
                                 .entries = entries,
                                 .entryCount = 5,
                                 .applies = applies,
                                 .takeBits = takeBits};

// The tree closed on `top` and accepted, reduced with the write count reset.
cursiv::ParseResult reduce(cursiv::Cst&& c, cursiv::CstBuilder& b,
                           cursiv::NodeId top,
                           const cursiv::AstActions& with = actions) {
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  return cursiv::AstReducer::build<kStamp>(with, std::move(c));
}

// An adoption takes the node its body built or none: a repair excuses an
// effect that does not land, never a node that was not built.
void an_adoption_takes_the_node_its_body_built(bool node, bool ownRepair,
                                               bool parentRepair,
                                               bool childRepair,
                                               bool unrelatedRepair,
                                               bool badExtent = false) {
  cursiv::Cst c("xx");
  cursiv::CstBuilder b(c);
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
    b.note_repair(cursiv::DiagnosticKind::deleted, cursiv::kNoElement, 0, 0);
  const auto result = reduce(std::move(c), b, top);
  const bool good = node && !badExtent;
  CHECK((result.error == cursiv::AstBuildError::none) == good);
  CHECK((result.value != nullptr) == good);
  if (good) {
    const auto* r = cursiv::ast_cast<Root>(result.value);
    CHECK(r != nullptr && r->child != nullptr && r->child->parent == r);
    CHECK(result.value->parent == nullptr);
    CHECK(result.nodes.size() == 2);
  }
  CHECK(writes == (good ? 1u : 0u));
}

// A node member owed no node is a broken tree, repaired or not.
void a_direct_write_without_its_node_is_broken(bool repaired) {
  cursiv::Cst c("");
  cursiv::CstBuilder b(c);
  b.bind_stream();
  const auto top = b.leaf(0, 0, kStamp.make(3, 0));
  b.stream().at(top).recovered = repaired;
  if (repaired)
    b.note_repair(cursiv::DiagnosticKind::inserted, cursiv::kNoElement, 0, 0);
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::malformed_cst);
  CHECK(result.value == nullptr && writes == 1);
}

void repeated_node_writes_keep_source_order_and_skip_other_families() {
  cursiv::Cst c("abcd");
  cursiv::CstBuilder b(c);
  b.bind_stream();
  const auto opened = b.enter();
  b.leaf(0, 1, kStamp.make(2, 0));
  b.leaf(1, 2, kStamp.make(0, 0));
  const auto second = b.leaf(2, 3, kStamp.make(2, 0));
  b.leaf(3, 4, kStamp.make(2, 0));
  const auto top = b.exit(opened, 0, kStamp.make(4, 0));
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::none);
  const auto* r = result.value_as<Root>();
  CHECK(r != nullptr && r->child != nullptr);
  if (r == nullptr || r->child == nullptr) return;
  CHECK(r->child->cstNodeId == second);
  CHECK(r->child->parent == r && r->parent == nullptr);
  CHECK(result.nodes.size() == 2);
  CHECK(writes == 2);
}

// A node no member takes is not in the node list.
void a_node_no_member_takes_leaves_the_node_list() {
  cursiv::Cst c("ab");
  cursiv::CstBuilder b(c);
  b.bind_stream();
  const auto opened = b.enter();
  const auto taken = b.leaf(0, 1, kStamp.make(2, 0));
  b.leaf(1, 2, kStamp.make(2, 0));
  const auto top = b.exit(opened, 0, kStamp.make(3, 0));
  const auto result = reduce(std::move(c), b, top);
  CHECK(result.error == cursiv::AstBuildError::none);
  const auto* r = result.value_as<Root>();
  CHECK(r != nullptr && r->child != nullptr && r->child->cstNodeId == taken);
  CHECK(result.nodes.size() == 2);
  for (const cursiv::AstNode* node : result.nodes)
    CHECK(node == r || node->parent == r);
  CHECK(writes == 1);
}

namespace nest {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  bool flag = false;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
};
using FlagField = cursiv::detail::field_tag<&Num::flag>;
using CalleeField = cursiv::detail::field_tag<&Call::callee>;
constexpr std::uint64_t derives[] = {7, 2, 4};
constexpr std::uint16_t targets[] = {0};
constexpr cursiv::detail::FieldDesc fields[] = {
    {"flag", cursiv::detail::FieldNature::value,
     cursiv::detail::FieldStorage::direct, cursiv::detail::kNoIndex, 1, targets,
     0},
    {"callee", cursiv::detail::FieldNature::node,
     cursiv::detail::FieldStorage::direct, 0, 2, targets, 1}};
constexpr cursiv::AstMetaData meta{.derivesBits = derives,
                                   .typeCount = 3,
                                   .typeWords = 1,
                                   .fields = fields,
                                   .referenceFields = {},
                                   .fieldCount = 2};
constexpr cursiv::detail::MakeFn makes[] = {&cursiv::detail::make_node<Expr>,
                                            &cursiv::detail::make_node<Num>,
                                            &cursiv::detail::make_node<Call>};
constexpr std::uint64_t takeBits[] = {0, 7};

bool expr_writer(cursiv::AstReducer&, const cursiv::Pending&,
                 const cursiv::Made&) {
  return false;
}
bool num_writer(cursiv::AstReducer& b, const cursiv::Pending& p,
                const cursiv::Made& m) {
  if (p.field != 0) return false;
  b.write<FlagField, nullptr, false>(0, p, m);
  return true;
}
bool call_writer(cursiv::AstReducer& b, const cursiv::Pending& p,
                 const cursiv::Made& m) {
  if (p.field != 1) return false;
  b.write<CalleeField, nullptr, false>(1, p, m);
  return true;
}
constexpr cursiv::AstApplyFn applies[] = {expr_writer, num_writer, call_writer};

// The receiver, then its flag owed to whatever the region is when it lands.
void number(cursiv::AstReducer& b, cursiv::NodeId id) {
  b.push_made(id, b.create<makes[1], num_writer, true>(1, id));
  b.owe_effect(id, 1, 0, cursiv::ActKind::flag);
}
void turn(cursiv::AstReducer& b, cursiv::NodeId id) {
  b.owe_effect(id, 2, 1, cursiv::ActKind::nest);
}
void close(cursiv::AstReducer& b, cursiv::NodeId id) {
  const auto m = b.adopt<true>(0, id);
  if (!b.failed()) b.push_made(id, m);
}
constexpr cursiv::AstEntryFn entries[] = {number, turn, close};
constexpr std::uint8_t turns[] = {0, 1, 0};
constexpr cursiv::AstActions byState{.meta = &meta,
                                     .makes = makes,
                                     .entries = entries,
                                     .entryCount = 3,
                                     .applies = applies,
                                     .takeBits = takeBits,
                                     .anyNest = true,
                                     .turnsByState = true};
constexpr cursiv::AstActions byRow{.meta = &meta,
                                   .makes = makes,
                                   .entries = entries,
                                   .entryCount = 3,
                                   .applies = applies,
                                   .takeBits = takeBits,
                                   .anyNest = true,
                                   .turns = turns};

// `1(` read as a call turn over its receiver, closed as an expression; a
// repair may have given the turn up, its receiver with it.
std::string reduce_turn(const cursiv::AstActions& actions, bool stateSays,
                        bool abandoned, unsigned badExtent = 0) {
  cursiv::Cst c("1(");
  cursiv::CstBuilder b(c);
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
    b.note_repair(cursiv::DiagnosticKind::incomplete, cursiv::kNoElement, 0, 2);
  const auto r = reduce(std::move(c), b, top, actions);
  if (r.error != cursiv::AstBuildError::none || r.value == nullptr)
    return "error";
  const auto* call = cursiv::ast_cast<Call>(r.value);
  const auto* num =
      cursiv::ast_cast<Num>(call != nullptr ? call->callee : r.value);
  std::string out = num == nullptr ? "?" : num->flag ? "Num!" : "Num";
  if (call != nullptr) out = "Call(" + out + ")";
  return out + " nodes=" + std::to_string(r.nodes.size());
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
  CHECK(r.error == cursiv::AstBuildError::none);
  CHECK(r.nodes.size() == count + 1);
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
  cursiv::CstBuilder b(c);
  b.bind_stream();
  const auto all = b.enter();
  b.leaf(0, 1, kStamp.make(0, 0));
  for (unsigned i = 0; i < count; ++i) b.leaf(1, 2, kStamp.make(1, 0));
  for (unsigned i = 0; i < count; ++i) {
    const auto leaf = b.leaf(2, 2, kStamp.make(cursiv::kNoElement, 0));
    b.stream().at(leaf).recovered = true;
  }
  const auto top = b.exit(all, 0, kStamp.make(2, 0));
  b.note_repair(cursiv::DiagnosticKind::inserted, cursiv::kNoElement, 2, 2);
  a_chain_of(reduce(std::move(c), b, top, nest::byState), count);
}

void a_deep_repair_keeps_turns_over_an_earlier_receiver() {
  constexpr unsigned count = 4096;
  std::string source(count + 1, '(');
  source[0] = '1';
  cursiv::Cst c(source);
  cursiv::CstBuilder b(c);
  b.bind_stream();
  const auto all = b.enter();
  b.leaf(0, 1, kStamp.make(0, 0));
  std::vector<cursiv::CstBuilder::Open> open;
  for (unsigned i = 0; i < count; ++i) open.push_back(b.enter());
  const auto bad = b.enter();
  for (unsigned i = 0; i < count; ++i)
    b.leaf(i + 1, i + 2, kStamp.make(1, 0));
  const auto repaired = b.exit(bad, 1, kStamp.make(cursiv::kNoElement, 0));
  b.stream().at(repaired).recovered = true;
  for (unsigned i = count; i-- > 0;)
    b.exit(open[i], 1, kStamp.make(cursiv::kNoElement, 0));
  const auto top = b.exit(all, 0, kStamp.make(2, 0));
  b.note_repair(cursiv::DiagnosticKind::incomplete, cursiv::kNoElement, 1,
                 count + 1);
  const auto start = std::chrono::steady_clock::now();
  const auto r = reduce(std::move(c), b, top, nest::byState);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  a_chain_of(r, count);
}

void the_first_repair_below_the_consumer_decides_the_turn() {
  for (bool outerRepair : {false, true}) {
    cursiv::Cst c("1(");
    cursiv::CstBuilder b(c);
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
    b.note_repair(cursiv::DiagnosticKind::incomplete, cursiv::kNoElement, 0, 2);
    const auto r = reduce(std::move(c), b, top, nest::byState);
    CHECK(r.error == cursiv::AstBuildError::none);
    CHECK(r.nodes.size() == (outerRepair ? 1u : 2u));
    CHECK((cursiv::ast_cast<nest::Num>(r.value) != nullptr) == outerRepair);
    CHECK((cursiv::ast_cast<nest::Call>(r.value) != nullptr) == !outerRepair);
  }
}

void only_a_nest_outside_the_repair_changes_its_receiver() {
  for (bool outside : {false, true}) {
    cursiv::Cst c("1(");
    cursiv::CstBuilder b(c);
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
    b.note_repair(cursiv::DiagnosticKind::incomplete, cursiv::kNoElement, 1, 2);
    const auto r = reduce(std::move(c), b, top, nest::byState);
    CHECK(r.error == cursiv::AstBuildError::none);
    CHECK(r.nodes.size() == (outside ? 2u : 3u));
    const auto* call = cursiv::ast_cast<nest::Call>(r.value);
    CHECK(call != nullptr);
    if (call == nullptr) continue;
    CHECK(call->cstNodeId == top);
    if (outside) CHECK(cursiv::ast_cast<nest::Num>(call->callee) != nullptr);
    else CHECK(cursiv::ast_cast<nest::Call>(call->callee) != nullptr);
  }
}

namespace text_cases {

std::vector<cursiv::NodeId> converted;
unsigned constantCalls = 0, failedCalls = 0;

std::optional<std::string_view> echo(cursiv::AstBuildCtx& ctx,
                                     cursiv::NodeId id) {
  const auto text = cursiv::detail::joined_parts<true, true>(ctx, id);
  if (text) converted.push_back(id);
  return text;
}
std::optional<std::string_view> constant(cursiv::AstBuildCtx&, cursiv::NodeId) {
  ++constantCalls;
  return "constant";
}
std::optional<std::string_view> refuse(cursiv::AstBuildCtx& ctx,
                                       cursiv::NodeId id) {
  if (!cursiv::detail::joined_parts<true, true>(ctx, id)) return std::nullopt;
  ++failedCalls;
  return std::nullopt;
}
constexpr cursiv::detail::TextFn conversions[] = {nullptr, echo, constant,
                                                  refuse};
constexpr std::uint8_t inputs[] = {0, 3, 0, 3};
constexpr cursiv::AstActions textActions{.texts = conversions,
                                         .calls = conversions,
                                         .entryCount = 4,
                                         .textInputs = inputs};

void deep_conversions_keep_order_and_stop_at_constants() {
  constexpr unsigned depth = 16384;
  cursiv::Cst tree("a");
  cursiv::CstBuilder builder(tree);
  std::vector<cursiv::CstBuilder::Open> open;
  for (unsigned i = 0; i < depth; ++i) open.push_back(builder.enter());
  builder.leaf(0, 1, kStamp.make(0, 0));
  cursiv::NodeId root = 0;
  for (unsigned i = depth; i-- > 0;)
    root = builder.exit(open[i], 0, kStamp.make(1, 0));
  cursiv::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.mem = tree.memory();
  ctx.actions = &textActions;
  for (unsigned query = 0; query < 2; ++query) {
    converted.clear();
    const auto value = cursiv::detail::joined_parts(ctx, root);
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
  const auto value = cursiv::detail::joined_parts(ctx, root);
  CHECK(value && *value == "constant");
  CHECK(converted.empty() && constantCalls == 1);
}

void a_failed_conversion_stops_before_later_siblings() {
  cursiv::Cst tree("ab");
  cursiv::CstBuilder builder(tree);
  const auto all = builder.enter();
  auto child = builder.enter();
  builder.leaf(0, 1, kStamp.make(0, 0));
  builder.exit(child, 0, kStamp.make(3, 0));
  child = builder.enter();
  builder.leaf(1, 2, kStamp.make(0, 0));
  builder.exit(child, 1, kStamp.make(1, 0));
  const auto root = builder.exit(all, 0, kStamp.make(1, 0));
  cursiv::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.mem = tree.memory();
  ctx.actions = &textActions;
  converted.clear();
  failedCalls = 0;
  CHECK(!cursiv::detail::joined_parts(ctx, root));
  CHECK(failedCalls == 1 && converted.empty());
  CHECK(ctx.textValues == nullptr);
}

struct Meter : std::pmr::memory_resource {
  std::pmr::memory_resource* upstream;
  std::size_t bytes = 0;
  explicit Meter(std::pmr::memory_resource* resource) : upstream(resource) {}
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    bytes += size;
    return upstream->allocate(size, alignment);
  }
  void do_deallocate(void* pointer, std::size_t size,
                     std::size_t alignment) override {
    upstream->deallocate(pointer, size, alignment);
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
  cursiv::Cst tree(source);
  cursiv::CstBuilder builder(tree);
  std::vector<cursiv::CstBuilder::Open> open;
  for (unsigned i = 0; i <= depth; ++i) {
    open.push_back(builder.enter());
    builder.leaf(2 * i, 2 * i + 1, kStamp.make(0, 0));
    if (i != depth) builder.hidden(2 * i + 1, 2 * i + 2, 0);
  }
  cursiv::NodeId root = 0;
  for (unsigned i = depth + 1; i-- > 0;)
    root = builder.exit(open[i], 2 * i, kStamp.make(0, 0));
  Meter memory(tree.memory());
  cursiv::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.mem = &memory;
  ctx.actions = &textActions;
  const auto value = cursiv::detail::joined_parts<false>(ctx, root);
  CHECK(value && *value == std::string(depth + 1, 'a'));
  CHECK(memory.bytes <= 2u * (depth + 1));
  const auto parts = cursiv::detail::parts_of<false>(ctx, root);
  CHECK(parts && parts->size() == 2);
  if (parts && parts->size() == 2) {
    CHECK((*parts)[0] == "a");
    CHECK((*parts)[1] == std::string(depth, 'a'));
  }
}

void invented_text_never_runs_its_converter() {
  cursiv::Cst tree("a");
  cursiv::CstBuilder builder(tree);
  const auto open = builder.enter();
  const auto invented = builder.leaf(0, 0, kStamp.make(1, 0));
  builder.stream().at(invented).recovered = true;
  builder.leaf(0, 1, kStamp.make(0, 0));
  const auto root = builder.exit(open, 0, kStamp.make(0, 0));
  cursiv::AstBuildCtx ctx;
  ctx.cst = &tree;
  ctx.mem = tree.memory();
  ctx.actions = &textActions;
  converted.clear();
  const auto parts = cursiv::detail::parts_of(ctx, root);
  CHECK(parts && parts->size() == 1 && (*parts)[0] == "a");
  CHECK(converted.empty());
}

void run() {
  deep_conversions_keep_order_and_stop_at_constants();
  a_failed_conversion_stops_before_later_siblings();
  raw_nested_text_is_joined_once_and_parts_keep_their_groups();
  invented_text_never_runs_its_converter();
}

}  // namespace text_cases

}  // namespace

int main() {
  text_cases::run();
  only_retained_owners_keep_active_reference_ranks();
  for (bool own : {false, true})
    for (bool parent : {false, true})
      for (bool kid : {false, true})
        for (bool unrelated : {false, true}) {
          an_adoption_takes_the_node_its_body_built(false, own, parent, kid,
                                                    unrelated);
          an_adoption_takes_the_node_its_body_built(true, own, parent, kid,
                                                    unrelated);
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
