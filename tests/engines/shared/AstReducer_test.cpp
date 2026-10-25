#include "cursiv/engines/shared/runtime/AstReducer.hpp"

#include <array>
#include <cstdint>
#include <string>
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

cursiv::ParseResult reduce(cursiv::Cst&& c) {
  return cursiv::AstReducer::build<kStamp>(actions, std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  const auto result = reduce(std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  const auto result = reduce(std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  const auto result = reduce(std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  writes = 0;
  const auto result = reduce(std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  const auto r = cursiv::AstReducer::build<kStamp>(actions, std::move(c));
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
  b.bind_entry(0, c.size(), top);
  b.bind_accepted(true);
  const auto r = cursiv::AstReducer::build<kStamp>(nest::byState, std::move(c));
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

}  // namespace

int main() {
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
  return cursiv::test::report("AstReducer_test");
}
