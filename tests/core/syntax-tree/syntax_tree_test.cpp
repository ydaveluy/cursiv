// The trees a parse hands over: the AST tables and their reducer, the node
// and its reflection, the converters, the CST and its DSL surface, the walks
// over it, and a reference resolved on read.

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cfenv>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "aball/generated/All/AstReflection.hpp"
#include "aball/generated/All/Module.hpp"
#include "aball/generated/All/Table.hpp"
#include "abbare/generated/G/AstReflection.hpp"
#include "abbare/generated/G/Module.hpp"
#include "conv/generated/G/Module.hpp"
#include "core/syntax-tree/AstBuilderGrammars.hpp"
#include "core/syntax-tree/ConvertGrammars.hpp"
#include "core/syntax-tree/CstNodeGrammars.hpp"
#include "core/syntax-tree/DualGrammar.hpp"
#include "core/syntax-tree/EntryHeld.hpp"
#include "cstinfix/generated/InfixG/Module.hpp"
#include "cstinfix/generated/InfixG/Table.hpp"
#include "cstlex/generated/LexG/Module.hpp"
#include "cstlex/generated/LexG/Table.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/lr/gen/LrLowering.hpp"
#include "cursiv/lr/gen/lr_build.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/shared/gen/AstRowOf.hpp"
#include "cursiv/shared/runtime/AstConvThunks.hpp"
#include "cursiv/shared/runtime/AstReducer.hpp"
#include "cursiv/shared/runtime/ast_fields.hpp"
#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/symbols/NamedNodes.hpp"
#include "cursiv/syntax-tree/AstArena.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/AstWalk.hpp"
#include "cursiv/syntax-tree/BlockArena.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/DefaultAstReflection.hpp"
#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/TypeName.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/ReferenceDescriptionProvider.hpp"
#include "dualll/generated/Ll/Module.hpp"
#include "duallr/generated/Lr/Module.hpp"
#include "support/World.hpp"

// The AST tables through the erased reflection, then the reducer: one case
// per member form, its structural errors, and its two shapes of its own.
namespace ast_builder {

namespace {

// The metadata is the language's table, so a white-box test asks it by the key
// a service holds: an index found by owner and member name, never a member
// pointer, which the type-erased reflection does not know.

using support::parser_of;

template <auto Create>
const cursiv::DefaultAstReflection& reflection_of() {
  static const std::unique_ptr<cursiv::AstReflection> r = Create();
  return static_cast<const cursiv::DefaultAstReflection&>(*r);
}

// The grammar most tests below read, and its tables.
const cursiv::Parser& engine() {
  return parser_of<&tab::generated::All::create_parser>();
}
const cursiv::DefaultAstReflection& refl() {
  return reflection_of<&tab::generated::All::create_ast_reflection>();
}

template <class T>
inline constexpr const cursiv::AstTypeTag* kTag = &cursiv::ast_type_tag<T>;

std::uint16_t field_of(const cursiv::DefaultAstReflection& refl,
                       const cursiv::AstTypeTag* owner, std::string_view name) {
  const cursiv::AstMetaData& m = refl.metaData();
  const std::uint16_t at = refl.typeIndexOf(owner);
  for (std::uint16_t f = 0; f < m.fieldCount; ++f)
    if (m.fields[f].owner == at && m.fields[f].name == name) return f;
  return cursiv::detail::kNoIndex;
}

// A member this grammar does not declare answers a neutral descriptor rather
// than reading past the table.
const cursiv::detail::FieldDesc& desc_of(
    const cursiv::DefaultAstReflection& refl, const cursiv::AstTypeTag* owner,
    std::string_view name) {
  static constexpr cursiv::detail::FieldDesc kNone{};
  const std::uint16_t f = field_of(refl, owner, name);
  return f == cursiv::detail::kNoIndex ? kNone : refl.metaData().fields[f];
}

// A member claims columns the table knows: it names an owner the type table
// has, and a target when it points at anything at all.
bool fields_are_coherent(const cursiv::DefaultAstReflection& refl) {
  const cursiv::AstMetaData& m = refl.metaData();
  for (std::uint16_t f = 0; f < m.fieldCount; ++f) {
    const cursiv::detail::FieldDesc& d = m.fields[f];
    if (d.owner >= m.typeCount) return false;
    if (d.target != cursiv::detail::kNoIndex && d.target >= m.typeCount)
      return false;
    for (std::uint16_t i = 0; i < d.target_count; ++i)
      if (d.targets[i] >= m.typeCount) return false;
  }
  return true;
}

// The reducer asks `derives` of indices, a tag holder walks the tag's
// bases: both agree for every pair.
bool folds_agree(const cursiv::DefaultAstReflection& refl) {
  const cursiv::AstMetaData& m = refl.metaData();
  for (std::uint16_t b = 0; b < m.typeCount; ++b)
    for (std::uint16_t s = 0; s < m.typeCount; ++s)
      if (m.derives(b, s) != m.tag_of(s)->derivesFrom(m.tag_of(b)))
        return false;
  return true;
}

namespace hier {

using namespace tab::hier;
using cursiv::detail::FieldContent;
using cursiv::detail::FieldStorage;

// Distinct types, distinct addresses -- the identity is the address.
static_assert(kTag<Mid> != kTag<Leaf>);
static_assert(kTag<Mid> != kTag<Base>);
static_assert(kTag<Mid>->name == "Mid");
static_assert(kTag<Leaf>->name == "Leaf");

// The three-level fold: a Mid-tagged and a Leaf-tagged node are both Base,
// only the Leaf-tagged one is a Leaf -- and a tag answers its own family, self
// first.
static_assert(kTag<Mid>->derivesFrom(kTag<Base>));
static_assert(kTag<Leaf>->derivesFrom(kTag<Base>));
static_assert(kTag<Leaf>->derivesFrom(kTag<Mid>));
static_assert(kTag<Leaf>->derivesFrom(kTag<Leaf>));
static_assert(!kTag<Mid>->derivesFrom(kTag<Leaf>));
static_assert(!kTag<Holder>->derivesFrom(kTag<Base>));
static_assert(!kTag<Base>->derivesFrom(kTag<Leaf>));
static_assert(cursiv::ast_cast<Base>(nullptr) == nullptr);

// The same question through a node, which is how a service asks it.
constexpr bool node_queries() {
  Mid m;
  m.type = kTag<Mid>;
  Leaf l;
  l.type = kTag<Leaf>;
  Mid untagged;
  return cursiv::ast_cast<Base>(&m) != nullptr &&
         cursiv::ast_cast<Leaf>(&m) == nullptr &&
         cursiv::ast_cast<Base>(&l) != nullptr &&
         cursiv::ast_cast<Mid>(&l) != nullptr && m.type->name == "Mid" &&
         l.type->name == "Leaf" && untagged.type == nullptr &&
         cursiv::ast_cast<Base>(&untagged) == nullptr;
}
static_assert(node_queries());

void the_tables_read_back() {
  const cursiv::DefaultAstReflection& r = refl();
  const cursiv::AstMetaData& m = r.metaData();

  // The two rules that write `name` share one entry: the action they name is
  // the same action, so the list holds it once.
  CHECK(std::count_if(m.fields, m.fields + m.fieldCount, [&](const auto& f) {
    const auto* owner = r.typeTagOf(f.owner);
    return owner == kTag<Base> || owner == kTag<Mid> || owner == kTag<Leaf> ||
           owner == kTag<Holder>;
  }) == 6);

  // Decoded kinds and storages.
  const cursiv::detail::FieldDesc& name = desc_of(r, kTag<Base>, "name");
  const cursiv::detail::FieldDesc& items = desc_of(r, kTag<Holder>, "items");
  const cursiv::detail::FieldDesc& level = desc_of(r, kTag<Holder>, "level");
  const cursiv::detail::FieldDesc& tag = desc_of(r, kTag<Holder>, "tag");
  const cursiv::detail::FieldDesc& target = desc_of(r, kTag<Holder>, "target");
  CHECK(name.content == FieldContent::value);
  CHECK(name.storage == FieldStorage::direct);
  CHECK(items.content == FieldContent::node);
  CHECK(items.storage == FieldStorage::vector);
  CHECK(level.content == FieldContent::value);
  CHECK(level.storage == FieldStorage::optional);
  CHECK(tag.content == FieldContent::variant);
  CHECK(target.content == FieldContent::reference);
  // The identifier is decoration, but it is filled.
  CHECK(name.name == "name");
  CHECK(target.name == "target");

  // The two type columns the interpreter checks before it hands a node to a
  // writer: what the member accepts, and who declares it.
  CHECK(items.target == r.typeIndexOf(kTag<Base>));
  CHECK(items.owner == r.typeIndexOf(kTag<Holder>));
  CHECK(target.target == r.typeIndexOf(kTag<Mid>));
  // A value member points at nothing; its owner is a class the grammar never
  // builds and never points at, which is why owners are a type source of their
  // own.
  CHECK(name.target == cursiv::detail::kNoIndex);
  CHECK(name.owner == r.typeIndexOf(kTag<Base>));

  // The converter of NUM into the variant's int64 alternative, and the default
  // path into a reference's name: two different entries.
  CHECK(cursiv::engine::ast_registry<tab::hier::G,
                                     cursiv::engine::LrLowering<tab::hier::G>>()
            .convCount >= 2);

  // Built types and the pointee of a node member, each once.
  CHECK(std::count_if(m.types, m.types + m.typeCount, [](const auto* t) {
    return t == kTag<Base> || t == kTag<Mid> || t == kTag<Leaf> || t == kTag<Holder>;
  }) == 4);
  CHECK(r.typeIndexOf(kTag<Base>) != r.typeIndexOf(kTag<Mid>));

  // Self first: a type's own tag is rank 0 of its family, so the exact-type
  // question -- the dominant one -- is one compare.
  CHECK(r.getAllSubTypes(kTag<Base>)[0] == kTag<Base>);
  CHECK(r.getAllSubTypes(kTag<Mid>)[0] == kTag<Mid>);
  CHECK(r.getAllSubTypes(kTag<Leaf>)[0] == kTag<Leaf>);

  CHECK(folds_agree(r));
  CHECK(fields_are_coherent(r));

  // Bounds-checked, like every other index-keyed query here.
  CHECK(r.fieldName(m.fieldCount).empty());
  CHECK(r.referenceOwner(m.fieldCount) == nullptr);
  CHECK(r.referenceTargets(m.fieldCount).empty());
  CHECK(r.referenceAccepts(m.fieldCount).empty());

  // The admissible tags of a reference field -- the linker's entry point:
  // `reference<Mid>` accepts a Mid and everything under it, {Mid, Leaf}.
  const std::uint16_t targetAt = field_of(r, kTag<Holder>, "target");
  const cursiv::AcceptSet admits = r.referenceAccepts(targetAt);
  CHECK(cursiv::AstReflection::accepts(admits, r.typeIndexOf(kTag<Mid>)));
  CHECK(cursiv::AstReflection::accepts(admits, r.typeIndexOf(kTag<Leaf>)));
  CHECK(!cursiv::AstReflection::accepts(admits, r.typeIndexOf(kTag<Base>)));
  CHECK(!cursiv::AstReflection::accepts(admits, r.typeIndexOf(kTag<Holder>)));
  // A value field points at nothing.
  CHECK(r.referenceTargets(field_of(r, kTag<Base>, "name")).empty());
  // The field key is the table's index, and the identifier reads back.
  CHECK(r.fieldName(targetAt) == "target");

  // The keyed lookup a caller holding only a container and a name goes
  // through -- the completion and MCP path into the same table.
  CHECK(r.referenceIndexOf(kTag<Holder>, "target") == targetAt);
  CHECK(r.referenceIndexOf(kTag<Holder>, "missing") == cursiv::kNoReference);
}

}  // namespace hier

// The climb writes left/op/right; no directive names them, so nothing in the
// body declares them -- yet the field writer is indexed, and a service asking
// about an operand needs its slot. The rule kind contributes them.
namespace infix {

using namespace tab::climb;

void the_climb_contributes_its_members() {
  const cursiv::DefaultAstReflection& r = refl();
  const cursiv::AstMetaData& m = r.metaData();
  const std::uint16_t left = field_of(r, kTag<Bin>, "left");
  const std::uint16_t op = field_of(r, kTag<Bin>, "op");
  const std::uint16_t right = field_of(r, kTag<Bin>, "right");
  CHECK(left < m.fieldCount && op < m.fieldCount && right < m.fieldCount);
  CHECK(left != right);
  CHECK(m.fields[left].content == cursiv::detail::FieldContent::node);
  CHECK(m.fields[op].content == cursiv::detail::FieldContent::value);
  CHECK(m.fields[op].name == "op");
  CHECK(r.typeIndexOf(kTag<Bin>) != cursiv::kNoType);
  // The operands' pointee is registered as a type even though no rule builds
  // it.
  CHECK(r.typeIndexOf(kTag<Expr>) != cursiv::kNoType);
}

}  // namespace infix

// Two climbs, one class: both rules below build `Two`, over different members.
// A descriptor keyed by type alone would hand the second rule the first's
// three member pointers, silently -- no error, no diagnostic.
namespace climbs {

using namespace tab::climbs;

void two_climbs_over_one_class_keep_their_own_members() {
  cursiv::ParseResult r = engine().parse("$twoclimbs 1+2 3@4");
  CHECK(r.accepted);
  auto* const h = r.value_as<Holder2>();
  CHECK(r.error == cursiv::AstBuildError::none);
  if (h == nullptr || h->a == nullptr || h->b == nullptr) {
    CHECK(false);
    return;
  }
  CHECK(h->a->op == "+" && h->a->left != nullptr && h->a->right != nullptr);
  CHECK(h->a->op2.empty() && h->a->l2 == nullptr && h->a->r2 == nullptr);
  // E2's triple alone -- E1's stayed empty.
  CHECK(h->b->op2 == "@" && h->b->l2 != nullptr && h->b->r2 != nullptr);
  CHECK(h->b->op.empty() && h->b->left == nullptr && h->b->right == nullptr);
}

}  // namespace climbs

namespace region {

using namespace tab::region;

static_assert(kTag<Wrap>->derivesFrom(kTag<Node>));
static_assert(!kTag<Node>->derivesFrom(kTag<Wrap>));

// A nest names both the type it creates and the member it moves into.
void the_nest_names_a_node_member() {
  const cursiv::DefaultAstReflection& r = refl();
  CHECK(desc_of(r, kTag<Wrap>, "inner").content ==
        cursiv::detail::FieldContent::node);
  CHECK(desc_of(r, kTag<Wrap>, "inner").target == r.typeIndexOf(kTag<Node>));
}

// Adoption: `Root` names no type, its head is the node `Inner`'s region
// built, and each `*` wraps what is current.
void adopts_and_wraps() {
  {
    cursiv::ParseResult r = engine().parse("$region abc");
    CHECK(r.accepted && static_cast<bool>(r));
    Node* const n = r.value_as<Node>();
    CHECK(n != nullptr && n->name == "abc");
    CHECK(cursiv::ast_cast<Wrap>(n) == nullptr);
  }
  {
    cursiv::ParseResult r = engine().parse("$region abc * *");
    CHECK(r.accepted && static_cast<bool>(r));
    Node* const outer = r.value_as<Node>();
    CHECK(outer != nullptr);
    CHECK(cursiv::ast_cast<Wrap>(outer) != nullptr);
    Node* const mid = static_cast<Wrap*>(outer)->inner;
    CHECK(mid != nullptr && cursiv::ast_cast<Wrap>(mid) != nullptr);
    Node* const inner = static_cast<Wrap*>(mid)->inner;
    CHECK(inner != nullptr && inner->name == "abc");
    CHECK(cursiv::ast_cast<Wrap>(inner) == nullptr);
    CHECK(inner->parent == mid && mid->parent == outer);
    CHECK(r.diagnostics.empty());
  }
}

}  // namespace region

// A user enum is a field type as is. With a converter answering the enum it
// is valid, and a plain text payload is also valid: reflection matches the
// enumerator names with no converter at all.
namespace enm {

using namespace tab::enm;

// The reflective default: a plain text rule writes the enum with no converter
// at all.
static_assert(cursiv::detail::has_default_convert<Vis>);

void an_enum_is_a_plain_value_member() {
  const cursiv::DefaultAstReflection& r = refl();
  // No storage, no node, no reference.
  CHECK(desc_of(r, kTag<Decl>, "vis").content ==
        cursiv::detail::FieldContent::value);
  // It converts through its own entry: (VIS's converter, Vis) is not
  // (default path, string_view), and the converter-less spelling of the same
  // member is one entry more -- (default path, Vis).
  CHECK(cursiv::engine::ast_registry<tab::enm::G,
                                     cursiv::engine::LrLowering<tab::enm::G>>()
            .convCount == 3);
}

}  // namespace enm

namespace bare {

using namespace tab::bare;

void no_directive_no_columns() {
  const cursiv::DefaultAstReflection& r =
      reflection_of<&tab::bare::generated::G::create_ast_reflection>();
  CHECK(r.metaData().fieldCount == 0);
  CHECK(r.metaData().typeCount == 1);
  CHECK(cursiv::engine::ast_registry<tab::bare::G,
                                     cursiv::engine::LrLowering<tab::bare::G>>()
            .convCount == 0);
  CHECK(r.typeIndexOf(kTag<Node>) != cursiv::kNoType);
}

}  // namespace bare

// The reducer, end to end.

namespace wiring {

using namespace tab::wiring;

// Collapsing a rule must keep the creation its parent appends.
void builds_doc() {
  cursiv::ParseResult r = engine().parse("doc item a item b");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* doc = r.value_as<Doc>();
  CHECK(doc && doc->items.size() == 2);
  CHECK(doc->items[0]->name == "a" && doc->items[1]->name == "b");
  CHECK(doc->items[0]->parent == doc && doc->items[1]->parent == doc);
  CHECK(doc->parent == nullptr);
  CHECK(r.cst().text(doc->items[0]->cstNodeId) == "item a");
  CHECK(r.diagnostics.empty() && r.error == cursiv::AstBuildError::none);
  // The tag is written at construction, so the dynamic question answers
  // without the grammar.
  CHECK(cursiv::ast_cast<Node>(doc->items[0]) != nullptr);
  CHECK(doc->type->name == "Doc");
  // A node keeps an ID, never a tree pointer: the tree it indexes is the
  // result's, and the reader is the one holding it.
  CHECK(doc->cstNodeId == r.cst().entry_root());

  // One entry per node created, sorted by the node it was built from.
  CHECK(r.nodes().size() == 3);
  bool sorted = true;
  for (std::size_t i = 1; i < r.nodes().size(); ++i)
    if (r.nodes()[i - 1]->cstNodeId >= r.nodes()[i]->cstNodeId) sorted = false;
  CHECK(sorted);
  // The map enumerates every node the builder created, and the entry rule
  // closes last, so its id is the largest.
  CHECK(!r.nodes().empty() && r.nodes().back() == doc);
}

void structural_errors() {
  // A tree that never parsed: nothing to materialise.
  cursiv::Cst empty;
  cursiv::ParseResult none =
      cursiv::engine::AstReducer::build<tab::generated::All::kTable.stamp>(
          tab::generated::All::kAstActions, std::move(empty));
  CHECK(!none && none.error == cursiv::AstBuildError::no_entry);
  CHECK(none.value == nullptr);

  // Trailing garbage does not reject: the recovery reads past it and the
  // verdict lives in `recovered()` -- the AST is built anyway, since
  // completion works on broken documents.
  cursiv::ParseResult tail = engine().parse("doc item a %%%");
  CHECK(tail.cst().has_entry() && tail.cst().accepted() &&
        tail.cst().recovered());
  auto* doc = tail.value_as<Doc>();
  CHECK(doc != nullptr && doc->items.size() == 1);
}

// A hand-built tape's entry may name nothing, answered without a crash: (1) a
// root past the end of the tape, (2) a root that reduces to nothing, (3) an
// extent out of bounds.
void a_hand_built_entry_is_answered_not_trusted() {
  const cursiv::engine::AstActions& acts = tab::generated::All::kAstActions;
  // (1) Out of range. The public wrapper refuses to bind a root that names no
  // record, so the tape comes back with no entry at all rather than with a
  // wild one.
  {
    cursiv::Cst cst;
    cst.reset("doc item a");
    {
      cursiv::CstBuilder b(cst);
      b.leaf(0, 3, 0);
      b.bind_entry(0, cst.size(), 5000000);
    }
    CHECK(!cst.has_entry());
    cursiv::ParseResult r =
        cursiv::engine::AstReducer::build<tab::generated::All::kTable.stamp>(
            acts, std::move(cst));
    CHECK(!r && r.error == cursiv::AstBuildError::no_entry);
    CHECK(r.value == nullptr);
  }
  // (2) In range and unreducible. A single hidden leaf as the root: the scan
  // skips it, nothing is pushed, and the result is the empty stack -- which is
  // `nullptr`, and `malformed_cst`, and not whatever the allocator left there.
  {
    cursiv::Cst cst;
    cst.reset("doc item a");
    {
      cursiv::CstBuilder b(cst);
      b.hidden(0, 3, 0);
      b.bind_entry(0, cst.size(), 0);
    }
    CHECK(cst.has_entry());
    cursiv::ParseResult r =
        cursiv::engine::AstReducer::build<tab::generated::All::kTable.stamp>(
            acts, std::move(cst));
    CHECK(!r && r.error == cursiv::AstBuildError::malformed_cst);
    CHECK(r.value == nullptr);
  }
  // (3) The root exists, but its extent underflows or reaches before the tape.
  for (const cursiv::NodeCount extent : {0u, 2u, 0xffffffffu}) {
    cursiv::Cst cst;
    cst.reset("doc item a");
    {
      cursiv::CstBuilder b(cst);
      b.stream().close(0, 3, extent, 0);
      b.bind_stream();
      b.bind_entry(0, cst.size(), 0);
    }
    CHECK(cst.has_entry());
    cursiv::ParseResult r =
        cursiv::engine::AstReducer::build<tab::generated::All::kTable.stamp>(
            acts, std::move(cst));
    CHECK(!r && r.error == cursiv::AstBuildError::malformed_cst);
    CHECK(r.value == nullptr);
  }
}

}  // namespace wiring

namespace vocab {

using namespace tab::vocab;


// The document that exercises every member form at once; the move tests
// reuse it, because a result carrying references is what a move must keep
// readable.
cursiv::ParseResult parse_full() {
  cursiv::ParseResult r = engine().parse(
      "target aa target bb ! level 7 small 5 tag 42 see aa maybe bb "
      "also aa also bb");
  CHECK(r.accepted);
  return r;
}

void whole_vocabulary() {
  cursiv::ParseResult r = parse_full();
  CHECK(static_cast<bool>(r) && r.diagnostics.empty());
  auto* rec = r.value_as<Rec>();
  CHECK(rec != nullptr);
  CHECK(rec->targets.size() == 2 && rec->targets[0]->name == "aa");
  CHECK(rec->targets[1]->parent == rec);
  CHECK(rec->flagged);  // ?= writes the fact
  CHECK(rec->level.has_value() && *rec->level == 7);
  CHECK(rec->small == 5);
  CHECK(rec->tag.index() == 0 && std::get<0>(rec->tag) == 42);

  const cursiv::DefaultAstReflection& tables = refl();
  const std::uint16_t one = field_of(tables, kTag<Rec>, "one");
  const std::uint16_t maybe = field_of(tables, kTag<Rec>, "maybe");
  const std::uint16_t also = field_of(tables, kTag<Rec>, "also");
  // One handle per written reference, in write order: `one`, `maybe`, then
  // the two of `also` at their positions in the vector.
  CHECK(r.references->handles.size() == 4);
  CHECK(r.references->handles[0].owner == rec &&
        r.references->handles[0].field == one &&
        r.references->handles[0].index == 0);
  CHECK(r.references->handles[1].field == maybe &&
        r.references->handles[1].index == 0);
  CHECK(r.references->handles[2].field == also &&
        r.references->handles[2].index == 0);
  CHECK(r.references->handles[3].field == also &&
        r.references->handles[3].index == 1);
  // The three storage forms of a reference, read back through the reflection.
  CHECK(desc_of(tables, kTag<Rec>, "one").storage ==
        cursiv::detail::FieldStorage::direct);
  CHECK(desc_of(tables, kTag<Rec>, "maybe").storage ==
        cursiv::detail::FieldStorage::optional);
  CHECK(desc_of(tables, kTag<Rec>, "also").storage ==
        cursiv::detail::FieldStorage::vector);

  // The name is a view, the source node's ID kept beside it.
  CHECK(rec->one.refText == "aa" &&
        r.cst().text(cursiv::detail::RefAccess::node_id(rec->one)) == "aa");
  // Reading resolves: the build sets up the reference but no linker, so a read
  // before a linker exists fails and caches the failure.
  CHECK(rec->one.resolve() == cursiv::RefState::ErrorNotFound &&
        rec->one.get() == nullptr);
  // The converted one carries its converter's answer, not the spelling --
  // bytes that live in the tree's own resource, nowhere in the document.
  CHECK(rec->maybe.has_value() && rec->maybe->refText == "BB");
  CHECK(rec->also.size() == 2 && rec->also[1].refText == "bb");
  // The handle is an index, so it survives the growth that relocated every
  // element of the vector.
  CHECK(&rec->also[r.references->handles[3].index] == &rec->also.back());
  CHECK(rec->also[r.references->handles[2].index].refText == "aa");
}

void minimal_document() {
  cursiv::ParseResult r = engine().parse("see aa");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* rec = r.value_as<Rec>();
  CHECK(rec != nullptr);
  CHECK(!rec->flagged);
  CHECK(!rec->level.has_value());  // an option not taken writes nothing
  CHECK(rec->targets.empty() && rec->also.empty());
  // An optional<reference<T>> not taken produces no handle.
  CHECK(!rec->maybe.has_value());
  CHECK(r.references->handles.size() == 1);
  CHECK(r.references->handles[0].field == field_of(refl(), kTag<Rec>, "one"));
}

void other_variant_branch() {
  cursiv::ParseResult r = engine().parse("tag abc see aa");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* rec = r.value_as<Rec>();
  CHECK(rec != nullptr && rec->tag.index() == 1);
  CHECK(std::get<1>(rec->tag) == "abc");
}

// Collect and continue: the converter refuses, the member keeps its default,
// the diagnostic is positioned, and everything after it is still built.
void conversion_failure_continues() {
  cursiv::ParseResult r = engine().parse("small 5000 see aa also bb");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* rec = r.value_as<Rec>();
  CHECK(rec != nullptr);
  CHECK(rec->small == 0);
  CHECK(r.diagnostics.size() == 1);
  CHECK(r.diagnostics[0].message == "small is at most 999");
  CHECK(r.cst().text().substr(r.diagnostics[0].begin,
                              r.diagnostics[0].end - r.diagnostics[0].begin) ==
        "5000");
  CHECK(rec->one.refText == "aa" && rec->also.size() == 1);
  CHECK(r.error == cursiv::AstBuildError::none);
}

void assigning_again_keeps_one_reference_site() {
  auto r = engine().parse("$rebind aa cc ee ff");
  auto* rec = r.value_as<Rec>();
  CHECK(r.accepted && rec && r.diagnostics.empty());
  if (!rec || !rec->maybe || rec->also.size() != 2) return;
  CHECK(rec->one.refText == "aa" && rec->maybe->refText == "cc");
  CHECK(r.references->handles.size() == 4);
  CHECK(cursiv::detail::RefAccess::handle(rec->one) == 0 &&
        cursiv::detail::RefAccess::handle(*rec->maybe) == 1);
  CHECK(cursiv::detail::RefAccess::handle(rec->also[0]) == 2 &&
        cursiv::detail::RefAccess::handle(rec->also[1]) == 3);
  CHECK(cursiv::detail::RefAccess::resolver(rec->one) == r.references.get() &&
        cursiv::detail::RefAccess::resolver(*rec->maybe) == r.references.get());

  // Both public visitors see the four surviving slots exactly once. Before
  // linking, forcing gives each slot the ordinary ErrorNotFound answer.
  r.references->force(r.references->handles, cursiv::kNoCancel);
  std::vector<cursiv::TextOffset> unresolved;
  std::vector<std::uint32_t> listed;
  r.references->visitUnresolved(
      r.cst(), [&](cursiv::RefState why, auto begin, auto,
                   const cursiv::ReferenceInfo& site) {
        CHECK(why == cursiv::RefState::ErrorNotFound);
        unresolved.push_back(begin);
        // A position exists for a list element only.
        CHECK(site.index.has_value() == (site.property == "also"));
        if (site.index) listed.push_back(*site.index);
      });
  CHECK(unresolved.size() == 4);
  std::sort(listed.begin(), listed.end());
  CHECK((listed == std::vector<std::uint32_t>{0, 1}));
  std::sort(unresolved.begin(), unresolved.end());
  CHECK(std::adjacent_find(unresolved.begin(), unresolved.end()) ==
        unresolved.end());

  Target target;
  for (auto* ref : {&rec->one, &*rec->maybe, &rec->also[0], &rec->also[1]}) {
    cursiv::detail::RefAccess::target(*ref) = &target;
    cursiv::detail::RefAccess::state(*ref) = cursiv::RefState::Resolved;
  }
  std::vector<cursiv::TextOffset> resolved;
  r.references->visitResolved(
      r.cst(), [&](auto begin, auto, const cursiv::AstNode& node, bool multi) {
        CHECK(&node == &target && !multi);
        resolved.push_back(begin);
      });
  std::sort(resolved.begin(), resolved.end());
  CHECK(resolved == unresolved);

  // A grammar writes a scalar once, so a second write is the builder's own to
  // keep whole: the site stays, the text follows it, and a refused conversion
  // leaves the value in place.
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &r.cst();
  ctx.refs = &r.references->handles;
  ctx.resolver = r.references.get();
  const cursiv::engine::Pending again{
      .root = cursiv::detail::RefAccess::node_id(rec->also[0]),
      .text = cursiv::detail::RefAccess::node_id(rec->also[0]),
      .kind = cursiv::ActKind::assign};
  const cursiv::engine::ConvSlot text{
      .fn = [](cursiv::engine::AstBuildCtx& c, cursiv::NodeId id, void* out) {
        *static_cast<cursiv::AstNode::string_view*>(out) = c.cst->text(id);
        return true;
      }};
  const cursiv::engine::ConvSlot refuses{.fn = [](cursiv::engine::AstBuildCtx&,
                                                  cursiv::NodeId,
                                                  void*) { return false; }};
  const auto one = field_of(refl(), kTag<Rec>, "one");
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::one>>(
      ctx, again, rec, &text, one, 0);
  CHECK(rec->one.refText == "ee" &&
        cursiv::detail::RefAccess::handle(rec->one) == 0);
  CHECK(r.references->handles.size() == 4);
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::one>>(
      ctx, again, rec, &refuses, one, 0);
  CHECK(rec->one.refText == "ee" &&
        cursiv::detail::RefAccess::handle(rec->one) == 0);
  CHECK(r.references->handles.size() == 4);
  // An optional keeps its site the same way.
  const auto held = field_of(refl(), kTag<Rec>, "maybe");
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::maybe>>(
      ctx, again, rec, &text, held, 0);
  CHECK(rec->maybe->refText == "ee" &&
        cursiv::detail::RefAccess::handle(*rec->maybe) == 1);
  CHECK(r.references->handles.size() == 4);
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::maybe>>(
      ctx, again, rec, &refuses, held, 0);
  CHECK(rec->maybe->refText == "ee" && r.references->handles.size() == 4);
}

// Each single-valued form that can hold a node.
struct NodeHolder : cursiv::Extends<cursiv::AstNode> {
  Target* direct = nullptr;
  optional<Target*> maybe;
  variant<Target*, int64> either;
  optional<variant<Target*, int64>> both;
};

// A write replaces the node a member held, and the node no longer has that
// parent; a refused conversion replaces nothing.
template <auto Member>
void a_write_replaces_the_node_of(auto hold) {
  using F = cursiv::detail::field_tag<Member>;
  NodeHolder holder;
  Target before;
  Target after;
  hold(holder, &before);
  before.parent = &holder;
  const cursiv::engine::ConvSlot refuses{.fn = [](cursiv::engine::AstBuildCtx&,
                                                  cursiv::NodeId,
                                                  void*) { return false; }};
  cursiv::engine::AstBuildCtx ctx;
  if constexpr (F::content == cursiv::detail::FieldContent::variant) {
    const cursiv::engine::Pending value{.kind = cursiv::ActKind::assign};
    cursiv::engine::write_into<F>(ctx, value, &holder, &refuses, 0, 0);
    CHECK(before.parent == &holder && !ctx.detached);
  }
  const cursiv::engine::Pending node{.kind = cursiv::ActKind::assign,
                                     .value = &after};
  cursiv::engine::write_into<F>(ctx, node, &holder, nullptr, 0, 0);
  CHECK(before.parent == nullptr && ctx.detached);
  CHECK(after.parent == &holder && ctx.attached == 1);
  if constexpr (F::content == cursiv::detail::FieldContent::variant) {
    // A value replaces it too.
    ctx.detached = false;
    const cursiv::engine::ConvSlot number{
        .fn = [](cursiv::engine::AstBuildCtx&, cursiv::NodeId, void* out) {
          cursiv::detail::place<std::int64_t>(
              *static_cast<typename F::member*>(out), std::int64_t{7});
          return true;
        }};
    const cursiv::engine::Pending value{.kind = cursiv::ActKind::assign};
    cursiv::engine::write_into<F>(ctx, value, &holder, &number, 0, 0);
    CHECK(after.parent == nullptr && ctx.detached);
  }
}

void a_write_replaces_the_node_a_member_held() {
  a_write_replaces_the_node_of<&NodeHolder::direct>(
      [](NodeHolder& h, Target* t) { h.direct = t; });
  a_write_replaces_the_node_of<&NodeHolder::maybe>(
      [](NodeHolder& h, Target* t) { h.maybe = t; });
  a_write_replaces_the_node_of<&NodeHolder::either>(
      [](NodeHolder& h, Target* t) { h.either = t; });
  a_write_replaces_the_node_of<&NodeHolder::both>(
      [](NodeHolder& h, Target* t) { h.both = t; });
}

void a_copied_reference_cannot_claim_another_slots_handle() {
  const std::string text = "$rebind aa cc ee ff";
  auto r = engine().parse(text);
  auto* rec = r.value_as<Rec>();
  CHECK(rec && rec->maybe && rec->also.size() == 2);
  if (!rec || !rec->maybe || rec->also.size() != 2) return;
  Rec copied;
  copied.one = rec->one;
  copied.maybe = rec->maybe;
  cursiv::detail::RefAccess::state(copied.one) = cursiv::RefState::Resolved;
  cursiv::detail::RefAccess::state(*copied.maybe) = cursiv::RefState::Resolved;
  cursiv::engine::AstBuildCtx ctx;
  ctx.cst = &r.cst();
  ctx.refs = &r.references->handles;
  ctx.resolver = r.references.get();
  const cursiv::engine::Pending p{
      .root = cursiv::detail::RefAccess::node_id(rec->also[0]),
      .text = cursiv::detail::RefAccess::node_id(rec->also[0]),
      .kind = cursiv::ActKind::assign};
  const cursiv::engine::ConvSlot conv{
      .fn = [](cursiv::engine::AstBuildCtx& c, cursiv::NodeId id, void* out) {
        *static_cast<cursiv::AstNode::string_view*>(out) = c.cst->text(id);
        return true;
      }};
  const auto one = field_of(refl(), kTag<Rec>, "one");
  const auto maybe = field_of(refl(), kTag<Rec>, "maybe");
  const auto first = r.references->handles.size();
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::one>>(
      ctx, p, &copied, &conv, one, 0);
  cursiv::engine::write_into<cursiv::detail::field_tag<&Rec::maybe>>(
      ctx, p, &copied, &conv, maybe, 0);
  CHECK(r.references->handles.size() == first + 2);
  CHECK(cursiv::detail::RefAccess::handle(copied.one) == first &&
        cursiv::detail::RefAccess::handle(*copied.maybe) == first + 1);
  CHECK(cursiv::detail::RefAccess::state(copied.one) ==
            cursiv::RefState::Unresolved &&
        cursiv::detail::RefAccess::state(*copied.maybe) ==
            cursiv::RefState::Unresolved);
  CHECK(copied.one.refText == "ee" && copied.maybe->refText == "ee");
  CHECK(rec->one.refText == "aa" && rec->maybe->refText == "cc");
  CHECK(r.references->handles[first].owner == &copied &&
        r.references->handles[first + 1].owner == &copied);
}

}  // namespace vocab

// A mark is the text of the run it wraps and a doc the prose of what it
// documents: a payload reads through either, and so does the spelling a
// literal into a member is checked by.
namespace wrapped {

struct Fence {};
using Plus = std::remove_cvref_t<decltype(cursiv::lit<"+">)>;
template <class E>
using payload = typename cursiv::detail::payload_of<E>::type;
template <class E>
using spelling = typename cursiv::detail::lit_payloads<E>::type;
static_assert(std::is_same_v<payload<cursiv::Mark<Fence, Plus>>, payload<Plus>>);
static_assert(
    std::is_same_v<spelling<cursiv::Mark<Fence, Plus>>, spelling<Plus>>);
static_assert(
    std::is_same_v<payload<cursiv::Doc<Plus, "plus">>, payload<Plus>>);
static_assert(
    std::is_same_v<spelling<cursiv::Doc<Plus, "plus">>, spelling<Plus>>);
static_assert(!std::is_same_v<spelling<Plus>, cursiv::TypeList<>>);

}  // namespace wrapped

// `variant<boolean, float64, string_view>`: a bool and a double convert to
// each other, so the alternative whose type is the converter's result is
// chosen first.
namespace exact {

using namespace tab::exact;

// The slot the builder emplaces is the exact alternative -- the same list the
// validation counted, so the two cannot disagree.
template <class R>
using slot_of =
    typename cursiv::engine::conv_alt<decltype(Val::v),
                                      cursiv::detail::text_payload<R>>::type;
static_assert(std::is_same_v<slot_of<G::BOOL>, bool>);
static_assert(std::is_same_v<slot_of<G::NUM>, double>);
static_assert(std::is_same_v<slot_of<G::STR>, std::string_view>);
// The positioned wrapper never reaches the comparison: PCT's value type is
// `double`, so it takes the same exact slot a bare double converter does.
static_assert(std::is_same_v<slot_of<G::PCT>, double>);

// A payload no alternative names exactly still goes through the scan: int8
// is constructible into `float64` and into nothing else here, so it resolves.
static_assert(std::is_same_v<typename cursiv::engine::conv_alt<
                                 std::variant<double, std::string_view>,
                                 cursiv::detail::text_payload<G::BOOL>>::type,
                             double>);

// Validation accepts all three, where the constructibility scan alone would
// have refused bool and double.
void every_alternative_is_kept() {
  const cursiv::DefaultAstReflection& r = refl();
  CHECK(desc_of(r, kTag<Val>, "v").content ==
        cursiv::detail::FieldContent::variant);
  CHECK(
      cursiv::engine::ast_registry<tab::exact::G,
                                   cursiv::engine::LrLowering<tab::exact::G>>()
          .convCount == 4);
}

void each_payload_goes_to_its_own_slot() {
  const cursiv::Parser& engine = ast_builder::engine();
  cursiv::ParseResult rb = engine.parse("$exact true");
  auto* b = rb.value_as<Val>();
  CHECK(rb.accepted);
  CHECK(b != nullptr && std::holds_alternative<bool>(b->v));
  CHECK(b != nullptr && std::get<bool>(b->v));

  cursiv::ParseResult rf = engine.parse("$exact false");
  auto* f = rf.value_as<Val>();
  CHECK(f != nullptr && std::holds_alternative<bool>(f->v));
  CHECK(f != nullptr && !std::get<bool>(f->v));

  cursiv::ParseResult rn = engine.parse("$exact 42");
  auto* n = rn.value_as<Val>();
  CHECK(n != nullptr && std::holds_alternative<double>(n->v));
  CHECK(n != nullptr && std::get<double>(n->v) == 42.0);

  cursiv::ParseResult rp = engine.parse("$exact 50%");
  auto* p = rp.value_as<Val>();
  CHECK(p != nullptr && std::holds_alternative<double>(p->v));
  CHECK(p != nullptr && std::get<double>(p->v) == 0.5);

  cursiv::ParseResult rs = engine.parse("$exact abc");
  auto* s = rs.value_as<Val>();
  CHECK(s != nullptr && std::holds_alternative<std::string_view>(s->v));
  CHECK(s != nullptr && std::get<std::string_view>(s->v) == "abc");
}

}  // namespace exact

// An annotated terminal with no converter: the writer emplaces the
// annotated type, as the exactness round counts it.
namespace annot {

using namespace tab::annot;

template <class R>
using slot_of =
    typename cursiv::engine::conv_alt<decltype(Val::v),
                                      cursiv::detail::text_payload<R>>::type;
static_assert(std::is_same_v<slot_of<G::NUM>, std::int64_t>);
static_assert(std::is_same_v<slot_of<G::WORD>, std::string_view>);

// The two rules keep two conversion entries: the annotated one may not share
// the bare-text one, or the writer emplaces the wrong slot.
void the_annotated_rule_keeps_its_own_entry() {
  CHECK(
      cursiv::engine::ast_registry<tab::annot::G,
                                   cursiv::engine::LrLowering<tab::annot::G>>()
          .convCount == 2);
}

void annotated_terminal_goes_to_its_annotated_slot() {
  const cursiv::Parser& engine = ast_builder::engine();
  cursiv::ParseResult rn = engine.parse("$annot 42");
  auto* const n = rn.value_as<Val>();
  CHECK(rn.error == cursiv::AstBuildError::none && rn.diagnostics.empty());
  CHECK(n != nullptr && std::holds_alternative<std::int64_t>(n->v));
  CHECK(n != nullptr && std::get<std::int64_t>(n->v) == 42);

  cursiv::ParseResult rw = engine.parse("$annot abc");
  auto* const w = rw.value_as<Val>();
  CHECK(w != nullptr && std::holds_alternative<std::string_view>(w->v));
  CHECK(w != nullptr && std::get<std::string_view>(w->v) == "abc");
}

}  // namespace annot

// The same check with no string_view alternative to fall into: the exactness
// round counts `int32`, so the writer may not take the candidate list's head
// (`double`) instead.
namespace annot2 {

using namespace tab::annot2;

static_assert(
    std::is_same_v<typename cursiv::engine::conv_alt<
                       decltype(Num::n),
                       cursiv::detail::text_payload<tab::All::Annot2INT>>::type,
                   std::int32_t>);

void the_validated_alternative_is_the_written_one() {
  cursiv::ParseResult r = engine().parse("$annot2 42");
  auto* const v = r.value_as<Num>();
  CHECK(r.error == cursiv::AstBuildError::none);
  CHECK(v != nullptr && std::holds_alternative<std::int32_t>(v->n));
  CHECK(v != nullptr && std::get<std::int32_t>(v->n) == 42);
}

}  // namespace annot2

// An override replacing only a body keeps the base's converter, as type
// deduction does.
namespace over {

using N = tdual::N;
using Base = tdual::G;
using DerivedHex =
    typename cursiv::ResolveOverride<tdual::D2>::template rule<Base::HEX>;

// The chain answers the base's converter, and with it the base's type.
static_assert(std::is_same_v<cursiv::detail::converter_rule_t<DerivedHex>,
                             cursiv::detail::conv_t<Base::HEX>>);
static_assert(cursiv::detail::text_value<DerivedHex>::converted);
// `std::size_t` and not `uint64_t`: the converter answers `t.size() * 1000`,
// so the rule's type follows the width of the target, not a spelling that only
// matches it on a 64-bit one.
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<DerivedHex>, std::size_t>);
// ...so the conversion entry keeps the rule: erasing it is only free for a
// converter-less rule answering string_view.
static_assert(!cursiv::detail::erased_src<
              cursiv::detail::text_payload<DerivedHex>>::erasable);
static_assert(
    std::is_same_v<
        cursiv::detail::erased_src_t<cursiv::detail::text_payload<DerivedHex>>,
        cursiv::detail::text_payload<DerivedHex>>);

void an_override_runs_the_converter_it_inherited(const cursiv::Parser& parser,
                                                 const char* engine) {
  cursiv::ParseResult r = parser.parse("0X1f");
  CHECK(r.error == cursiv::AstBuildError::none && r.diagnostics.empty());
  const N* n = r.value_as<N>();
  if (n == nullptr || n->n != 4000) {
    std::printf("FAIL %s: 0X1f converted to %llu\n", engine,
                n == nullptr ? 0ull : static_cast<unsigned long long>(n->n));
    ++cursiv::test::failures;
  }
}

// An override that names a target type replaces its base's converter: that
// type's own conversion reads the text.
using DecimalHex =
    typename cursiv::ResolveOverride<tdual::D2>::template rule<Base::DEC>;
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<DecimalHex>, std::uint64_t>);
static_assert(std::is_same_v<cursiv::detail::converter_rule_t<DecimalHex>,
                             cursiv::default_converter<std::uint64_t>>);

void an_override_naming_a_target_converts_to_it(const cursiv::Parser& parser,
                                               const char* engine) {
  cursiv::ParseResult r = parser.parse("dec 10");
  CHECK(r.error == cursiv::AstBuildError::none && r.diagnostics.empty());
  const N* n = r.value_as<N>();
  if (n == nullptr || n->n != 10) {
    std::printf("FAIL %s: 10 converted to %llu\n", engine,
                n == nullptr ? 0ull : static_cast<unsigned long long>(n->n));
    ++cursiv::test::failures;
  }
}

// An override that restates only the body keeps the converter of the rule it
// overrode -- the middle grammar's, not the base's.
using ChainHex =
    typename cursiv::ResolveOverride<tdual::D2>::template rule<Base::TEN>;
static_assert(
    std::is_same_v<cursiv::detail::rule_type_t<ChainHex>, std::uint32_t>);
static_assert(std::is_same_v<cursiv::detail::converter_rule_t<ChainHex>,
                             cursiv::detail::text_conv<tdual::D1::tens_conv>>);

void an_override_keeps_the_converter_it_replaced(const cursiv::Parser& parser,
                                                 const char* engine) {
  cursiv::ParseResult r = parser.parse("ten 0Y1f");
  CHECK(r.error == cursiv::AstBuildError::none && r.diagnostics.empty());
  const N* n = r.value_as<N>();
  if (n == nullptr || n->n != 40) {
    std::printf("FAIL %s: 0Y1f converted to %llu\n", engine,
                n == nullptr ? 0ull : static_cast<unsigned long long>(n->n));
    ++cursiv::test::failures;
  }
}

}  // namespace over

// A create keeps the writes made before it in the sequence: the node it makes
// holds them.
namespace precreate {

void a_create_keeps_the_writes_before_it(const cursiv::Parser& parser,
                                        const char* engine) {
  cursiv::ParseResult r = parser.parse("fn run ()");
  CHECK(r.error == cursiv::AstBuildError::none && r.diagnostics.empty());
  const auto* f = r.value_as<tdual::Function>();
  if (f == nullptr || f->name != "run") {
    std::printf("FAIL %s: the Function lost the name written before it\n",
                engine);
    ++cursiv::test::failures;
  }
}

}  // namespace precreate

// `admits` counts the alternatives that accept the payload, so
// `variant<A*, B*>` is not ambiguous; the field keeps every target.
namespace nodealt {

using namespace tab::nodealt;

// Both families are reachable from the member, in declaration order, and the
// position is the alternative to emplace.
void both_families_are_reachable() {
  const cursiv::DefaultAstReflection& r = refl();
  const cursiv::detail::FieldDesc& v = desc_of(r, kTag<H>, "v");
  CHECK(v.target_count == 2);
  CHECK(v.targets[0] == r.typeIndexOf(kTag<A>));
  CHECK(v.targets[1] == r.typeIndexOf(kTag<B>));
  CHECK(v.target == r.typeIndexOf(kTag<A>));  // the head, for a node member
}

void both_node_alternatives_build() {
  const cursiv::Parser& engine = ast_builder::engine();
  cursiv::ParseResult ra = engine.parse("@ax");
  auto* const ha = ra.value_as<H>();
  CHECK(ra.error == cursiv::AstBuildError::none);
  CHECK(ha != nullptr && std::holds_alternative<A*>(ha->v));
  CHECK(ha != nullptr && std::get<A*>(ha->v)->a == "ax");

  // The second alternative -- the one a head-only target cannot find.
  cursiv::ParseResult rb = engine.parse("#by");
  auto* const hb = rb.value_as<H>();
  CHECK(rb.error == cursiv::AstBuildError::none);
  CHECK(hb != nullptr && std::holds_alternative<B*>(hb->v));
  CHECK(hb != nullptr && std::get<B*>(hb->v)->b == "by");
  CHECK(hb != nullptr && std::get<B*>(hb->v)->parent == hb);
}

// A node held in a variant is a child the reflection visits.
void the_variant_child_is_visited() {
  const cursiv::Parser& engine = ast_builder::engine();
  cursiv::ParseResult ra = engine.parse("@ax");
  auto* const ha = ra.value_as<H>();
  CHECK(ha != nullptr);
  if (ha == nullptr) return;
  std::string seen;
  refl().visitMembers(
      *ha, [&](std::string_view name, std::uint32_t, cursiv::AstNode& c) {
        seen += name;
        seen += c.parent == ha ? "+" : "-";
      });
  CHECK(seen == "v+");
}

}  // namespace nodealt

namespace nest {

using namespace tab::nested;

// A walk reads what a node contains, a parent first, and skips what is pruned;
// a walk up finds the nearest node of a class, the node itself first.
void a_walk_reads_what_a_node_contains() {
  cursiv::ParseResult r = engine().parse("$nest pub a . b !!");
  N* const outer = r.value_as<N>();
  CHECK(outer != nullptr && outer->child != nullptr);
  if (outer == nullptr || outer->child == nullptr) return;
  std::vector<const cursiv::AstNode*> all;
  cursiv::visit_all_contents(refl(), *outer,
                             [&](cursiv::AstNode& node) { all.push_back(&node); });
  CHECK(all.size() >= 2);
  if (!all.empty()) CHECK(all.front() == outer->child);
  CHECK(std::ranges::find(all, outer) == all.end());
  std::size_t direct = 0, pruned = 0;
  cursiv::visit_contents(refl(), *outer, [&](cursiv::AstNode&) { ++direct; });
  cursiv::visit_all_contents(refl(), *outer, [&](cursiv::AstNode&) {
    ++pruned;
    return cursiv::AstWalk::prune;
  });
  CHECK(direct != 0 && pruned == direct && pruned < all.size());
  CHECK(cursiv::container_of_type<N>(all.back()) != nullptr);
  CHECK(cursiv::container_of_type<N>(outer) == outer);
  CHECK(cursiv::container_of_type<N>(nullptr) == nullptr);
}

void nest_chain() {
  cursiv::ParseResult r = engine().parse("$nest pub a . b !!");
  CHECK(r.accepted && static_cast<bool>(r));
  N* const outer = r.value_as<N>();
  CHECK(outer != nullptr);
  // Two nests, so a chain of two wrappers over the node the rule built.
  N* const middle = outer->child;
  CHECK(middle != nullptr);
  N* const inner = middle->child;
  CHECK(inner != nullptr && inner->child == nullptr);
  CHECK(inner->parent == middle && middle->parent == outer);
  CHECK(outer->parent == nullptr);
  // The directives wrote the node current at their point in source order --
  // the innermost one.
  CHECK(inner->pub && !middle->pub && !outer->pub);
  // A data rule's value is the join of its parts: the raw spelling would be
  // "a . b", trivia included.
  CHECK(inner->name == "a.b");
  CHECK(outer->name.empty() && middle->name.empty());
  // Three nodes -- the rule's own and the two wrappers -- and the map is
  // sorted although the wrappers were created out of scan order (their rule
  // closes after them).
  CHECK(r.nodes().size() == 3);
  bool sorted = true;
  for (std::size_t i = 1; i < r.nodes().size(); ++i)
    if (r.nodes()[i - 1]->cstNodeId >= r.nodes()[i]->cstNodeId) sorted = false;
  CHECK(sorted);
  // Each wrapper spans the node it wraps, which spans what the rule read
  // before the first nest.
  CHECK(r.cst().text(inner->cstNodeId) == "pub a . b");
  CHECK(r.cst().text(middle->cstNodeId) == "pub a . b !");
  // The outermost wrapper is the root, and the entry's record is its.
  CHECK(r.cst().text(outer->cstNodeId) == "$nest pub a . b !!");
}

}  // namespace nest

// After a wrap the reducer holds the wrapper, of another class than the
// rule's element declares: what follows asks the node its class.
namespace wrapper {

using namespace tab::wrapper;

// A list makes room for what its own node reads: the region goes on to the
// wrappers around it, and what they read is not this one's.
void a_wrapper_makes_room_for_its_own_list() {
  constexpr int kWrappers = 2000;
  std::string text = "$listed a";
  for (int i = 0; i < kWrappers; ++i) text += " ! b c";
  const cursiv::ParseResult r = engine().parse(text);
  CHECK(r.accepted && static_cast<bool>(r));
  int wrappers = 0;
  for (const Listed* at = r.value_as<Listed>(); at != nullptr;
       at = cursiv::ast_cast<Listed>(at->child)) {
    ++wrappers;
    CHECK(at->items.size() == 2 && at->items.capacity() <= 4);
  }
  CHECK(wrappers == kWrappers);
}

void wrapper_owns_what_follows_the_nest() {
  cursiv::ParseResult r = engine().parse("$wrapper a ! ?");
  CHECK(r.accepted && static_cast<bool>(r));
  Wrap* const w = r.value_as<Wrap>();
  CHECK(w != nullptr && cursiv::ast_cast<Wrap>(w) != nullptr);
  if (w == nullptr) return;
  // The flag reached the wrapper, and the rule's own node kept the name.
  CHECK(w->flag);
  CHECK(w->child != nullptr && w->child->name == "a");
  CHECK(cursiv::ast_cast<Wrap>(w->child) == nullptr);
  CHECK(r.diagnostics.empty());
  // The wrapper is the root, so the entry's record is its; the rule's own
  // node spans what the rule read before the nest.
  CHECK(r.cst().text(w->cstNodeId) == "$wrapper a ! ?");
  CHECK(r.cst().text(w->child->cstNodeId) == "a");
  CHECK(w->child->parent == w);
}

// A member of the rule's declared class written after the nest reaches the
// wrapper -- legal here since `Wrap` derives from `Inner`. Where the two are
// unrelated, the current-node timeline refuses the write at compile time.

// A parent adopts the result of a nesting rule. The rule's element says
// `Inner`, the value is a `Wrap` -- here `Wrap` is an `Inner`, so the adoption
// is legal and the pointer is sound.
void adopts_the_wrapper_as_itself() {
  cursiv::ParseResult r = engine().parse("$adopt a ! b !");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* doc = r.value_as<tab::adoptwrap::Doc>();
  CHECK(doc != nullptr && doc->items.size() == 2);
  // What went into the vector is the wrapper, and it answers as one.
  CHECK(cursiv::ast_cast<Wrap>(doc->items[0]) != nullptr);
  CHECK(static_cast<Wrap*>(doc->items[0])->child->name == "a");
  CHECK(doc->items[1]->parent == doc);
}

}  // namespace wrapper

namespace move {

using namespace tab::move;

cursiv::ParseResult parse(std::string_view text) {
  cursiv::ParseResult r = engine().parse(text);
  CHECK(r.accepted);
  return r;
}

// The tree lives in a heap cell the result owns, so a move leaves it where it
// was -- and the IDs a node and a reference carry keep denoting the same
// records, because the tape they index moved with it.
void move_keeps_the_views_valid() {
  cursiv::ParseResult r = vocab::parse_full();
  const cursiv::Cst* const before = &r.cst();
  auto* const rec = r.value_as<tab::vocab::Rec>();
  const cursiv::NodeId root = rec->cstNodeId;

  std::vector<cursiv::ParseResult> box;
  box.push_back(std::move(r));

  CHECK(&box[0].cst() == before);
  CHECK(box[0].value == rec);
  CHECK(box[0].value->cstNodeId == root);
  // The IDs still denote the same records in the moved-to result's tree.
  CHECK(box[0].cst().text(root).starts_with("target aa"));
  CHECK(box[0].cst().text(cursiv::detail::RefAccess::node_id(rec->one)) ==
        "aa");  // a reference's source
  CHECK(box[0].references->handles[0].owner == rec);
  CHECK(static_cast<bool>(box[0]));
}

// Move-assignment over a live result: the tree, its pool and the map on it
// all change hands, and the new value answers from the new tree.
void move_assignment_takes_the_new_tree() {
  cursiv::ParseResult a = parse("x aa");
  cursiv::ParseResult b = parse("x bb");
  CHECK(a.value_as<Node>()->name == "aa");
  a = std::move(b);
  CHECK(a.value_as<Node>()->name == "bb");
  CHECK(a.nodes().size() == 1 && a.nodes()[0] == a.value);
}

void self_assignment_is_a_no_op() {
  cursiv::ParseResult a = parse("x aa");
  cursiv::ParseResult& self = a;
  a = std::move(self);
  CHECK(a.value_as<Node>()->name == "aa");
}

}  // namespace move

// An infix record has three visible children in order: left, operator,
// right. Precedence shows as nesting; a lone primary commits no infix
// record.
namespace climb {

using namespace tab::climb;

std::int32_t leaf_value(Expr* e) {
  const Num* const n = cursiv::ast_cast<Num>(e);
  return n != nullptr ? n->value : -1;
}

void infix_tree() {
  cursiv::ParseResult r = engine().parse("$climb 1+2*3");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* root = r.value_as<Bin>();
  CHECK(root != nullptr && cursiv::ast_cast<Bin>(root) != nullptr);
  CHECK(root->op == "+");
  CHECK(leaf_value(root->left) == 1);
  CHECK(cursiv::ast_cast<Bin>(root->right) != nullptr);
  auto* rhs = static_cast<Bin*>(root->right);
  CHECK(rhs->op == "*" && leaf_value(rhs->left) == 2 &&
        leaf_value(rhs->right) == 3);
  // The operands are the climb's children, so the parents are the climb's.
  CHECK(root->left->parent == root && rhs->parent == root);
  CHECK(rhs->left->parent == rhs);
  CHECK(root->parent == nullptr);
  CHECK(r.nodes().size() == 5);  // three primaries, two operators
}

void infix_left_nesting_and_trivia() {
  // Spaces put hidden children between the three semantic ones.
  cursiv::ParseResult r = engine().parse("$climb 1 * 2 + 3");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* root = r.value_as<Bin>();
  CHECK(root != nullptr && root->op == "+");
  CHECK(cursiv::ast_cast<Bin>(root->left) != nullptr &&
        leaf_value(root->right) == 3);
  auto* lhs = static_cast<Bin*>(root->left);
  CHECK(lhs->op == "*" && leaf_value(lhs->left) == 1 &&
        leaf_value(lhs->right) == 2);
}

void infix_single_primary() {
  cursiv::ParseResult r = engine().parse("$climb 7");
  CHECK(r.accepted && static_cast<bool>(r));
  // No operator, no climb record: the entry's result is the primary.
  CHECK(cursiv::ast_cast<Num>(r.value) != nullptr);
  CHECK(cursiv::ast_cast<Bin>(r.value) == nullptr);
  CHECK(r.value_as<Num>()->value == 7);
}

}  // namespace climb

// The factorer lifts a directive shared by a body-root choice of creates:
// the branch's node then takes every effect of the body.
namespace hoist {

using namespace tab::hoist;

void a_hoisted_write_reaches_the_branch() {
  cursiv::ParseResult r = engine().parse("$hoist x[ y]");
  CHECK(r.accepted && static_cast<bool>(r) &&
        r.error == cursiv::AstBuildError::none);
  CHECK(r.diagnostics.empty());
  auto* const bag = r.value_as<Bag>();
  CHECK(bag && bag->items.size() == 2);
  if (bag == nullptr || bag->items.size() != 2) return;
  // Each U adopted its branch's node -- no Base of its own was made...
  CHECK(bag->items[0]->type->name == "L");
  CHECK(bag->items[1]->type->name == "V");
  // ...and the write outside the regions went to the adopted node.
  CHECK(bag->items[0]->name == "x");
  CHECK(bag->items[1]->name == "y");
}

}  // namespace hoist

// A rule adopting a node built by a create<> region that a directive one
// level up holds, and the variant where that region is the whole body: both
// must build the same AST. The shape, not the level, is what decides.
namespace held {

using namespace tab::held;

void one(const cursiv::Parser& engine, std::string_view src,
         std::string_view want) {
  cursiv::ParseResult r = engine.parse(src);
  CHECK(r.accepted && static_cast<bool>(r));
  auto* const doc = r.value_as<Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  // A real node -- 0x1 is what a fuse over a lost creation leaves behind.
  CHECK(doc->held != nullptr && doc->held != reinterpret_cast<Base*>(1));
  if (doc->held == nullptr || doc->held == reinterpret_cast<Base*>(1)) return;
  CHECK(doc->held->name == want);
  CHECK(doc->held->type->name == "L");
  CHECK(doc->held->parent == doc);
  CHECK(r.diagnostics.empty());
}

void the_collapse_repro_builds_a_correct_ast() {
  one(engine(), "doc let a end", "a");
  one(engine(), "let a", "a");
}

}  // namespace held

// Valid literal payloads still convert for bool, numeric and enum members.
namespace spell {

using namespace tab::spell;

Flag* built(std::string_view text, cursiv::ParseResult& out) {
  out = engine().parse(text);
  if (!out.accepted) return nullptr;
  return out ? out.value_as<Flag>() : nullptr;
}

void a_spelling_the_member_reads_still_converts() {
  cursiv::ParseResult r1;
  const Flag* const t = built("true 42 slow", r1);
  CHECK(t != nullptr);
  if (t != nullptr) {
    CHECK(t->on == true);
    CHECK(t->count == 42);
    CHECK(t->mode == Mode::slow);
  }
  cursiv::ParseResult r2;
  const Flag* const f = built("false 42 fast", r2);
  CHECK(f != nullptr);
  if (f != nullptr) {
    CHECK(f->on == false);
    CHECK(f->mode == Mode::fast);
  }
  // Not one diagnostic: a spelling that matches an enumerator exactly is what
  // the default converter reads, so nothing is collected at build time.
  CHECK(r1.diagnostics.empty() && r2.diagnostics.empty());
}

}  // namespace spell

// A text member is a `string_view`, written from the matched text or from
// a converter answering a view. Refused: a converter answering a string, and
// `as<T>` with a type that is not text.
namespace textlaw {

using namespace tab::textlaw;

// The rule itself, on every shape at once.
template <class M, class R>
inline constexpr bool takes =
    cursiv::engine::admits<M, cursiv::detail::text_payload<R>>();
static_assert(takes<std::string_view, tab::All::TextID>);
static_assert(takes<std::string_view, tab::All::TextQUOTED>);
static_assert(!takes<std::string_view, tab::All::TextNUM>);
static_assert(!takes<std::string, tab::All::TextNUM>);
static_assert(takes<std::int64_t, tab::All::TextNUM>);
// A converter that builds its text answers a string, which a view cannot hold.
static_assert(!takes<std::string_view, tab::All::TextLOWER>);
static_assert(!takes<std::string_view, tab::All::TextOWNS>);
// A `string` takes any text.
static_assert(takes<std::string, tab::All::TextOWNS>);
static_assert(takes<std::string, tab::All::TextQUOTED>);
static_assert(takes<std::string, tab::All::TextID>);

void the_law_leaves_the_safe_combinations_alone() {
  cursiv::ParseResult r = engine().parse("$text abc 'xy' 42 HELLO");
  CHECK(r.accepted && static_cast<bool>(r) && r.diagnostics.empty());
  const N* const n = r.value_as<N>();
  CHECK(n != nullptr);
  if (n == nullptr) return;
  CHECK(n->slice == "abc");
  CHECK(n->viewed == "xy");
  CHECK(n->annotated == 42);
  CHECK(n->built == "hello");
  // A `string_view` member holds a slice of the document; only a converter that
  // builds its text needs a `string`, which owns its bytes.
  const std::string_view src = r.cst().text(r.cst().entry_root());
  const auto inside = [src](std::string_view v) {
    return v.data() >= src.data() &&
           v.data() + v.size() <= src.data() + src.size();
  };
  CHECK(inside(n->slice) && inside(n->viewed));
}

}  // namespace textlaw

// `default_converter<T>` carrying a `convert` is the conversion, so every
// rule naming T reaches it without repeating a converter function.
namespace own {

using namespace tab::own;

void a_specialised_converter_reaches_its_own_target() {
  cursiv::ParseResult r = engine().parse("$own 42");
  auto* n = r.value_as<Node>();
  CHECK(n != nullptr && n->share.v == 42);
  CHECK(r.diagnostics.empty());
}

void its_refusal_is_a_diagnostic_and_not_a_crash() {
  cursiv::ParseResult r = engine().parse("$own 240");
  auto* n = r.value_as<Node>();
  CHECK(n != nullptr && n->share.v == 0);
  CHECK(r.diagnostics.size() == 1 &&
        r.diagnostics[0].message.find("over 100") != std::string::npos);
}

}  // namespace own

namespace optnode {

using namespace tab::optnode;

std::string walk(const cursiv::AstNode& n) {
  std::string out;
  refl().visitMembers(
      n, [&](std::string_view name, std::uint32_t, cursiv::AstNode&) {
        out += name;
        out += ';';
      });
  return out;
}

void the_reflection_walks_the_optional_child() {
  cursiv::ParseResult r = engine().parse("$opt a b");
  CHECK(r.accepted);
  auto* bag = r.value_as<Bag>();
  CHECK(bag != nullptr && bag->tail.has_value());
  if (bag == nullptr) return;
  CHECK(walk(*bag) == "head;tail;");
}

void an_absent_one_yields_nothing() {
  cursiv::ParseResult r = engine().parse("$opt a");
  CHECK(r.accepted);
  auto* bag = r.value_as<Bag>();
  CHECK(bag != nullptr && !bag->tail.has_value());
  if (bag == nullptr) return;
  CHECK(walk(*bag) == "head;");
}

}  // namespace optnode

// `assign<&Num::value>(ref<terminal<as<f>(body)>>)` carries its conversion in
// the rule's own type, so splicing the reference cannot lose it.
namespace inl {

using namespace tab::inl;

void the_inline_slot_converts() {
  cursiv::ParseResult r = engine().parse("$inl ff 10");
  CHECK(r.accepted && !!r);
  auto* d = r.value_as<Doc>();
  CHECK(d != nullptr && d->nums.size() == 2);
  CHECK(d->nums[0]->value == 255.0 && d->nums[1]->value == 16.0);
}

}  // namespace inl

// The member accepts the whole family; this site accepts `Kind1` alone, and
// the site's own filter is what makes a resolution honour it.
namespace narrow {

using namespace tab::narrow;

void a_narrowed_site_records_and_filters() {
  cursiv::ParseResult r = engine().parse("use abc");
  CHECK(r.accepted);
  auto* const site = static_cast<Site*>(r.value);
  CHECK(site != nullptr);
  if (site == nullptr) return;
  CHECK(cursiv::detail::RefAccess::narrow(site->broad) != cursiv::kNoNarrow);

  CHECK(r.references->handles.size() == 1);
  const std::uint16_t at = r.references->handles[0].field;
  const cursiv::DefaultAstReflection& refl = ast_builder::refl();
  const cursiv::RefOps& ops = refl.metaData().refOps[at];
  CHECK(ops.narrow_count == 1);
  CHECK(ops.narrow_of(*site, 0) ==
        cursiv::detail::RefAccess::narrow(site->broad));

  // The member's own row accepts both kinds...
  CHECK(ops.branch(kTag<Kind1>) != cursiv::kNoBranch);
  CHECK(ops.branch(kTag<Kind2>) != cursiv::kNoBranch);
  // ...the site's filter accepts one.
  const cursiv::detail::RefFilter narrowed{
      &ops, cursiv::detail::RefAccess::narrow(site->broad)};
  CHECK(narrowed.branch(kTag<Kind1>) != cursiv::kNoBranch);
  CHECK(narrowed.branch(kTag<Kind2>) == cursiv::kNoBranch);
}

}  // namespace narrow

// `a + b + (d | e) + c`, where d and e create their own class: the writes
// go to the branch's node, and the rule's type is their common base.
namespace deferred {

using namespace tab::deferred;

void writes_go_to_the_branch_node() {
  cursiv::ParseResult r = engine().parse("pets rex #fast dog ! @big tom cat");
  CHECK(r.accepted && static_cast<bool>(r));
  auto* doc = r.value_as<Doc>();
  CHECK(doc && doc->pets.size() == 2);
  // The branch decided the class; the upstream name, the upstream tag and the
  // downstream tag all reached the node it created.
  CHECK(cursiv::ast_cast<Dog>(doc->pets[0]) != nullptr);
  CHECK(doc->pets[0]->name == "rex");
  CHECK(doc->pets[0]->tags.size() == 2);
  CHECK(doc->pets[0]->tags[0] == "fast" && doc->pets[0]->tags[1] == "big");
  CHECK(static_cast<Dog*>(doc->pets[0])->good);
  CHECK(cursiv::ast_cast<Cat>(doc->pets[1]) != nullptr);
  CHECK(doc->pets[1]->name == "tom" && doc->pets[1]->tags.empty());
  CHECK(r.diagnostics.empty() && r.error == cursiv::AstBuildError::none);
}

}  // namespace deferred

}  // namespace

void run() {
  // The tables the generator wrote, read back through the reflection.
  hier::the_tables_read_back();
  infix::the_climb_contributes_its_members();
  region::the_nest_names_a_node_member();
  enm::an_enum_is_a_plain_value_member();
  bare::no_directive_no_columns();
  exact::every_alternative_is_kept();
  annot::the_annotated_rule_keeps_its_own_entry();
  nodealt::both_families_are_reachable();

  // ...and the reducer that reads them.
  wiring::builds_doc();
  narrow::a_narrowed_site_records_and_filters();
  wiring::structural_errors();
  wiring::a_hand_built_entry_is_answered_not_trusted();
  vocab::whole_vocabulary();
  vocab::minimal_document();
  vocab::other_variant_branch();
  vocab::conversion_failure_continues();
  vocab::assigning_again_keeps_one_reference_site();
  vocab::a_write_replaces_the_node_a_member_held();
  vocab::a_copied_reference_cannot_claim_another_slots_handle();
  exact::each_payload_goes_to_its_own_slot();
  annot::annotated_terminal_goes_to_its_annotated_slot();
  annot2::the_validated_alternative_is_the_written_one();
  for (const auto& [name, parser] : {
           std::pair{"LR", &parser_of<&tdual::generated::Lr::create_parser>()},
           std::pair{"LL", &parser_of<&tdual::generated::Ll::create_parser>()}}) {
    over::an_override_runs_the_converter_it_inherited(*parser, name);
    over::an_override_naming_a_target_converts_to_it(*parser, name);
    over::an_override_keeps_the_converter_it_replaced(*parser, name);
    precreate::a_create_keeps_the_writes_before_it(*parser, name);
  }
  nodealt::both_node_alternatives_build();
  nodealt::the_variant_child_is_visited();
  region::adopts_and_wraps();
  nest::nest_chain();
  nest::a_walk_reads_what_a_node_contains();
  wrapper::wrapper_owns_what_follows_the_nest();
  wrapper::a_wrapper_makes_room_for_its_own_list();
  wrapper::adopts_the_wrapper_as_itself();
  move::move_keeps_the_views_valid();
  move::move_assignment_takes_the_new_tree();
  move::self_assignment_is_a_no_op();
  climb::infix_tree();
  climb::infix_left_nesting_and_trivia();
  climb::infix_single_primary();
  climbs::two_climbs_over_one_class_keep_their_own_members();
  hoist::a_hoisted_write_reaches_the_branch();
  held::the_collapse_repro_builds_a_correct_ast();
  spell::a_spelling_the_member_reads_still_converts();
  textlaw::the_law_leaves_the_safe_combinations_alone();
  own::a_specialised_converter_reaches_its_own_target();
  own::its_refusal_is_a_diagnostic_and_not_a_crash();
  optnode::the_reflection_walks_the_optional_child();
  optnode::an_absent_one_yields_nothing();
  inl::the_inline_slot_converts();
  deferred::writes_go_to_the_branch_node();
}

}  // namespace ast_builder

namespace ast_node {

namespace {

struct N : cursiv::Extends<cursiv::AstNode> {  // the vocabulary is inherited:
                                               // bare names
  string_view name;
  vector<N*> children;
  optional<int32> level;
  variant<int64, string_view> tag;
  reference<N> target;
};
static_assert(std::is_base_of_v<cursiv::AstNode, N>);
// Checked on 64-bit targets only, where a pointer is eight bytes: the size
// catches a field added.
static_assert(sizeof(void*) != 8 || sizeof(cursiv::RefHandle) == 16);
static_assert(!cursiv::has_error(cursiv::RefState::Resolved) &&
              cursiv::has_error(cursiv::RefState::ErrorNotFound));

// A diagnostic positions by range alone: its reader holds the tree.
static_assert(std::is_same_v<decltype(cursiv::ParseDiagnostic::begin),
                             cursiv::TextOffset>);
static_assert(cursiv::ParseDiagnostic{}.begin == 0 &&
              cursiv::ParseDiagnostic{}.end == 0);
static_assert(cursiv::ParseDiagnostic{}.element == cursiv::kNoElement);

const cursiv::Parser& engine() {
  return support::parser_of<&tcstn::generated::LexG::create_parser>();
}

void cst_node_view_and_bind_round_trip() {
  const cursiv::ParseResult parsed = engine().parse("abc");
  CHECK(parsed.accepted);
  const cursiv::Cst& cst = parsed.cst();
  const cursiv::CstNodeView v{&cst, cst.entry_root()};
  CHECK(static_cast<bool>(v));
  CHECK(v.text() == "abc");
  CHECK(v.begin() == 0 && v.end() == 3);
  CHECK(!cursiv::CstNodeView{});

  // bind round-trip: the name is a view the writer hands over, and the
  // position is an ID the caller resolves against the tree it already has.
  cursiv::reference<N> r;
  cursiv::detail::RefAccess::bind(r, cst.entry_root(), "a.b");
  CHECK(r.refText == "a.b" &&
        cst.text(cursiv::detail::RefAccess::node_id(r)) == "abc" &&
        r.resolve() == cursiv::RefState::Unresolved);
  const cursiv::reference<N> fresh;
  CHECK(cursiv::detail::RefAccess::node_id(fresh) == cursiv::kNoNode);
}

// A single reference owns nothing: it is trivially destructible.
static_assert(std::is_trivially_destructible_v<cursiv::reference<N>>);
static_assert(std::is_default_constructible_v<cursiv::reference<N>>);
static_assert(std::is_default_constructible_v<cursiv::multi_reference<N>>);

// multi_reference reads like a reference. The builder does not fill `items`
// -- resolution is the linker's -- so this checks what a consumer sees once
// the overload set is filled.
void multi_reference_reads() {
  cursiv::multi_reference<N> m;
  CHECK(m.empty() && m.size() == 0);
  CHECK(m.get(0) == nullptr);  // unresolved and out of range
  N a, b;
  const cursiv::MultiRefItem targets[2] = {{&a, 0}, {&b, 0}};
  cursiv::detail::RefAccess::items(m) =
      cursiv::MultiRefItems{targets[0], targets[1]};
  // A detached reference has no resolver or published result. Its manually
  // filled items become readable only once its state publishes that result.
  CHECK(m.size() == 0 && m.empty());
  CHECK(m.get(0) == nullptr);  // still unresolved -> nothing
  cursiv::detail::RefAccess::state(m) = cursiv::RefState::Resolved;
  CHECK(m.size() == 2 && !m.empty());
  CHECK(m.get(0) == &a && m.get(1) == &b);
  CHECK(m.get(2) == nullptr);  // out of range -> null, never UB
  // One candidate class: `get(i)` needs no template argument, and the typed
  // form answers the same thing.
  CHECK(m.get<N>(0) == &a);
}

// The rank `get<T>()` compares is the one the linker writes: the first
// candidate T matches. A pack that names one class twice is the case where
// first and last differ, and the last is a rank no linker ever writes.
void the_branch_of_a_candidate_is_its_first_rank() {
  cursiv::reference<N, N> r;
  N a;
  cursiv::detail::RefAccess::target(r) = &a;
  cursiv::detail::RefAccess::which(r) = 0;
  cursiv::detail::RefAccess::state(r) = cursiv::RefState::Resolved;
  CHECK(r.get<N>() == &a);
}

void reference_defaults() {
  N n;
  CHECK(n.target.resolve() == cursiv::RefState::Unresolved);
  CHECK(n.target.get<N>() == nullptr);  // unresolved -> null, never UB
  CHECK(n.target.get() == nullptr);     // no template argument needed
  CHECK(n.parent == nullptr &&
        n.cstNodeId == cursiv::kNoNode);  // built by hand
  CHECK(n.target.refText.empty());
}

struct D : cursiv::Extends<cursiv::AstNode> {
  int payload = 0;
};

// Counts the blocks the arena asks for, to check that its nodes share chunks.
// It only grows: the arena never gives a chunk back.
struct counting_resource : std::pmr::memory_resource {
  int blocks = 0;
  std::pmr::monotonic_buffer_resource owned;
  void* do_allocate(std::size_t n, std::size_t a) override {
    ++blocks;
    return owned.allocate(n, a);
  }
  void do_deallocate(void* p, std::size_t n, std::size_t a) override {
    owned.deallocate(p, n, a);
  }
  bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override {
    return this == &o;
  }
};

// `release()` frees the arena's blocks. Checked by what the arena asks for,
// since a reclaimed pool gives the same bytes back.
void arena_lifetimes() {
  counting_resource pool;
  cursiv::detail::AstArena a(&pool);
  D* d = a.make<D>();
  d->payload = 7;
  CHECK(d->parent == nullptr);
  const int first = pool.blocks;
  CHECK(first == 1);  // one chunk allocated, not one block per node
  (void)a.make<D>();
  CHECK(pool.blocks == first);  // the second node goes in that chunk
  a.release();
  (void)a.make<D>();
  CHECK(pool.blocks == first + 1);
}

struct A2 : cursiv::Extends<cursiv::AstNode> {};
struct B2 : cursiv::Extends<cursiv::AstNode> {};
struct C2 : cursiv::Extends<A2> {};
struct D2 : cursiv::Extends<C2> {};

// The closure is transitive and names every base once: three levels down,
// `derivesFrom` still answers in one walk of one array.
void the_closure_is_transitive() {
  const cursiv::AstTypeTag& d = cursiv::ast_type_tag<D2>;
  CHECK(d.derivesFrom(&cursiv::ast_type_tag<C2>));
  CHECK(d.derivesFrom(&cursiv::ast_type_tag<A2>));
  CHECK(d.derivesFrom(&cursiv::ast_type_tag<cursiv::AstNode>));
  CHECK(!d.derivesFrom(&cursiv::ast_type_tag<B2>));
  CHECK(d.baseCount == 3);
  for (std::uint32_t i = 0; i < d.baseCount; ++i)
    for (std::uint32_t j = i + 1; j < d.baseCount; ++j)
      CHECK(d.bases[i] != d.bases[j]);
}

// `ast_cast` accepts the class and its subtypes, refuses the rest, and never
// dereferences what it refuses.
void ast_cast_accepts_the_family_and_refuses_the_rest() {
  D2 d;
  d.type = &cursiv::ast_type_tag<D2>;
  CHECK(cursiv::ast_cast<A2>(&d) == &d);
  CHECK(cursiv::ast_cast<C2>(&d) == &d);
  CHECK(cursiv::ast_cast<D2>(&d) == &d);
  CHECK(cursiv::ast_cast<const A2>(&d) == &d);
  CHECK(cursiv::ast_cast<B2>(&d) == nullptr);  // the other family

  A2 a;
  a.type = &cursiv::ast_type_tag<A2>;
  CHECK(cursiv::ast_cast<D2>(&a) == nullptr);  // a base is not its subtype
  CHECK(cursiv::ast_cast<A2>(static_cast<cursiv::AstNode*>(nullptr)) ==
        nullptr);
  cursiv::AstNode untagged;
  CHECK(cursiv::ast_cast<A2>(&untagged) == nullptr);  // no tag, no family
}

// A reference with two candidate classes: `get<T>()` answers null on the
// branch the linker did not write, never a mis-cast pointer.
void a_two_candidate_reference_answers_by_branch() {
  cursiv::reference<A2, B2> r;
  A2 a;
  a.type = &cursiv::ast_type_tag<A2>;
  cursiv::detail::RefAccess::target(r) = &a;
  cursiv::detail::RefAccess::which(r) = 0;  // the linker decided: the A2 branch
  cursiv::detail::RefAccess::state(r) = cursiv::RefState::Resolved;
  CHECK(r.get<A2>() == &a);
  CHECK(r.get<B2>() == nullptr);
  B2 b;
  b.type = &cursiv::ast_type_tag<B2>;
  cursiv::detail::RefAccess::target(r) = &b;
  cursiv::detail::RefAccess::which(r) = 1;
  CHECK(r.get<B2>() == &b);
  CHECK(r.get<A2>() == nullptr);
}

// A copy takes the members' values and none of the source's identity; an
// assignment keeps the target's own.
void a_copy_takes_no_identity() {
  D source;
  source.payload = 7;
  source.type = &cursiv::ast_type_tag<D>;
  const D copy = source;
  CHECK(copy.payload == 7 && copy.type == nullptr);

  A2 holder;
  D2 d;
  d.type = &cursiv::ast_type_tag<D2>;
  d.parent = &holder;
  d.cstNodeId = 3;
  const C2 sliced = d;
  CHECK(sliced.type == nullptr && sliced.parent == nullptr &&
        sliced.cstNodeId == cursiv::kNoNode);
  CHECK(cursiv::ast_cast<D2>(&sliced) == nullptr);
  C2 other;
  other.type = &cursiv::ast_type_tag<C2>;
  static_cast<C2&>(d) = other;
  CHECK(d.type == &cursiv::ast_type_tag<D2> && d.parent == &holder &&
        d.cstNodeId == 3);
  CHECK(cursiv::ast_cast<D2>(&d) == &d);
}

}  // namespace

void run() {
  cst_node_view_and_bind_round_trip();
  multi_reference_reads();
  the_branch_of_a_candidate_is_its_first_rank();
  reference_defaults();
  arena_lifetimes();
  the_closure_is_transitive();
  ast_cast_accepts_the_family_and_refuses_the_rest();
  a_two_candidate_reference_answers_by_branch();
  a_copy_takes_no_identity();
}

}  // namespace ast_node

namespace ast_reflection {

namespace {
using namespace cursiv;
using namespace tdual;
struct Manual : Extends<Derived> {};

std::vector<std::string> children(const AstReflection& reflection,
                                  const AstNode& node) {
  std::vector<std::string> result;
  reflection.visitMembers(
      node, [&](std::string_view name, std::uint32_t index, AstNode& child) {
        CHECK(child.type == &ast_type_tag<Child>);
        result.push_back(std::string(name) + ':' + std::to_string(index));
      });
  return result;
}

void check(const Parser& parser, const AstReflection& reflection) {
  auto parsed =
      parser.parse("child a child b tail child c item child d target a");
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  auto* root = parsed.value_as<Derived>();
  CHECK(root != nullptr);
  if (root == nullptr) return;
  const auto seen = children(reflection, *root);
  CHECK(
      (seen == std::vector<std::string>{"head:4294967295", "choice:4294967295",
                                        "tail:4294967295", "items:0"}));
  const auto target =
      reflection.referenceIndexOf(&ast_type_tag<Base>, "target");
  CHECK(target != kNoReference);
  CHECK(reflection.referenceIndexOf(&ast_type_tag<Derived>, "target") ==
        target);
  CHECK(reflection.referenceIndexOf(&ast_type_tag<reference<Child>>) == target);
  CHECK(reflection.referenceIndexOf(&ast_type_tag<Derived>, "head") ==
        kNoReference);
  CHECK(reflection.referenceIndexOf(&ast_type_tag<Derived>, "missing") ==
        kNoReference);
  CHECK(reflection.hasMultiReference());
  const auto links =
      reflection.referenceIndexOf(&ast_type_tag<Derived>, "links");
  CHECK(links != kNoReference && reflection.isMultiReference(links));

  // Unknown derived tags retain their inherited children without duplicates.
  Manual manual;
  manual.type = &ast_type_tag<Manual>;
  manual.head = root->head;
  manual.choice = root->choice;
  manual.tail = root->tail;
  manual.items = root->items;
  CHECK(reflection.typeIndexOf(manual.type) == kNoType);
  CHECK(children(reflection, manual) == seen);
  AstNode unrelated;
  CHECK(children(reflection, unrelated).empty());
}
}  // namespace

void run() {
  check(*tdual::generated::Lr::create_parser(),
        *tdual::generated::Lr::create_ast_reflection());
  check(*tdual::generated::Ll::create_parser(),
        *tdual::generated::Ll::create_ast_reflection());
}

}  // namespace ast_reflection

// Default converters and Parts, end to end through a real parse.
namespace convert {

namespace {

using namespace tconv;

const cursiv::Parser& engine() {
  return support::parser_of<&tconv::generated::G::create_parser>();
}

void keyword_template_converters_compile_and_parse() {
  const auto ll = tdual::generated::Ll::create_parser();
  for (const auto* parser :
       std::array<const cursiv::Parser*, 2>{&engine(), ll.get()}) {
    const auto result = parser->parse(parser == ll.get() ? "$conv:keyword is"
                                                        : "keyword is");
    CHECK(result.accepted && !result.cst().recovered() &&
          result.diagnostics.empty());
    const auto* value = result.value_as<tconv::comp::N>();
    CHECK(value && value->name == "is");
  }
}

// A data rule with a separator -- `some(ref<IDT>, lit<".">)` matches one IDT,
// then repeats `lit<"."> + ref<IDT>` -- exactly the shape Parts is built for.
const cursiv::Parser& separated() {
  return support::parser_of<&tab::generated::All::create_parser>();
}

void defaults() {
  auto i = cursiv::default_converter<std::int32_t>::convert("123");
  CHECK(i && *i.value == 123);
  auto o = cursiv::default_converter<std::int8_t>::convert("999");  // overflow
  CHECK(!o && !o.error.message.empty());
  auto f = cursiv::default_converter<double>::convert("2.5");
  CHECK(f && *f.value == 2.5);
  auto b = cursiv::default_converter<bool>::convert("true");
  CHECK(b && *b.value);
  auto b2 = cursiv::default_converter<bool>::convert("false");
  CHECK(b2 && !*b2.value);
  auto upper = cursiv::default_converter<bool>::convert("TRUE");
  CHECK(upper && *upper.value);
  auto mixed = cursiv::default_converter<bool>::convert("FaLsE");
  CHECK(mixed && !*mixed.value);
  // Only true and false are read.
  auto bx = cursiv::default_converter<bool>::convert("nope");
  CHECK(!bx && !bx.error.message.empty());
  auto sv = cursiv::default_converter<std::string_view>::convert("xyz");
  CHECK(sv && *sv.value == "xyz");
  auto bad = cursiv::default_converter<std::int32_t>::convert("abc");
  CHECK(!bad && !bad.error.message.empty());
}

void text_helpers_keep_empty_values_present() {
  const cursiv::Document document(
      cursiv::TextDocument::create("file:///kept", "kept", 0, ""));
  const auto copied = document.keep_text(std::string_view{});
  CHECK(copied.empty() && copied.data() != nullptr);

  std::string text = "value";
  const auto owned = document.keep_text(text);
  text[0] = 'x';
  CHECK(owned == "value");

  // Parts whose sizes add up past what memory can hold are refused before a
  // byte of them is read.
  const std::string_view half(text.data(),
                              std::numeric_limits<std::size_t>::max() / 2 + 1);
  bool refused = false;
  try {
    (void)document.keep_text(half, half);
  } catch (const std::bad_alloc&) {
    refused = true;
  }
  CHECK(refused);
}

void converters_answer_classes_of_their_own() {
  const int before = values::Tracked::live;
  {
    const auto parsed = engine().parse(
        "values %1 %22 %333 ; maybe %4 either %5 ^6 ~abcd $7 $8 $9 #payload "
        "@1 , @2 then @3 <123> sets @5 @5 @6 maps @7 @7 @8");
    CHECK(parsed && parsed.diagnostics.empty());
    const auto* doc = parsed.value_as<values::Doc>();
    CHECK(doc != nullptr);
    if (doc != nullptr) {
      CHECK(doc->one.raw == "%1");
      CHECK(doc->many.size() == 2 && doc->many[1].raw == "%333" &&
            doc->many[1].parts[0] == 4);
      CHECK(doc->maybe.has_value() && doc->maybe->raw == "%4");
      CHECK(std::holds_alternative<values::Tracked>(doc->either) &&
            std::get<values::Tracked>(doc->either).raw == "%5");
      CHECK(doc->guarded.raw == "^6");
      CHECK(doc->id.bytes[1] == 'a');
      CHECK(doc->price.text() == "$7" && doc->prices.size() == 2 &&
            doc->prices[1].text() == "$9");
      CHECK(doc->payload != nullptr && doc->payload->bytes == "#payload");
      CHECK(doc->pieces.size() == 3 && doc->pieces[2] == "@2");
      CHECK(doc->forwarded.size() == 1 && doc->forwarded[0] == "@3");
      CHECK((doc->numbers == std::vector<int>{1, 2, 3}));
      CHECK(doc->tagged.size() == 2 && doc->tagged.contains("@6"));
      CHECK(doc->counted.size() == 2 && doc->counted.at("@7") == 2);
    }
    CHECK(values::Tracked::live > before);
  }
  CHECK(values::Tracked::live == before);
  {
    // A refusing converter: the member keeps its default, and a diagnostic says
    // so.
    const auto parsed =
        engine().parse("values %1 ; either %2 ^0 ~a $1 #x @1 then @2 <0>");
    const auto* doc = parsed.value_as<values::Doc>();
    CHECK(doc != nullptr && doc->guarded.raw.empty() && doc->numbers.empty());
    CHECK(parsed.diagnostics.size() == 2);
  }
  CHECK(values::Tracked::live == before);
}

void a_class_reached_by_two_paths_is_one_node() {
  const auto reflection = tconv::generated::G::create_ast_reflection();
  const auto parsed =
      engine().parse("diamond first : int &hello second : text &world");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<diamond::Doc>();
  CHECK(doc != nullptr && doc->fields.size() == 2);
  if (doc == nullptr || doc->fields.size() != 2) return;
  const cursiv::AstNode* const node = doc->fields[1];
  CHECK(node->parent == doc);
  const auto* const named = cursiv::ast_cast<diamond::Named>(node);
  const auto* const typed = cursiv::ast_cast<diamond::Typed>(node);
  const auto* const documented = cursiv::ast_cast<diamond::Documented>(node);
  CHECK(named != nullptr && named->name == "second");
  CHECK(typed != nullptr && typed->type == "text" && typed->name == "second");
  CHECK(documented != nullptr && documented->doc == "&WORLD" &&
        documented->name == "second");
  CHECK(cursiv::ast_cast<diamond::Doc>(node) == nullptr);
  std::size_t count = 1;
  cursiv::visit_all_contents(*reflection, *parsed.value,
                             [&](cursiv::AstNode&) { ++count; });
  CHECK(count == 3);
}

// A reference targets the shared class, and a check filed over one path runs on
// the node both paths reach.
struct DiamondChecks {
  mutable int typed = 0;
  void onTyped(const diamond::Typed& node, cursiv::ValidationAcceptor&) const {
    if (!node.name.empty() && !node.type.empty()) ++typed;
  }
};

void a_class_reached_by_two_paths_links_and_validates() {
  support::World<cursiv::SharedCoreServices> world;
  auto& language = support::add_language<cursiv::CoreServices>(
      world.shared, {"conv", {".conv"}, {}},
      tconv::generated::G::create_parser(),
      tconv::generated::G::create_ast_reflection());
  const DiamondChecks checks;
  language.validationRegistry->registerCheck(
      cursiv::ValidationRegistry::makeValidationCheck<&DiamondChecks::onTyped>(
          checks));
  const cursiv::Document* const document = world.built(
      "file:///a.conv",
      "diamond first : int &hello second : text &world use second use missing");
  const auto* const doc = document->parseResult.value_as<diamond::Doc>();
  CHECK(doc != nullptr && doc->fields.size() == 2 && doc->uses.size() == 2);
  if (doc == nullptr || doc->fields.size() != 2 || doc->uses.size() != 2)
    return;
  const diamond::Named* const target = doc->uses[0]->target.get();
  CHECK(target != nullptr && target->name == "second");
  CHECK(target == cursiv::ast_cast<diamond::Named>(doc->fields[1]));
  CHECK(doc->uses[1]->target.get() == nullptr);
  CHECK(checks.typed == 2);
}

// A node the parser did not build says nothing of where its bases sit, whether
// its tag was written by hand or never.
void a_node_typed_by_hand_is_cast_too() {
  diamond::Hand hand;
  cursiv::AstNode& node = hand;
  const diamond::Named* const named = &hand;
  node.type = &cursiv::ast_type_tag<diamond::Hand>;
  CHECK(cursiv::ast_cast<diamond::Named>(&node) == named);
  node.type = nullptr;
  CHECK(cursiv::detail::ast_cast_unchecked<const diamond::Named>(&node) ==
        named);
}

// The view holds what is written and the `string` what it means.
void a_terminal_feeds_a_view_and_a_decoded_string() {
  const std::string source = R"(chain "a\\b" "a\\b")";
  const auto parsed = engine().parse(source);
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->raw == R"(a\\b)");
  CHECK(doc->raw.data() == source.data() + source.find('a', 6));
  CHECK(doc->decoded == "ab");
}

// A data rule passes a built string on to a class made from it, as a converter
// on the rule itself would.
void a_passed_on_string_builds_a_class() {
  const auto parsed = engine().parse(R"(chain "a" "b" @tag)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr && doc->boxed.text == "tag");
}

// A class with no empty state fills an optional and a list, and a converter
// answering a whole list of variants fills it at once.
void a_value_with_no_default_constructor_fills_a_list() {
  const auto parsed = engine().parse(R"(chain "a" "b" $one $two $three #word)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->first.has_value() && doc->first->text == "one");
  CHECK(doc->rest.size() == 2 && doc->rest[1].text == "three");
  CHECK(doc->mixed.size() == 2 &&
        std::get<std::string>(doc->mixed[1]) == "word");
}

// Written where it stands, and nothing past it.
void a_direct_member_with_no_empty_state_is_written_in_place() {
  const auto parsed = engine().parse(R"(chain "a" "b" !word)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->only.text == "word");
  CHECK(doc->guard == 0);
}

// `as<T>` makes a `T` first: the member that takes it does not widen the range.
void a_named_type_converts_before_the_member_takes_it() {
  // An integer is a fair value for a floating member.
  const auto ratio = engine().parse(R"(chain "a" "b" ratio 3)");
  CHECK(ratio && ratio.diagnostics.empty());
  const auto* whole = ratio.value_as<chain::Doc>();
  CHECK(whole != nullptr && whole->ratio == 3.0);
  const auto fits = engine().parse(R"(chain "a" "b" small 127)");
  CHECK(fits && fits.diagnostics.empty());
  const auto* doc = fits.value_as<chain::Doc>();
  CHECK(doc != nullptr && doc->wide == 127);
  const auto wide = engine().parse(R"(chain "a" "b" small 128)");
  CHECK(!wide.diagnostics.empty());
  const auto* refused = wide.value_as<chain::Doc>();
  CHECK(refused != nullptr && refused->wide == -1);
}

// A list and an optional build the value in place: it needs no assignment.
void a_value_never_assigned_fills_a_list() {
  const auto parsed =
      engine().parse(R"(chain "a" "b" constant =first =second =third)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(doc->constText.has_value() && doc->constText->text == "first");
  CHECK(doc->constTexts.size() == 2 && doc->constTexts[1].text == "third");
}

// A rule that passes on a value with no empty state.
void a_passed_on_value_needs_no_empty_state() {
  const auto parsed = engine().parse(R"(chain "a" "b" pass $word)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr && doc->passed.has_value() &&
        doc->passed->text == "word");
}

void a_variant_with_no_empty_state_takes_its_alternative() {
  const auto parsed = engine().parse(R"(chain "a" "b" pick !one !two 7)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(std::get<chain::Ident>(doc->pick).text == "one");
  CHECK(doc->picks.size() == 2 &&
        std::get<chain::Ident>(doc->picks[0]).text == "two" &&
        std::get<std::int64_t>(doc->picks[1]) == 7);
}

// A converter may decide the alternative, or that there is no value at all.
void a_converter_answers_a_member_whole() {
  const auto parsed =
      engine().parse(R"(chain "a" "b" answer ?word ?one ?two maybe *some)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) return;
  CHECK(std::get<std::string>(doc->answer) == "word");
  CHECK(doc->answers.size() == 2 &&
        std::get<std::int64_t>(doc->answers[0]) == 1 &&
        std::get<std::string>(doc->answers[1]) == "two");
  CHECK(doc->maybe == "some");
  const auto none = engine().parse(R"(chain "a" "b" maybe *none)");
  CHECK(none && none.diagnostics.empty());
  const auto* empty = none.value_as<chain::Doc>();
  CHECK(empty != nullptr && !empty->maybe.has_value());
}

void a_member_takes_what_it_can_be_assigned() {
  const auto parsed = engine().parse(R"(chain "a" "b" latch @word)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr && doc->latch.text == "word");
  // A root of another class is not one of these.
  CHECK(parsed.value_as<deep::Box>() == nullptr);
}

void a_final_class_still_converts() {
  const auto parsed = engine().parse(R"(chain "a" "b" trimmed ^word)");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<chain::Doc>();
  CHECK(doc != nullptr && doc->trimmed == "word");
}

// Walks from deep in a long chain cost the tree's size, not size times
// depth. Timed against a build in the same run.
void a_deep_tree_is_walked_once() {
  constexpr std::size_t kDepth = 48000;
  const auto word = [](std::size_t n) {
    std::string out = "n";
    for (; n != 0; n /= 26) out += static_cast<char>('a' + n % 26);
    return out;
  };
  std::string text;
  for (std::size_t i = 0; i < kDepth; ++i)
    text += "[item " + word(i) + " see " + word(i) + " ";
  text.append(kDepth, ']');
  support::World<cursiv::SharedCoreServices> world;
  const auto* const language = &support::add_language<cursiv::CoreServices>(
      world.shared, {"conv", {".conv"}, {}},
      tconv::generated::G::create_parser(),
      tconv::generated::G::create_ast_reflection());
  const auto start = std::chrono::steady_clock::now();
  const cursiv::Document* const document =
      world.built("file:///deep.conv", std::move(text));
  const auto built = std::chrono::steady_clock::now() - start;
  CHECK(document->diagnostics.empty());
  const auto fastest = [](const auto& run) {
    auto best = std::chrono::steady_clock::duration::max();
    for (int round = 0; round < 5; ++round) {
      const auto begin = std::chrono::steady_clock::now();
      run();
      best = std::min(best, std::chrono::steady_clock::now() - begin);
    }
    return best;
  };
  std::size_t owners = 0;
  const auto symbols = fastest([&] {
    owners = cursiv::detail::collect_named_nodes(
                 *document, *language->nameProvider, cursiv::kNoCancel)
                 .owners.size();
  });
  CHECK(owners == kDepth);
  CHECK(symbols < built);
  std::size_t rows = 0;
  const auto described = fastest([&] {
    rows = language->referenceDescriptionProvider
               ->createDescriptions(*document, cursiv::kNoCancel)
               .size();
  });
  CHECK(rows == kDepth);
  CHECK(described < built);
}

// Diamonds on diamonds: a base reached by several paths is one base.
namespace diamonds {
template <int N>
struct Level;
template <int N, int Side>
struct Path : cursiv::VirtualExtends<Level<N>> {};
template <int N>
struct Level : cursiv::VirtualExtends<Path<N - 1, 0>, Path<N - 1, 1>> {};
template <>
struct Level<0> : cursiv::VirtualExtends<cursiv::AstNode> {};
static_assert(cursiv::detail::bases_of<Level<2>>::kCount == 2 * 3 + 1);
}  // namespace diamonds

void a_class_of_many_paths_states_each_base_once() {
  diamonds::Level<2> node;
  cursiv::AstNode* const base = cursiv::detail::set_type_tag(node);
  const auto* const path = cursiv::ast_cast<diamonds::Path<1, 1>>(base);
  CHECK(path == static_cast<const diamonds::Path<1, 1>*>(&node));
  const auto* const low = cursiv::ast_cast<diamonds::Level<0>>(base);
  CHECK(low == static_cast<const diamonds::Level<0>*>(&node));
}

// The node a reference resolves to, whichever candidate that is.
void a_reference_names_its_target_without_a_type() {
  support::World<cursiv::SharedCoreServices> world;
  (void)support::add_language<cursiv::CoreServices>(
      world.shared, {"conv", {".conv"}, {}},
      tconv::generated::G::create_parser(),
      tconv::generated::G::create_ast_reflection());
  const cursiv::Document* const document = world.built(
      "file:///target.conv", "diamond first : int &hello use first use none");
  const auto* const doc = document->parseResult.value_as<diamond::Doc>();
  CHECK(doc != nullptr && doc->uses.size() == 2);
  if (doc == nullptr || doc->uses.size() != 2) return;
  CHECK(doc->uses[0]->target.target() == doc->fields[0]);
  CHECK(doc->uses[1]->target.target() == nullptr);
}

// A class whose `name` is a `string` declares a name as one with a view does.
static_assert(cursiv::NamedAstNode<diamond::Named>);

// A node's destructor may read what its `string_view` members name: the
// document holds that text, and lets its nodes go first.
void a_document_keeps_its_text_past_its_nodes() {
  const std::size_t before = chain::Doc::read;
  {
    support::World<cursiv::SharedCoreServices> world;
    (void)support::add_language<cursiv::CoreServices>(
        world.shared, {"conv", {".conv"}, {}},
        tconv::generated::G::create_parser(),
        tconv::generated::G::create_ast_reflection());
    (void)world.built("file:///chain.conv",
                      R"(chain "abcdefghijklmnopqrstuvwxyz" "b")");
  }
  CHECK(chain::Doc::read != before);
}

// `vector<boolean>` is `std::vector<bool>`, whose elements are not `bool&`.
void a_list_of_booleans_holds_its_values() {
  const auto parsed = engine().parse("diamond bits + - +");
  CHECK(parsed && parsed.diagnostics.empty());
  const auto* doc = parsed.value_as<diamond::Doc>();
  CHECK(doc != nullptr && doc->bits == std::vector<bool>({true, false, true}));
}

void empty_materialized_members_keep_their_value() {
  for (const std::string_view text :
       {"materialized \"\" ``", "materialized \"ab\" `cd`"}) {
    const auto parsed = engine().parse(text);
    CHECK(parsed && parsed.diagnostics.empty());
    const auto* doc = parsed.value_as<materialized::Doc>();
    CHECK(doc != nullptr);
    if (doc == nullptr) continue;
    CHECK(doc->copied == (text == "materialized \"\" ``" ? "" : "ab"));
    CHECK(doc->written == (text == "materialized \"\" ``" ? "" : "cd"));
  }
}

// Parts that are all present but convert to empty text still write the
// member: only a member no conversion reached holds the null view.
void empty_joined_parts_keep_their_value() {
  const auto ll = tdual::generated::Ll::create_parser();
  const cursiv::Parser* const engines[]{&engine(), ll.get()};
  for (const cursiv::Parser* const parser : engines) {
    const auto parsed = parser->parse(parser == ll.get()
                                          ? "$conv:blank \"\" \"\""
                                          : "blank \"\" \"\"");
    CHECK(parsed && parsed.diagnostics.empty());
    const auto* value = parsed.value_as<comp::N>();
    CHECK(value != nullptr);
    if (value == nullptr) continue;
    CHECK(value->name.empty() && value->name.data() != nullptr);
  }
}

void floating_conversion_ignores_host_rounding() {
  struct Restore {
    int mode = std::fegetround();
    ~Restore() { std::fesetround(mode); }
  } restore;
  for (const int mode : {
           FE_TONEAREST,
#ifdef FE_DOWNWARD
           FE_DOWNWARD,
#endif
#ifdef FE_UPWARD
           FE_UPWARD,
#endif
#ifdef FE_TOWARDZERO
           FE_TOWARDZERO,
#endif
       }) {
    CHECK(std::fesetround(mode) == 0);
    for (const auto& [text, expected] :
         {std::pair{"0.1", 0.1}, std::pair{"-0.1", -0.1},
          std::pair{"00000000000000000000.1", 0.1}, std::pair{"42", 42.0}}) {
      const auto converted = cursiv::default_converter<double>::convert(text);
      CHECK(converted && *converted.value == expected);
    }
  }
}

// std::from_chars refuses a leading '+' for every arithmetic type; the
// default converter strips at most one so an optional-sign grammar's "+5"
// converts like "5" instead of failing, without accepting "++5".
void leading_plus() {
  auto plus = cursiv::default_converter<std::int32_t>::convert("+5");
  CHECK(plus && *plus.value == 5);
  auto plusplus = cursiv::default_converter<std::int32_t>::convert("++5");
  CHECK(!plusplus && !plusplus.error.message.empty());
  CHECK(!cursiv::default_converter<std::int32_t>::convert("+-5"));
  CHECK(!cursiv::default_converter<float>::convert("+-2.5"));
  CHECK(!cursiv::default_converter<double>::convert("+-2.5"));
  auto minus = cursiv::default_converter<std::int32_t>::convert("-5");
  CHECK(minus && *minus.value == -5);  // untouched
  auto plusf = cursiv::default_converter<double>::convert("+2.5");
  CHECK(plusf && *plusf.value == 2.5);
}

// Only the AstNode vocabulary's numbers get a default converter: not the
// wide character types, nor plain `char`, which would read "7" as 7.
void char_like_excluded() {
  static_assert(!cursiv::detail::has_default_convert<char>);
  static_assert(!cursiv::detail::has_default_convert<wchar_t>);
  static_assert(!cursiv::detail::has_default_convert<char8_t>);
  static_assert(!cursiv::detail::has_default_convert<char16_t>);
  static_assert(!cursiv::detail::has_default_convert<char32_t>);
  // int8/uint8 (signed/unsigned char) are numbers in the vocabulary and
  // keep their default converter -- this is not "every integer type minus
  // the character ones", it is exactly the AstNode vocabulary's set.
  static_assert(cursiv::detail::has_default_convert<std::int8_t>);
  static_assert(cursiv::detail::has_default_convert<std::uint8_t>);
  static_assert(cursiv::detail::from_chars_number<std::int8_t>);
  static_assert(!cursiv::detail::from_chars_number<char>);
  // ...and text is a `string_view` of the document, or a `string` with a copy.
  static_assert(cursiv::detail::has_default_convert<std::string_view>);
  static_assert(cursiv::detail::has_default_convert<std::string>);
}

// An enum's default conversion is its enumerator names, read through
// reflection -- no converter needed to write one from plain text.
using Vis = tab::enm::Vis;

void enum_defaults() {
  auto p = cursiv::default_converter<Vis>::convert("priv");
  CHECK(p && *p.value == Vis::priv);
  auto q = cursiv::default_converter<Vis>::convert("pub");
  CHECK(q && *q.value == Vis::pub);
  auto bad = cursiv::default_converter<Vis>::convert("nope");
  CHECK(!bad && bad.error.message.find("enumerator") != std::string::npos);
  static_assert(cursiv::detail::has_default_convert<Vis>);
}

// The probe is a window of values, not the run [0, 64): a sentinel below zero
// and a flag set's single bits above it are enumerators like any other.
enum class Sign { negative = -3, zero = 0, positive = 3 };
enum class Perm : unsigned { none = 0, read = 1, write = 64, exec = 256 };

void enumerators_outside_the_first_run() {
  auto n = cursiv::default_converter<Sign>::convert("negative");
  CHECK(n && *n.value == Sign::negative);
  auto z = cursiv::default_converter<Sign>::convert("zero");
  CHECK(z && *z.value == Sign::zero);
  auto w = cursiv::default_converter<Perm>::convert("write");
  CHECK(w && *w.value == Perm::write);
  auto x = cursiv::default_converter<Perm>::convert("exec");
  CHECK(x && *x.value == Perm::exec);
  // The window is bounded, and what falls outside has no spellable name.
  static_assert(cursiv::detail::kEnumProbeMin < 0 &&
                cursiv::detail::kEnumProbeMax > 256);
}

void a_repair_invents_no_value() {
  // `state` stands where a name was expected: the whole word is noise.
  for (const auto text : {"init state Off end", "init state\tOff end",
                          "init state\nOff end"}) {
    const cursiv::ParseResult r = engine().parse(text);
    CHECK(r.cst().recovered());
    const auto* n = r.value_as<inv::N>();
    CHECK(n != nullptr);
    // Neither resuming inside `state` nor joining it to `Off` may evade the
    // leading !kw<"state">, which already refused the original spelling.
    if (n != nullptr) CHECK(n->name == "Off");
  }

  // The sound document reads the same value, with no repair at all.
  const cursiv::ParseResult c = engine().parse("init Off end");
  CHECK(c.accepted);
  CHECK(!c.cst().recovered());
  const auto* m = c.value_as<inv::N>();
  CHECK(m != nullptr && m->name == "Off");
}

void a_leaf_converter_composes() {
  const cursiv::ParseResult r = engine().parse("a . 'b'");
  CHECK(r.accepted);
  const auto* n = r.value_as<comp::N>();
  CHECK(n != nullptr);
  // "a.b", not "a.'b'": the quotes are not part of what the parts denote.
  if (n != nullptr) CHECK(n->name == "a.b");
}

// A call that read nothing still gives its parent the text it converts to,
// in a grammar where no converter takes parts.
void an_empty_call_gives_its_text() {
  const cursiv::ParseResult none = engine().parse("emptycall abc");
  CHECK(none.accepted && none.diagnostics.empty());
  const auto* n0 = none.value_as<comp::N>();
  CHECK(n0 != nullptr);
  if (n0 != nullptr) CHECK(n0->name == "noneabc");
  const cursiv::ParseResult some = engine().parse("emptycall ! abc");
  CHECK(some.accepted && some.diagnostics.empty());
  const auto* n1 = some.value_as<comp::N>();
  CHECK(n1 != nullptr);
  if (n1 != nullptr) CHECK(n1->name == "!abc");
}

// A converter that reads nothing is run alone: the calls below its record, here
// one that refuses its text, are not its input.
void a_converter_without_input_runs_no_call() {
  for (const char* const text : {"fixed !ab.c/d", "fixed !az.c/d"}) {
    const cursiv::ParseResult r = engine().parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto* n = r.value_as<comp::N>();
    CHECK(n != nullptr);
    if (n != nullptr) CHECK(n->name == "fixed/d");
  }
}

void parts_on_data_rule() {
  // The value is what the parts denote, and the engine joins them: spaced
  // apart, the trivia between them is not part of the name.
  const cursiv::ParseResult r = separated().parse("$nest pub a . b");
  CHECK(r.accepted);
  const auto* n1 = r.value_as<tab::nested::N>();
  CHECK(n1 != nullptr && n1->name == "a.b");

  // No spaces: the same value, and the view is a slice of the source rather
  // than a freshly built buffer -- nothing was said differently, so nothing
  // is copied.
  const cursiv::ParseResult r2 = separated().parse("$nest pub a.b");
  CHECK(r2.accepted);
  const std::string_view src = r2.cst().text();
  const auto* n2 = r2.value_as<tab::nested::N>();
  CHECK(n2 != nullptr && n2->name == "a.b");
  CHECK(n2->name.data() >= src.data() &&
        n2->name.data() + n2->name.size() <= src.data() + src.size());
}

void data_value_at(const char* lvl) {
  const std::string src =
      "a.b ; c /*x*/ . d ; e . f / g . h ; i /*y*/ . j ! 'quoted'";
  const cursiv::ParseResult r = engine().parse(src);
  CHECK(r.accepted);
  const auto* d = static_cast<const dv::Doc*>(r.value);
  CHECK(d != nullptr);
  if (d == nullptr) {
    std::printf("  (at %s)\n", lvl);
    return;
  }
  const bool ok = d->a == "a.b" && d->b == "c.d" && d->c == "e.f/g.h" &&
                  d->r.refText == "i.j";
  CHECK(ok);
  CHECK(d->quote == "quoted");
  if (!ok)
    std::printf("  at %s: a='%.*s' b='%.*s' c='%.*s' r='%.*s'\n", lvl,
                static_cast<int>(d->a.size()), d->a.data(),
                static_cast<int>(d->b.size()), d->b.data(),
                static_cast<int>(d->c.size()), d->c.data(),
                static_cast<int>(d->r.refText.size()), d->r.refText.data());
  // Atoms that do not touch are joined into the shared pool, alive as long
  // as the `ParseResult`; atoms that touch stay a slice of the caller's buffer.
  const std::string_view doc = r.cst().text();
  const auto in_doc = [&](std::string_view v) {
    return v.data() >= doc.data() &&
           v.data() + v.size() <= doc.data() + doc.size();
  };
  CHECK(in_doc(d->a));                    // "a.b": the atoms touch
  CHECK(!in_doc(d->b) && !in_doc(d->c));  // joined into the pool
  CHECK(!in_doc(d->r.refText));
}

void data_value_is_the_language() { data_value_at("tables"); }

// A data rule's parts are the leaves of its span, so the join is the same
// at O0 and O2, which render different records.
void parts_are_atoms_at(const char* lvl) {
  const cursiv::ParseResult r =
      engine().parse("a.b ; c.d ; e . f / g . h ; i.j");
  CHECK(r.accepted);
  const cursiv::Cst& cst = r.cst();
  // `Path`'s span, found by extent: the third field, "e . f / g . h".
  cursiv::NodeId path = cursiv::kNoNode;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    if (cst[i].begin == 12 && cst[i].end == 25 &&
        (path == cursiv::kNoNode || cst[i].subtreeSize < cst[path].subtreeSize))
      path = i;
  CHECK(path != cursiv::kNoNode);
  if (path == cursiv::kNoNode) {
    std::printf("  (at %s)\n", lvl);
    return;
  }
  // e . f / g . h -> the seven atoms, never the three direct children
  // (QN, "/", QN) whose own trivia would have survived the join. The value
  // the builder writes is the proof: it is what the leaves denote, joined.
  const auto* doc = r.value_as<dv::Doc>();
  CHECK(doc != nullptr);
  if (doc == nullptr) {
    std::printf("  (at %s)\n", lvl);
    return;
  }
  if (doc->c != "e.f/g.h") {
    ++failures;
    std::printf("FAIL the joined path lost its atoms (at %s)\n  got [%s]\n",
                lvl, std::string(doc->c).c_str());
  }
}

void parts_are_atoms() { parts_are_atoms_at("tables"); }

// `fast_decimal` either declines or gives the same 64 bits as
// `std::from_chars`. A divergence and an over-acceptance are told apart.
std::uint64_t bits_of(double d) {
  std::uint64_t u = 0;
  std::memcpy(&u, &d, sizeof u);
  return u;
}

int diverged = 0;
int over_accepted = 0;
int took_fast = 0;
int cases = 0;

void differential(std::string_view t) {
  ++cases;
  double fast = 0.0;
  if (!cursiv::detail::fast_decimal(t, fast)) return;
  ++took_fast;
  double slow = 0.0;
  const auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), slow);
  if (ec != std::errc{} || p != t.data() + t.size()) {
    ++over_accepted;
    std::printf("FAIL over-accepted '%.*s'\n", static_cast<int>(t.size()),
                t.data());
    ++cursiv::test::failures;
    return;
  }
  if (bits_of(fast) != bits_of(slow)) {
    ++diverged;
    std::printf("FAIL diverged '%.*s': fast=%.17g slow=%.17g\n",
                static_cast<int>(t.size()), t.data(), fast, slow);
    ++cursiv::test::failures;
  }
}

void clinger_is_from_chars() {
  // The adversaries first, one by one: the signs and the bare forms, the two
  // sides of every window bound (10^22 in, 10^23 out; 2^53 in, 2^53+1 out),
  // the extremes of the format, and the spellings that must fall through.
  for (const char* s : {"0",
                        "-0",
                        "0.0",
                        "-0.0",
                        ".5",
                        "-.5",
                        "5.",
                        "5",
                        "42",
                        "-42",
                        "+1",
                        "1e22",
                        "1E22",
                        "1e23",
                        "1e-22",
                        "1e-23",
                        "1e+22",
                        "9007199254740992",
                        "9007199254740993",
                        "1.7976931348623157e308",
                        "5e-324",
                        "1e999",
                        "-1e999",
                        "2.2250738585072014e-308",
                        "0.1",
                        "3.141592653589793",
                        "1234567890123456789",
                        "12345678901234567890",
                        "1e",
                        "e5",
                        ".",
                        "-",
                        "",
                        "inf",
                        "nan",
                        "0x1p3",
                        "1 ",
                        " 1",
                        "1.2.3",
                        "1e1e1"})
    differential(s);

  // Then the sweep: splitmix64, so the corpus is byte-identical on every run
  // and machine.
  std::uint64_t rng = 0xC1149E12ULL;
  const auto next = [&rng] {
    std::uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  };
  char buf[64];
  for (int i = 0; i < 300000; ++i) {
    const std::uint64_t r = next();
    const int shape = static_cast<int>(r % 5);
    int n = 0;
    switch (shape) {
      case 0:  // an integer, up to 20 digits: straddles the 19-digit bound
        n = std::snprintf(buf, sizeof buf, "%llu",
                          static_cast<unsigned long long>(
                              next() >> static_cast<int>(next() % 64)));
        break;
      case 1:  // a fixed-point value, 0 to 17 fraction digits
        n = std::snprintf(
            buf, sizeof buf, "%.*f", static_cast<int>(next() % 18),
            static_cast<double>(static_cast<std::int64_t>(next())) /
                static_cast<double>(1 + (next() % 1000000)));
        break;
      case 2:  // scientific, the exponent straddling the [-22, 22] window
        n = std::snprintf(buf, sizeof buf, "%llue%+d", (next() % 1000000000ULL),
                          static_cast<int>(next() % 81) - 40);
        break;
      case 3:  // a 20-digit significand: the accumulator must refuse it
        n = std::snprintf(buf, sizeof buf, "%020llu",
                          static_cast<unsigned long long>(next()));
        break;
      default:  // %e, the round-trip spelling
        n = std::snprintf(
            buf, sizeof buf, "%.17e",
            static_cast<double>(static_cast<std::int64_t>(next())) * 1e-9);
        break;
    }
    if (n > 0) differential(std::string_view(buf, static_cast<std::size_t>(n)));
  }
  std::printf(
      "  clinger: %d spellings, %d took the fast path (%.1f%%), "
      "%d divergences, %d over-acceptances\n",
      cases, took_fast,
      100.0 * static_cast<double>(took_fast) / static_cast<double>(cases),
      diverged, over_accepted);
  CHECK(diverged == 0);
  CHECK(over_accepted == 0);
  CHECK(took_fast > cases / 4);  // the path must actually be taken

  // And the same through the public converter the grammars use.
  auto a = cursiv::default_converter<double>::convert("42");
  CHECK(a && *a.value == 42.0);
  auto b = cursiv::default_converter<double>::convert("1e999");
  CHECK(!b && !b.error.message.empty());
  auto c = cursiv::default_converter<double>::convert("0.1");
  CHECK(c && bits_of(*c.value) == bits_of(0.1));
  auto e = cursiv::default_converter<double>::convert("1.2.3");
  CHECK(!e && !e.error.message.empty());
  // float keeps the library call -- decimal -> double -> float would round
  // twice, so the fast path is not offered for it.
  auto f = cursiv::default_converter<float>::convert("0.1");
  CHECK(f && *f.value == 0.1f);
}
}  // namespace

void run() {
  defaults();
  keyword_template_converters_compile_and_parse();
  text_helpers_keep_empty_values_present();
  converters_answer_classes_of_their_own();
  a_class_reached_by_two_paths_is_one_node();
  a_class_reached_by_two_paths_links_and_validates();
  a_node_typed_by_hand_is_cast_too();
  a_list_of_booleans_holds_its_values();
  a_terminal_feeds_a_view_and_a_decoded_string();
  a_passed_on_string_builds_a_class();
  a_value_with_no_default_constructor_fills_a_list();
  a_direct_member_with_no_empty_state_is_written_in_place();
  a_named_type_converts_before_the_member_takes_it();
  a_value_never_assigned_fills_a_list();
  a_passed_on_value_needs_no_empty_state();
  a_variant_with_no_empty_state_takes_its_alternative();
  a_converter_answers_a_member_whole();
  a_member_takes_what_it_can_be_assigned();
  a_final_class_still_converts();
  a_deep_tree_is_walked_once();
  a_class_of_many_paths_states_each_base_once();
  a_reference_names_its_target_without_a_type();
  a_document_keeps_its_text_past_its_nodes();
  empty_materialized_members_keep_their_value();
  empty_joined_parts_keep_their_value();
  floating_conversion_ignores_host_rounding();
  leading_plus();
  char_like_excluded();
  enum_defaults();
  enumerators_outside_the_first_run();
  parts_on_data_rule();
  a_leaf_converter_composes();
  an_empty_call_gives_its_text();
  a_converter_without_input_runs_no_call();
  a_repair_invents_no_value();
  parts_are_atoms();
  data_value_is_the_language();
  clinger_is_from_chars();
}

}  // namespace convert

// The CST runtime alone, no grammar involved: postorder commit, subtree
// sizes and spans, growth, the parent inserted in front of trailing trivia,
// reverse navigation, what a move and a reset leave behind.
namespace cst {

namespace {

// `ab cd` as: Pair( Word[0,2), Space[2,3) hidden, Word[3,5) ). The element
// ids are 0, 1 and 2 and stay numbers here: what they name is the grammar's
// answer, and this test is about the tape.
void build_pair(cursiv::Cst& cst) {
  cursiv::CstBuilder b(cst);
  const auto open = b.enter();
  b.leaf(0, 2, 0);
  b.hidden(2, 3, 1);
  b.leaf(3, 5, 0);
  b.exit(open, 0, 2);
}

void basics() {
  cursiv::Cst cst{"ab cd"};
  build_pair(cst);

  CHECK(cst.size() == 4);
  CHECK(cst[3].subtreeSize == 4);  // the parent covers everything
  CHECK(cst[3].begin == 0 && cst[3].end == 5);
  CHECK(cst.childless(0) && cst.childless(2));
  CHECK(!cst.childless(3));
  CHECK(cst.text(0) == "ab");
  CHECK(cst.text(2) == "cd");
  CHECK(cst.text(3) == "ab cd");
  CHECK(cst[0].engine == 0 && cst[3].engine == 2);
  CHECK(cst.subtree_begin(3) == 0);
  CHECK(cst.contains(3, 1));

  // The predicate bits: leaves are leaves, the trivia leaf is hidden, nothing
  // is recovered, the parent is neither.
  CHECK(cst.childless(0) && cst.childless(1) && cst.childless(2));
  CHECK(!cst.childless(3));
  CHECK(cst[1].hidden && !cst[0].hidden && !cst[3].hidden);

  // Reverse children: last child first, hidden leaf included.
  std::vector<cursiv::NodeId> children;
  for (cursiv::NodeId id : cst.reverse_children(3)) children.push_back(id);
  CHECK(children.size() == 3);
  CHECK(children[0] == 2 && children[1] == 1 && children[2] == 0);
}

// The run doubles as it grows, and every record written survives the moves.
void growth_keeps_the_records() {
  std::string big(20000, 'x');
  cursiv::Cst cst{std::string_view(big)};
  cursiv::CstBuilder b(cst);
  for (int i = 0; i < 10000; ++i)
    b.leaf(static_cast<cursiv::TextOffset>(i),
           static_cast<cursiv::TextOffset>(i + 1), 0);
  CHECK(cst.size() == 10000);
  CHECK(cst[0].begin == 0);
  CHECK(cst[9999].begin == 9999);
}

void a_large_document_is_reserved_for() {
  cursiv::Cst cst;
  cursiv::CstBuilder b(cst);
  b.reserve_stream(std::size_t{72} << 20);
  b.leaf(0, 1, 0);
  CHECK(cst.size() == 1);
}

void reservation_follows_record_density() {
  class CountingMemory final : public std::pmr::memory_resource {
   public:
    std::size_t allocated = 0;

   private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
      allocated += bytes;
      return _storage.allocate(bytes, align);
    }
    void do_deallocate(void*, std::size_t, std::size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const
        noexcept override {
      return this == &other;
    }
    std::pmr::monotonic_buffer_resource _storage;
  } memory;
  cursiv::CstRun run;
  run.bind(&memory);
  run.reserve_for(sizeof(void*) < 8 ? std::size_t{64} << 20
                                   : std::size_t{256} << 10);
  if constexpr (sizeof(void*) < 8)
    CHECK(memory.allocated <= (std::size_t{1} << 20));
  CHECK(run.records() == 0);

  run.bind(&memory);
  run.reserve_for(sizeof(void*) < 8 ? std::size_t{4} << 20
                                   : std::size_t{256} << 10);
  const std::size_t initial = memory.allocated;
  cursiv::CstRun moved(std::move(run));
  run = std::move(moved);
  constexpr cursiv::NodeCount count = 1024 * 1024;
  for (cursiv::NodeCount i = 0; i < count; ++i) run.leaf(i, i + 1, 0);
  CHECK(run.records() == count);
  CHECK(run.at(0).begin == 0);
  CHECK(run.at(count / 2).end == count / 2 + 1);
  CHECK(run.at(count - 1).end == count);
  if constexpr (sizeof(void*) < 8)
    CHECK(memory.allocated - initial <= (std::size_t{17} << 20));
  else
    CHECK(memory.allocated < std::size_t{count} * sizeof(cursiv::CstNode) * 4);
  const std::size_t allocated = memory.allocated;
  run.clear();
  run.leaf(0, 1, 0);
  CHECK(memory.allocated == allocated);
  run.bind(&memory);
  run.leaf(0, 1, 0);
  CHECK(memory.allocated - allocated <= 1024 * sizeof(cursiv::CstNode));
}

// A run on the heap is one parse branch among thousands: whatever it writes,
// it holds at most twice that.
void a_branch_run_holds_at_most_twice_its_records() {
  cursiv::CstRun run;
  for (cursiv::NodeCount i = 0; i < 5000; ++i) {
    run.leaf(i, i + 1, 0);
    CHECK(run.capacity() <= std::max<std::size_t>(64, 2 * run.records()));
  }
}

// A watched run reads the stop once per run of records, at the check that
// guards its capacity; the watch ends with its scope and never travels with a
// move.
void a_watched_run_answers_a_stop_within_a_run_of_records() {
  constexpr cursiv::NodeCount kRun = 64 * 1024;
  cursiv::CancellationTokenSource source;
  const cursiv::CancellationToken token = source.get_token();
  cursiv::CstRun run;
  run.reserve_for(std::size_t{4} * kRun);
  cursiv::NodeCount written = 0;
  bool threw = false;
  {
    cursiv::CstRun::Watch watch(run, token);
    for (; written < kRun / 2; ++written) run.leaf(written, written + 1, 0);
    source.request_stop();
    try {
      for (; written < 4 * kRun; ++written) run.leaf(written, written + 1, 0);
    } catch (const cursiv::OperationCancelled&) {
      threw = true;
    }
  }
  CHECK(threw && written <= kRun);

  // Unwatched again: the stop is no longer read, and the capacity still grows.
  for (cursiv::NodeCount i = 0; i < 4 * kRun; ++i) run.leaf(i, i + 1, 0);
  CHECK(run.records() == written + 4 * kRun);

  cursiv::CstRun watched;
  cursiv::CstRun::Watch watch(watched, token);
  cursiv::CstRun moved(std::move(watched));
  for (cursiv::NodeCount i = 0; i < 4 * kRun; ++i) moved.leaf(i, i + 1, 0);
  CHECK(moved.records() == 4 * kRun);
}

void entry_defaults() {
  cursiv::Cst cst{"ab cd"};
  build_pair(cst);
  // Entry binding is the parser's (a friend); a bare tree has none.
  CHECK(!cst.has_entry());
  CHECK(!cst.accepted());
  CHECK(cst.entry_root() == cursiv::kNoNode);
}

void moved_from_is_empty() {
  cursiv::Cst cst{"ab cd"};
  build_pair(cst);

  cursiv::Cst taken = std::move(cst);
  CHECK(taken.size() == 4);
  CHECK(taken.text(3) == "ab cd");

  CHECK(cst.size() == 0);
  CHECK(cst.text().empty());
  CHECK(!cst.has_entry());
  CHECK(!cst.accepted());
  CHECK(cst.entry_root() == cursiv::kNoNode);
  CHECK(cst.entry_begin() == 0 && cst.entry_end() == 0);

  // Reusable afterwards: `reset` rebuilds the pool the move took away.
  cst.reset("zz");
  cursiv::CstBuilder b(cst);
  b.leaf(0, 2, 0);
  CHECK(cst.size() == 1 && cst.text(0) == "zz");

  // Move-assignment leaves the same state, and self-assignment is a no-op.
  cursiv::Cst sink;
  sink = std::move(cst);
  CHECK(sink.size() == 1 && cst.size() == 0);
  // Through an indirection: gcc rejects the literal self-move under
  // -Werror=self-move, and aliasing callers are exactly what the guard is for.
  const auto move_into = [](cursiv::Cst& to, cursiv::Cst& from) {
    to = std::move(from);
  };
  move_into(sink, sink);
  CHECK(sink.size() == 1 && sink.text(0) == "zz");
}

// A repair belongs to the tree: it moves with it and goes with a reset.
void diagnostics_follow_the_tree() {
  cursiv::Cst cst{"ab cd"};
  cursiv::CstBuilder b(cst);
  b.note_repair(cursiv::DiagnosticKind::deleted, cursiv::kNoElement, 0, 2);
  CHECK(cst.recovered() && cst.diagnostics().size() == 1);
  CHECK(cst.diagnostics()[0].kind == cursiv::DiagnosticKind::deleted);

  cursiv::Cst taken = std::move(cst);
  CHECK(taken.recovered() && taken.diagnostics().size() == 1);
  CHECK(!cst.recovered() && cst.diagnostics().empty());

  // A work-bound note repairs nothing: it is kept, and the tree is
  // not recovered by it alone.
  cursiv::Cst bounded{"ab cd"};
  cursiv::CstBuilder(bounded).note_repair(cursiv::DiagnosticKind::bounded,
                                          cursiv::kNoElement, 0, 5);
  CHECK(!bounded.recovered() && bounded.diagnostics().size() == 1);
  cursiv::CstBuilder(bounded).note_repair(cursiv::DiagnosticKind::deleted,
                                          cursiv::kNoElement, 0, 2);
  CHECK(bounded.recovered());

  // A second parse on the same tree starts clean.
  taken.reset("zz");
  CHECK(!taken.recovered() && taken.diagnostics().empty());
  CHECK(taken.size() == 0 && taken.text() == "zz");

  // A repair equal to the one before is the same fault; one that names other
  // alternatives is another.
  cursiv::Cst closers{"ab"};
  cursiv::CstBuilder c(closers);
  const auto insert = [&c](std::vector<cursiv::ElementId> also) {
    c.note_repair(cursiv::DiagnosticKind::inserted, 1, 1, 1, std::move(also));
  };
  insert({2});
  insert({2});
  insert({3});
  CHECK(closers.diagnostics().size() == 2);
  CHECK(closers.diagnostics().back().also == std::vector<cursiv::ElementId>{3});
}

// The tape's end order, at the builder: a parent closing behind trailing
// trivia is inserted in front of it, so `end` never goes back as ids grow.
void exit_inserts_before_trailing_trivia() {
  cursiv::Cst cst{"ab cd "};
  cursiv::CstBuilder b(cst);
  const auto open = b.enter();
  b.leaf(0, 2, 0);
  b.hidden(2, 3, 1);
  b.leaf(3, 5, 0);
  b.hidden(5, 6, 1);  // trailing: taken after the last visible record
  const cursiv::NodeId id = b.exit(open, 0, 2);
  CHECK(id == 3);  // right after the last visible child, not at the tape's end
  CHECK(cst.size() == 5);
  CHECK(cst[3].subtreeSize == 4);  // the trailing trivia is not a child
  CHECK(cst[3].end == 5);          // ...and not in the span
  CHECK(cst[4].hidden && cst[4].begin == 5);
  for (cursiv::NodeId i = 1; i < cst.size(); ++i)
    CHECK(cst[i - 1].end <= cst[i].end);
}

// An empty composite goes at its region's open -- in front of trivia taken
// inside -- and becomes the last visible record there.
void empty_composite_keeps_the_order() {
  cursiv::Cst cst{" x"};
  cursiv::CstBuilder b(cst);
  const auto open = b.enter();
  b.hidden(0, 1, 1);  // trivia taken inside the (otherwise empty) region
  const cursiv::NodeId id = b.exit(open, 0, 2);
  CHECK(id == 0);
  CHECK(cst.size() == 2);
  CHECK(cst[0].begin == 0 && cst[0].end == 0 && cst[0].subtreeSize == 1);
  CHECK(cst[1].hidden);
  CHECK(cst[0].end <= cst[1].end);

  // With nothing on the tape at all it simply appends.
  cursiv::Cst flat{"x"};
  cursiv::CstBuilder c(flat);
  const auto o2 = c.enter();
  CHECK(c.exit(o2, 0, 2) == 0);
  CHECK(flat.size() == 1 && flat[0].begin == 0 && flat[0].end == 0);

  // A visible record before the open is not the region's: still empty.
  const auto o3 = c.enter();
  c.hidden(1, 1, 1);
  CHECK(c.exit(o3, 1, 2) == 1);
  CHECK(flat.size() == 3 && flat[1].begin == 1 && flat[1].end == 1);
  CHECK(flat[2].hidden);
}

// An insert into a full buffer is what grows it: the suffix moves
// into the new buffer, and the next append goes behind it.
void insert_across_a_reallocation() {
  std::string big(9000, 'x');
  cursiv::Cst cst{std::string_view(big)};
  cursiv::CstBuilder b(cst);
  const auto open = b.enter();
  for (int i = 0; i < 4095; ++i)
    b.leaf(static_cast<cursiv::TextOffset>(i),
           static_cast<cursiv::TextOffset>(i + 1), 0);
  b.hidden(4095, 4096, 1);  // record 4095 fills the doubled buffer exactly
  const cursiv::NodeId id = b.exit(open, 0, 2);
  CHECK(id == 4095);
  CHECK(cst.size() == 4097);
  CHECK(cst[4095].subtreeSize == 4096 && cst[4095].end == 4095);
  CHECK(cst[4096].hidden && cst[4096].end == 4096);
  b.leaf(4096, 4097, 0);
  CHECK(cst.size() == 4098 && cst[4097].begin == 4096);
}

// The leaves are found from the records: the first ending past an offset, the
// one before a record and the next one, over parents, trivia and an empty
// record.
void leaves_are_found_from_the_records() {
  const std::string_view text = "ab cd ef";
  cursiv::Cst cst{text};
  {
    cursiv::CstBuilder b(cst);
    const auto outer = b.enter();
    (void)b.leaf(0, 2, 0);      // 0
    (void)b.hidden(2, 3, 0);    // 1
    (void)b.leaf(3, 5, 0);      // 2
    (void)b.exit(outer, 0, 0);  // 3, a parent
    (void)b.hidden(5, 6, 0);    // 4, trailing trivia
    const auto last = b.enter();
    (void)b.exit(b.enter(), 6, 0);  // 5, an empty record
    (void)b.leaf(6, 8, 0);          // 6
    (void)b.exit(last, 6, 0);       // 7, a parent
  }
  CHECK(cst.size() == 8 && !cst.childless(3) && !cst.childless(7));
  CHECK(cst.leaf_after(0) == 0 && cst.leaf_after(1) == 0);
  CHECK(cst.leaf_after(2) == 1 && cst.leaf_after(3) == 2);
  CHECK(cst.leaf_after(5) == 4 && cst.leaf_after(6) == 6);
  CHECK(cst.leaf_after(8) == cst.size() && cst.leaf_after(99) == cst.size());
  CHECK(cst.leaf_before(4) == 2 && cst.leaf_before(6) == 5);
  CHECK(cst.leaf_before(cst.size()) == 6 && cst.leaf_before(1) == 0);
  CHECK(cst.next_leaf(2) == 4 && cst.next_leaf(4) == 5);
  CHECK(cst.next_leaf(6) == cst.size());
  const cursiv::CstLeaf trivia = cst.as_leaf(4);
  CHECK(trivia.id == 4 && trivia.hidden && trivia.begin == 5 &&
        trivia.end == 6);
}

}  // namespace

void run() {
  basics();
  growth_keeps_the_records();
  a_large_document_is_reserved_for();
  reservation_follows_record_density();
  a_branch_run_holds_at_most_twice_its_records();
  a_watched_run_answers_a_stop_within_a_run_of_records();
  entry_defaults();
  moved_from_is_empty();
  diagnostics_follow_the_tree();
  exit_inserts_before_trailing_trivia();
  empty_composite_keeps_the_order();
  insert_across_a_reallocation();
  leaves_are_found_from_the_records();
}

}  // namespace cst

// The DSL surface the table engine reads: each construction of the example
// grammars on the smallest grammar that spells it, checked by what it accepts
// and refuses.
namespace cst_node {

namespace {

using namespace tcstn;

// `table` only where a test reads the frozen table itself.
template <class G>
struct Gen;
template <>
struct Gen<LexG> {
  static constexpr auto make = &tcstn::generated::LexG::create_parser;
};
template <>
struct Gen<InfixG> {
  static constexpr auto make = &tcstn::generated::InfixG::create_parser;
  static constexpr const auto& table = tcstn::generated::InfixG::kTable;
};
template <>
struct Gen<OverlapG> {
  static constexpr auto make = &tcstn::generated::LexG::create_parser;
};
template <>
struct Gen<WordGuardG> {
  static constexpr auto make = &tcstn::generated::LexG::create_parser;
};

template <class G>
const cursiv::Parser& engine() {
  static const std::unique_ptr<cursiv::Parser> p = Gen<G>::make();
  return *p;
}

template <class G>
bool reads(std::string_view text) {
  std::string input;
  if constexpr (std::is_same_v<G, OverlapG>) input = "overlap:";
  if constexpr (std::is_same_v<G, WordGuardG>) input = "guard:";
  input += text;
  const cursiv::ParseResult r = engine<G>().parse(input);
  return r.accepted && !r.cst().recovered();
}

// Every span a record of `row` covers, in tape order: a precedence table
// shows as the nesting of the folds.
template <class T, class... Rs>
constexpr auto climbs(cursiv::TypeList<Rs...>) {
  return std::array<bool, sizeof...(Rs)>{
      (cursiv::engine::row_of<Rs>::kind == cursiv::engine::WriteKind::climb &&
       std::is_same_v<typename cursiv::engine::row_of<Rs>::type, T>)...};
}
template <class G, class T>
const std::vector<bool>& builds() {
  static const std::vector<bool> out = [] {
    constexpr auto climb =
        climbs<T>(typename cursiv::engine::LrLowering<G>::actions{});
    const auto& low = cursiv::engine::LrLowering<G>::table();
    const cursiv::engine::LrBuildTable a =
        cursiv::engine::lr_build_automaton(low);
    // The core graph stands, and no condition joins two chains: the
    // lowering's are the table's.
    CHECK(a.nMergeInduced == 0 && a.nImpliedMixed == 0);
    CHECK(low.cfg.ngapConds == 0);
    const std::span<const cursiv::engine::LrFusedRow> fused(a.fused.data(),
                                                            a.nfused);
    std::vector<bool> b(low.writes.nchains + fused.size(), false);
    for (std::uint32_t i = 0; i < b.size(); ++i)
      for (const std::uint16_t r :
           cursiv::test::entry_runs(low.writes, fused, i).held)
        b[i] = b[i] || climb[r];
    return b;
  }();
  return out;
}

template <class G, class T>
std::string spans_of(std::string_view text) {
  const cursiv::ParseResult r = engine<G>().parse(text);
  if (!r.accepted) return "<refused>";
  const cursiv::Cst& cst = r.cst();
  std::string out;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::ElementId row = Gen<G>::table.stamp.row(cst[i]);
    if (row >= builds<G, T>().size() || !builds<G, T>()[row]) continue;
    const cursiv::CstNode& n = cst[i];
    out += '[';
    out += std::to_string(n.begin);
    out += ',';
    out += std::to_string(n.end);
    out += ')';
  }
  return out;
}

void a_refused_word_stops_the_token_where_the_predicate_did() {
  const char* trivia[] = {
      "#\na",
      "#  \na",
      "#define X\na",
      "#  define X\na",
      // The word's own boundary: `#includes` is not `#include`.
      "#includes_more\na",
      "#  includex\na",
      // A proper prefix of the word is not the word.
      "#inclu\na",
      "#includ\na",
      "#i\na",
  };
  const char* parser[] = {
      // The word is spelled: the line stays for whoever reads `#include`.
      "#include <x.h>\na",
      "#  include <x.h>\na",
      "#include\na",
      "#include \na",
  };
  for (const char* t : trivia) CHECK(reads<WordGuardG>(t));
  for (const char* t : parser) CHECK(!reads<WordGuardG>(t));
}

void ikw_reads_either_case_and_stops_at_the_boundary() {
  CHECK(reads<LexG>("entity a"));
  CHECK(reads<LexG>("ENTITY a"));
  CHECK(reads<LexG>("EnTiTy a"));
  // The boundary of `kw<>` is what refuses a longer word.
  CHECK(!reads<LexG>("entityx a"));
  CHECK(!reads<LexG>("entit a"));
}

void through_runs_to_its_literal_closer() {
  CHECK(reads<LexG>("a , /* b */ c"));
  CHECK(reads<LexG>("a , /**/ c"));
  // A closer half-spelled inside the run does not end it.
  CHECK(reads<LexG>("a , /* * / ** */ c"));
  CHECK(!reads<LexG>("a , /* b c"));
}

void repeat_counts_exactly() {
  CHECK(reads<LexG>("\\u00ff"));
  CHECK(reads<LexG>("\\u0000\\uabcd"));
  CHECK(!reads<LexG>("\\u00f"));
  CHECK(!reads<LexG>("\\u00fff"));
}

void a_frontier_of_one_byte_refuses_the_digit_behind_it() {
  CHECK(reads<LexG>("@a"));
  CHECK(reads<LexG>("@a1"));
  CHECK(reads<LexG>("@a,@b"));
  // The identifier `1` is spellable, so the refusal is the frontier's own.
  CHECK(!reads<LexG>("@1"));
  CHECK(!reads<LexG>("@a,@2b"));
}

void two_overlapping_bytes_are_decided_by_the_state() {
  // The pair asks for a candidate walk, and the table says so instead of
  // lowering the grammar to the character.
  const auto isolated = cursiv::engine::lr_build_automaton(
      cursiv::engine::LrLowering<OverlapG>::table());
  CHECK(isolated.multiCand && isolated.ncls > 0);
  static_assert(tcstn::generated::LexG::kTable.multiCand);
  static_assert(tcstn::generated::LexG::kTable.ncls > 0);

  // The same byte, read as either terminal, by what follows it alone.
  CHECK(reads<OverlapG>("a*b"));
  CHECK(reads<OverlapG>("a*1"));
  // A byte only one of the two spells stays decided without a fork.
  CHECK(reads<OverlapG>("a!1"));
  CHECK(reads<OverlapG>("a~1"));
  CHECK(!reads<OverlapG>("a!b"));
  CHECK(!reads<OverlapG>("a*"));
}

void infix_binds_by_its_levels() {
  CHECK(reads<InfixG>("1+2*3"));
  CHECK(reads<InfixG>("(1+2)*3"));
  CHECK(reads<InfixG>("1"));
  CHECK(!reads<InfixG>("1+"));
  CHECK(!reads<InfixG>("+1"));

  // A primary alone is no fold: it commits no record of the infix row.
  CHECK(spans_of<InfixG, ast::Expr>("1").empty());
  // `*` binds tighter, so the multiplication is the inner fold either way.
  CHECK(spans_of<InfixG, ast::Expr>("1+2*3") == "[2,5)[0,5)");
  CHECK(spans_of<InfixG, ast::Expr>("1*2+3") == "[0,3)[0,5)");
  // Equal steps group to the left, as the table declares.
  CHECK(spans_of<InfixG, ast::Expr>("1+2+3") == "[0,3)[0,5)");
  CHECK(spans_of<InfixG, ast::Expr>("1*2*3") == "[0,3)[0,5)");

  CHECK(spans_of<InfixG, ast::Expr>("~1^2^3") == "[3,6)[1,6)");
}

}  // namespace

void run() {
  ikw_reads_either_case_and_stops_at_the_boundary();
  a_refused_word_stops_the_token_where_the_predicate_did();
  through_runs_to_its_literal_closer();
  repeat_counts_exactly();
  a_frontier_of_one_byte_refuses_the_digit_behind_it();
  two_overlapping_bytes_are_decided_by_the_state();
  infix_binds_by_its_levels();
}

}  // namespace cst_node

// Where a member was written and a keyword spelled: an out-of-range index
// clamps, a hidden keyword is skipped, an owned child is a boundary, and a
// keyword under a directive is found.
namespace cst_utils {

namespace {

using tdual::Doc;
using tdual::Item;

const cursiv::Parser& engine() {
  return support::parser_of<&tdual::generated::Lr::create_parser>();
}

enum class Engine : std::uint8_t { lr, ll };

// A document of the dual grammar, parsed by one engine: a lookup walks up from
// a node to it for the tree it searches. Both engines build the same classes,
// so one reflection serves.
cursiv::Document* dual_document(support::World<>& world, std::string_view text,
                                Engine engine) {
  support::add_language<cursiv::CoreServices>(
      world.shared, {"dual", {".dual"}, {}},
      engine == Engine::ll ? tdual::generated::Ll::create_parser()
                           : tdual::generated::Lr::create_parser(),
      tdual::generated::Lr::create_ast_reflection());
  return support::add(world.shared, "file:///lookup.dual", std::string(text));
}

struct Parsed {
  support::World<> world;
  cursiv::Document* document;
  cursiv::ParseResult& r;
  explicit Parsed(std::string_view text, Engine engine = Engine::lr)
      : document(dual_document(world, text, engine)), r(document->parseResult) {
    CHECK(r.accepted);
    CHECK(static_cast<bool>(r));
  }
  [[nodiscard]] const cursiv::AstNode& root() const { return *r.value; }
  [[nodiscard]] const Doc& doc() const { return *static_cast<Doc*>(r.value); }
};

// The record that wrote a member, by name; the walk stops at an owned child.
void a_feature_is_found_and_an_owned_child_is_a_boundary() {
  const Parsed p("doc item aa item bb");
  // `name` is written inside each Item, which owns its node: asked of the
  // root, the walk must not answer with a grandchild's member.
  CHECK(cursiv::find_nodes_for_feature(p.root(), "name").empty());
  // Asked of the Item itself, it answers.
  const std::vector<cursiv::CstNodeView> names =
      cursiv::find_nodes_for_feature(*p.doc().items[0], "name");
  CHECK(names.size() == 1);
  if (!names.empty()) CHECK(names[0].text() == "aa");
}

// A node whose one record is its keyword answers for that keyword, and its
// parent, for which that record is a boundary, does not.
void a_keyword_node_answers_for_its_own_keyword(Engine engine) {
  const Parsed p("doc mark item aa", engine);
  const auto* doc = static_cast<Doc*>(p.r.value);
  CHECK(doc != nullptr && doc->mark.has_value());
  if (doc == nullptr || !doc->mark.has_value()) return;
  const cursiv::AstNode& mark = **doc->mark;
  const std::vector<cursiv::CstNodeView> own =
      cursiv::find_nodes_for_keyword(mark, "mark");
  CHECK(own.size() == 1);
  if (!own.empty()) CHECK(own[0].id == mark.cstNodeId);
  const auto one = cursiv::find_node_for_keyword(mark, "mark");
  CHECK(one.has_value());
  if (one) CHECK(one->id == mark.cstNodeId);
  CHECK(cursiv::find_nodes_for_keyword(p.root(), "mark").empty());
  CHECK(!cursiv::find_node_for_keyword(p.root(), "mark").has_value());
  CHECK(cursiv::find_nodes_for_keyword(p.root(), "item").size() == 1);
}

// Out of range clamps to the last occurrence; nullopt stays "never at all".
void an_out_of_range_index_clamps() {
  const Parsed p("doc item aa item bb");
  const auto second = cursiv::find_node_for_keyword(p.root(), "item", 1);
  CHECK(second.has_value());
  const auto far = cursiv::find_node_for_keyword(p.root(), "item", 9);
  CHECK(far.has_value());
  if (second && far) CHECK(far->id == second->id);
  CHECK(!cursiv::find_node_for_keyword(p.root(), "absent", 0).has_value());
}

// A keyword fused under `?=` is the directive's own record, relabelled; it is
// still the word the author spelled, and the search finds it.
void a_fused_keyword_is_still_found() {
  const Parsed p("doc locked item aa");
  CHECK(static_cast<Doc*>(p.r.value)->locked);
  const std::vector<cursiv::CstNodeView> found =
      cursiv::find_nodes_for_keyword(p.root(), "locked");
  CHECK(found.size() == 1);
  if (!found.empty()) CHECK(found[0].text() == "locked");
  // The unfused neighbour is found too.
  CHECK(cursiv::find_nodes_for_keyword(p.root(), "doc").size() == 1);
}

// A word the grammar reads in either case is that keyword in any case, written
// or asked; a word read exactly is asked exactly.
void a_folded_keyword_is_found_in_any_case(Engine engine) {
  const Parsed cased("EvEnTs aa", engine);
  CHECK(cursiv::find_nodes_for_keyword(cased.root(), "events").size() == 1);
  const auto upper = cursiv::find_node_for_keyword(cased.root(), "EVENTS");
  CHECK(upper.has_value());
  if (upper) CHECK(upper->text() == "EvEnTs");
  const Parsed exact("state aa", engine);
  CHECK(cursiv::find_nodes_for_keyword(exact.root(), "state").size() == 1);
  CHECK(cursiv::find_nodes_for_keyword(exact.root(), "STATE").empty());
}

// The records strictly between two others, as maximal subtrees.
void interior_nodes_walk_the_extent() {
  const Parsed p("doc item aa item bb item cc");
  const std::vector<cursiv::CstNodeView> kws =
      cursiv::find_nodes_for_keyword(p.root(), "item");
  CHECK(kws.size() == 3);
  if (kws.size() != 3) return;
  const std::vector<cursiv::NodeId> mid =
      cursiv::interior_nodes(p.r.cst(), kws[0].id, kws[2].id);
  // Whatever sits between the first and third `item` covers the second.
  bool coversSecond = false;
  for (const cursiv::NodeId id : mid)
    coversSecond |= p.r.cst().subtree_begin(id) <= kws[1].id && kws[1].id <= id;
  CHECK(coversSecond);
  CHECK(cursiv::interior_nodes(p.r.cst(), cursiv::kNoNode, kws[2].id).empty());
}

void interior_nodes_exclude_the_endpoint_branches() {
  cursiv::Cst cst("abmcd");
  cursiv::CstBuilder builder(cst);
  const auto root = builder.enter();
  const auto left = builder.enter();
  const auto open = builder.leaf(0, 1, 0);
  builder.leaf(1, 2, 0);
  const auto leftNode = builder.exit(left, 0, 0);
  const auto middle = builder.leaf(2, 3, 0);
  const auto right = builder.enter();
  builder.leaf(3, 4, 0);
  const auto close = builder.leaf(4, 5, 0);
  builder.exit(right, 3, 0);
  builder.exit(root, 0, 0);
  CHECK((cursiv::interior_nodes(cst, open, close) ==
         std::vector<cursiv::NodeId>{middle}));
  CHECK((cursiv::interior_nodes(cst, leftNode, close) ==
         std::vector<cursiv::NodeId>{middle}));
  CHECK(cursiv::interior_nodes(cst, open, leftNode).empty());
  CHECK((cursiv::interior_nodes(cst, close, open) ==
         std::vector<cursiv::NodeId>{middle}));

  cursiv::Cst forest("ab");
  cursiv::CstBuilder roots(forest);
  const auto first = roots.leaf(0, 1, 0);
  const auto last = roots.leaf(1, 2, 0);
  CHECK(cursiv::interior_nodes(forest, first, last).empty());
}

// A hidden record is found by rule name; the leaf a caret resolves to is the
// last visible one at or before the offset, hidden ones stepped over; and
// the node that owns it is found from there.
void find_hidden_nodes_and_leaf_and_owner_answer_a_caret() {
  constexpr std::string_view kText = "doc # note\nitem aa item bb";
  const Parsed p(kText);

  const std::vector<cursiv::CstNodeView> notes =
      cursiv::find_hidden_nodes(p.root(), "NOTE");
  CHECK(notes.size() == 1);
  if (!notes.empty()) CHECK(notes[0].text() == "# note");

  const auto beforeFirstItem =
      static_cast<cursiv::TextOffset>(kText.find("item"));
  const cursiv::CstLeaf leaf =
      cursiv::find_leaf_before_offset(p.r.cst(), beforeFirstItem);
  CHECK(static_cast<bool>(leaf));
  CHECK(p.r.cst().text(leaf.id) == "doc");  // the hidden comment is skipped

  const auto insideAa = static_cast<cursiv::TextOffset>(kText.find("aa") + 2);
  cursiv::AstNode* const owner = cursiv::owner_at(p.r.cst(), insideAa);
  CHECK(owner != nullptr && cursiv::ast_cast<Item>(owner) != nullptr);

  CHECK(cursiv::owner_at(p.r.cst(), 0) == nullptr);
}

// A parsed tree names the grammar that built it and the records that
// opened a node; a tape built by hand names neither and answers nothing.
void the_tree_names_the_grammar_that_built_it() {
  const Parsed p("doc item aa");
  CHECK(p.r.cst().grammar() == &p.document->language->parser->getGrammar());
  CHECK(p.r.cst().owned().data() == p.r.nodes().data());
  CHECK(p.r.cst().owned().size() == p.r.nodes().size());

  cursiv::Cst bare("doc item aa");
  {
    cursiv::CstBuilder builder(bare);
    const cursiv::CstBuilder::Open all = builder.enter();
    builder.leaf(0, 3, cursiv::kNoElement);
    builder.exit(all, 0, cursiv::kNoElement);
  }
  CHECK(bare.grammar() == nullptr);
  cursiv::AstNode root;
  root.cstNodeId = bare.entry_root();
  CHECK(cursiv::find_nodes_for_feature(root, "name").empty());
  CHECK(!cursiv::find_node_for_feature(root, "name"));
  CHECK(cursiv::find_nodes_for_keyword(root, "doc").empty());
  CHECK(!cursiv::find_node_for_keyword(root, "doc"));
  CHECK(cursiv::find_hidden_nodes(root, "NOTE").empty());
}

// The map follows the result through a move, by construction and by
// assignment: the tree travels by pointer and the vector keeps its buffer.
void the_map_follows_the_result_through_a_move() {
  Parsed p("doc item aa item bb");
  const std::vector<cursiv::CstNodeView> before =
      cursiv::find_nodes_for_keyword(p.root(), "item");
  CHECK(before.size() == 2);

  cursiv::ParseResult moved = std::move(p.r);
  CHECK(!static_cast<bool>(p.r) && p.r.value == nullptr);
  CHECK(moved.cst().owned().data() == moved.nodes().data());

  // Held by another document, it answers as it did. Its root's parent is still
  // the first one, so the holder is named rather than walked up to.
  Parsed other("doc item cc");
  other.r = std::move(moved);
  const std::vector<cursiv::CstNodeView> after =
      cursiv::find_nodes_for_keyword(other.root(), "item", other.document);
  CHECK(after.size() == 2);
  if (before.size() == 2 && after.size() == 2)
    CHECK(before[0].id == after[0].id && before[1].id == after[1].id);
  CHECK(other.r.cst().owned().data() == other.r.nodes().data());
}

// The single answer at index zero is the list's first, whether the member is
// written by a child, by the node's own record, or never.
void index_zero_answers_what_the_list_answers() {
  const Parsed p("doc item aa item bb item cc");
  const auto* doc = static_cast<Doc*>(p.r.value);
  CHECK(doc != nullptr && doc->items.size() == 3);
  if (doc == nullptr || doc->items.size() != 3) return;

  const std::vector<cursiv::CstNodeView> keywords =
      cursiv::find_nodes_for_keyword(p.root(), "item");
  CHECK(keywords.size() == 3);
  const auto first = cursiv::find_node_for_keyword(p.root(), "item");
  const auto zero = cursiv::find_node_for_keyword(p.root(), "item", 0);
  CHECK(first.has_value() && zero.has_value() && !keywords.empty());
  if (first && zero && !keywords.empty()) {
    CHECK(first->id == keywords.front().id);
    CHECK(zero->id == keywords.front().id);
  }

  // `name` is the member the Item's own record fills: the node answers for
  // itself, in both spellings.
  const cursiv::AstNode& item = *doc->items[1];
  const std::vector<cursiv::CstNodeView> names =
      cursiv::find_nodes_for_feature(item, "name");
  const auto name = cursiv::find_node_for_feature(item, "name");
  CHECK(!names.empty() && name.has_value());
  if (!names.empty() && name) CHECK(name->id == names.front().id);
  if (name) CHECK(name->text() == "bb");

  CHECK(cursiv::find_nodes_for_feature(p.root(), "nosuch").empty());
  CHECK(!cursiv::find_node_for_feature(p.root(), "nosuch").has_value());
  CHECK(!cursiv::find_node_for_keyword(p.root(), "nosuch").has_value());
}

// Every index answers the list's clamped entry, on both sides -- including a
// member the node's own record fills, past the end.
void every_index_clamps_to_the_list() {
  const Parsed p("doc item aa item bb item cc");
  const std::vector<cursiv::CstNodeView> keywords =
      cursiv::find_nodes_for_keyword(p.root(), "item");
  CHECK(keywords.size() == 3);
  for (const std::size_t k : {0u, 1u, 2u, 3u, 99u}) {
    const auto at = cursiv::find_node_for_keyword(p.root(), "item", k);
    CHECK(at.has_value());
    if (at && !keywords.empty())
      CHECK(at->id ==
            keywords[std::min<std::size_t>(k, keywords.size() - 1)].id);
  }
  const auto* doc = static_cast<Doc*>(p.r.value);
  if (doc == nullptr || doc->items.empty()) return;
  const cursiv::AstNode& item = *doc->items[0];
  const std::vector<cursiv::CstNodeView> names =
      cursiv::find_nodes_for_feature(item, "name");
  CHECK(names.size() == 1);
  const auto past = cursiv::find_node_for_feature(item, "name", 5);
  CHECK(past.has_value());
  if (past && !names.empty()) CHECK(past->id == names.front().id);
}

// A tree with a long member list: every answer equals the same walk that asks
// the owned nodes' records, by binary search, where a construction ends.
void the_cursor_answers_what_a_search_answers() {
  std::string text = "doc mark";
  for (int i = 0; i < 200; ++i) text += " item aa";
  const Parsed p(text);
  const auto* doc = static_cast<Doc*>(p.r.value);
  CHECK(doc != nullptr && doc->items.size() == 200 && doc->mark.has_value());
  if (doc == nullptr || doc->items.size() != 200 || !doc->mark.has_value())
    return;
  const cursiv::Cst& cst = p.r.cst();

  std::vector<cursiv::NodeId> ownedIds;
  for (const cursiv::AstNode* const node : p.r.nodes())
    ownedIds.push_back(node->cstNodeId);
  // A member's writer is asked before the boundary, a keyword after it.
  const auto reference = [&](cursiv::NodeId root, auto&& matches,
                             bool writerFirst) {
    std::vector<cursiv::NodeId> out;
    const auto owned = [&](cursiv::NodeId id) {
      return std::binary_search(ownedIds.begin(), ownedIds.end(), id);
    };
    const auto walk = [&](auto&& self, cursiv::NodeId at) -> void {
      for (const cursiv::NodeId child : cst.reverse_children(at)) {
        if (!writerFirst && owned(child)) continue;
        if (matches(child)) {
          out.push_back(child);
          continue;
        }
        if (owned(child)) continue;
        if (!cst.childless(child)) self(self, child);
      }
    };
    walk(walk, root);
    std::reverse(out.begin(), out.end());
    return out;
  };
  const auto withOwn = [&](cursiv::NodeId root,
                           std::vector<cursiv::NodeId> found) {
    if (engine().getGrammar().recordFills(cst[root], "name"))
      found.insert(found.begin(), root);
    return found;
  };
  const cursiv::Grammar& grammar = engine().getGrammar();
  const auto spells = [&](cursiv::NodeId id, std::string_view word) {
    const cursiv::CstNode& n = cst[id];
    return n.subtreeSize == 1 && n.hidden == 0 && cst.text(id) == word &&
           grammar.recordRule(n).empty();
  };
  const auto keywordAt = [&](cursiv::NodeId root, std::string_view word) {
    if (spells(root, word)) return std::vector<cursiv::NodeId>{root};
    return reference(
        root, [&](cursiv::NodeId id) { return spells(id, word); }, false);
  };
  const auto name = [&](cursiv::NodeId id) {
    return grammar.recordWrites(cst[id], "name");
  };
  const auto same = [](const std::vector<cursiv::CstNodeView>& got,
                       const std::vector<cursiv::NodeId>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
      if (got[i].id != want[i]) return false;
    return true;
  };
  for (const cursiv::AstNode* const node :
       std::initializer_list<const cursiv::AstNode*>{
           p.r.value, *doc->mark, doc->items[0], doc->items[100],
           doc->items[199]}) {
    const cursiv::NodeId root = node->cstNodeId;
    for (const std::string_view word : {"item", "mark"})
      CHECK(same(cursiv::find_nodes_for_keyword(*node, word),
                 keywordAt(root, word)));
    CHECK(same(cursiv::find_nodes_for_feature(*node, "name"),
               withOwn(root, reference(root, name, true))));
  }
}

// A data rule whose text spells a keyword is not one: the rule clause is
// asked last, and it still refuses the name leaf.
void a_data_rule_that_spells_a_keyword_is_not_one() {
  const Parsed p("doc item item");
  const std::vector<cursiv::CstNodeView> found =
      cursiv::find_nodes_for_keyword(p.root(), "item");
  CHECK(found.size() == 1);
  const auto* doc = static_cast<Doc*>(p.r.value);
  CHECK(doc != nullptr && doc->items.size() == 1);
  if (doc != nullptr && doc->items.size() == 1) {
    CHECK(doc->items[0]->name == "item");
    CHECK(cursiv::find_nodes_for_keyword(*doc->items[0], "item").empty());
  }
}

}  // namespace

// Readers also accept deeply nested concrete regions with no AST ownership
// boundary. A search must not use one call-stack frame per composite.
void deep_concrete_regions_need_no_recursive_stack() {
  const Parsed parsed("doc # note\nitem aa");
  const auto notes = cursiv::find_hidden_nodes(parsed.root(), "NOTE");
  CHECK(notes.size() == 1);
  const auto stamp = notes.front().node().engine;

  cursiv::Cst cst("# note");
  cursiv::CstBuilder builder(cst);
  builder.hidden(0, 6, stamp);
  for (cursiv::NodeCount depth = 0; depth < 100000; ++depth)
    builder.stream().close(0, 6, cst.size() + 1, 0);
  builder.bind_model(engine().getGrammar(), {});
  // A document holds the hand-built tree, which owns no node: nothing stops
  // the walk.
  cursiv::Document holder(
      cursiv::TextDocument::create("file:///deep", "deep", 0, "# note"));
  holder.parseResult = cursiv::ParseResult(std::move(cst));
  cursiv::AstNode root;
  root.cstNodeId = holder.parseResult.cst().size() - 1;
  const auto found = cursiv::find_hidden_nodes(root, "NOTE", &holder);
  CHECK(found.size() == 1 && found.front().id == 0);
  CHECK(found.front().text() == "# note");
}

void run() {
  deep_concrete_regions_need_no_recursive_stack();
  a_feature_is_found_and_an_owned_child_is_a_boundary();
  an_out_of_range_index_clamps();
  a_fused_keyword_is_still_found();
  interior_nodes_walk_the_extent();
  interior_nodes_exclude_the_endpoint_branches();
  find_hidden_nodes_and_leaf_and_owner_answer_a_caret();
  the_tree_names_the_grammar_that_built_it();
  the_map_follows_the_result_through_a_move();
  index_zero_answers_what_the_list_answers();
  every_index_clamps_to_the_list();
  the_cursor_answers_what_a_search_answers();
  a_data_rule_that_spells_a_keyword_is_not_one();
  a_keyword_node_answers_for_its_own_keyword(Engine::lr);
  a_keyword_node_answers_for_its_own_keyword(Engine::ll);
  a_folded_keyword_is_found_in_any_case(Engine::lr);
  a_folded_keyword_is_found_in_any_case(Engine::ll);
}

}  // namespace cst_utils

namespace reference_resolution {

namespace {
using namespace cursiv;
using namespace std::chrono_literals;
struct Node : AstNode {};
using Ref = reference<Node>;
using Multi = multi_reference<Node>;

struct Resolver : RefResolver {
  std::function<Answer(std::uint32_t)> one;
  std::function<RefState(MultiRefItems&)> many;
  Answer resolve(std::uint32_t id) const override { return one(id); }
  RefState resolveAll(std::uint32_t, MultiRefItems& out) const override {
    return many(out);
  }
};

// Forces one reference as the linker does: a phase carrying `token`.
struct Owner : AstNode {
  const Ref* ref = nullptr;
};
constexpr RefOps kForce{.force = [](const AstNode& o, std::uint32_t) {
  return static_cast<const Owner&>(o).ref->resolve();
}};
constexpr AstMetaData kForceMeta{.refOps = &kForce, .referenceFields = {}};
void force(const Ref& ref, const CancellationToken& token) {
  Owner owner;
  owner.ref = &ref;
  DefaultDocumentReferences references(kForceMeta);
  references.handles.push_back({&owner, 0, 0});
  references.force(references.handles, token);
}

// What a stub hands back: the targets it gathered, named once at the end.
MultiRefItems named(const std::vector<MultiRefItem>& store) { return store; }

RefResolver::Answer answer(Node* target) {
  return {target, target ? RefState::Resolved : RefState::ErrorNotFound, 0};
}

template <class R>
RefState raw(const R& r) {
  return std::atomic_ref<RefState>(cursiv::detail::RefAccess::state(r))
      .load(std::memory_order_acquire);
}

// Counted in turns given away, not in seconds: a loaded machine runs the
// threads awaited as often in a turn as an idle one.
template <class R>
void observed(const R& r) {
  for (unsigned turns = 0;
       raw(r) != detail::kRefObserved && turns != (1u << 20); ++turns)
    std::this_thread::yield();
  CHECK(raw(r) == detail::kRefObserved);
}

// The steps the test has taken, for the watchdog to read.
std::atomic<unsigned> g_steps{0};
void step() { g_steps.fetch_add(1, std::memory_order_relaxed); }

void a_shared_dependency_is_computed_once() {
  Resolver resolver;
  Node target;
  std::array<Ref, 3> refs;
  for (unsigned i = 0; i < refs.size(); ++i) {
    cursiv::detail::RefAccess::resolver(refs[i]) = &resolver;
    cursiv::detail::RefAccess::handle(refs[i]) = i;
  }
  std::array<std::atomic<unsigned>, 3> calls{};
  std::latch entered(1), release(1), readers(2);
  resolver.one = [&](unsigned id) {
    ++calls[id];
    if (id == 2) {
      entered.count_down();
      release.wait();
      return answer(&target);
    }
    readers.count_down();
    return answer(refs[2].get());
  };
  std::array<Node*, 3> results{};
  std::jthread producer([&] { results[2] = refs[2].get(); });
  entered.wait();
  std::jthread a([&] { results[0] = refs[0].get(); });
  std::jthread b([&] { results[1] = refs[1].get(); });
  readers.wait();
  observed(refs[2]);
  CHECK(detail::public_ref_state(raw(refs[2])) == RefState::Resolving);
  release.count_down();
  producer.join(); a.join(); b.join();
  for (unsigned i = 0; i < refs.size(); ++i) {
    CHECK(results[i] == &target);
    CHECK(calls[i] == 1);
  }
}

// Each thread's provider reads the next thread's reference with nothing in
// hand; `nested` puts a second resolution of the same thread in the loop.
void cycles_cross_threads(unsigned count, bool nested) {
  Resolver resolver;
  const unsigned stride = nested ? 2 : 1;
  std::vector<Ref> refs(count * stride);
  for (unsigned i = 0; i < refs.size(); ++i) {
    cursiv::detail::RefAccess::resolver(refs[i]) = &resolver;
    cursiv::detail::RefAccess::handle(refs[i]) = i;
  }
  std::barrier entered(static_cast<std::ptrdiff_t>(count));
  resolver.one = [&](unsigned id) {
    if (nested && id % stride == 0) return answer(refs[id + 1].get());
    entered.arrive_and_wait();
    return answer(refs[((id / stride + 1) % count) * stride].get());
  };
  std::vector<RefState> results(count);
  std::vector<std::jthread> threads;
  for (unsigned i = 0; i < count; ++i)
    threads.emplace_back([&, i] { results[i] = refs[i * stride].resolve(); });
  for (auto& thread : threads) thread.join();
  for (const auto& r : refs) CHECK(raw(r) == RefState::ErrorCycle);
  for (const RefState result : results) CHECK(result == RefState::ErrorCycle);
}

// Every link of the loop says cycle, through either reference kind.
void a_cycle_on_one_thread_is_detected() {
  Resolver resolver;
  std::array<Ref, 3> refs;
  for (unsigned i = 0; i < refs.size(); ++i) {
    cursiv::detail::RefAccess::resolver(refs[i]) = &resolver;
    cursiv::detail::RefAccess::handle(refs[i]) = i;
  }
  resolver.one = [&](unsigned id) {
    return answer(refs[(id + 1) % refs.size()].get());
  };
  force(refs[0], kNoCancel);
  for (auto& r : refs) CHECK(raw(r) == RefState::ErrorCycle);

  // A multi reference takes the same guard.
  Ref one;
  Multi many;
  cursiv::detail::RefAccess::resolver(one) =
      cursiv::detail::RefAccess::resolver(many) = &resolver;
  resolver.one = [&](unsigned) {
    return answer(many.size() != 0 ? many.get(0) : nullptr);
  };
  resolver.many = [&](MultiRefItems&) {
    return one.get() != nullptr ? RefState::Resolved : RefState::ErrorNotFound;
  };
  CHECK(many.resolve() == RefState::ErrorCycle);
  CHECK(raw(one) == RefState::ErrorCycle && raw(many) == RefState::ErrorCycle);
}

void readers_see_a_complete_multi_reference() {
  Resolver resolver;
  Node a, b;
  Multi ref;
  cursiv::detail::RefAccess::resolver(ref) = &resolver;
  std::latch entered(1), release(1);
  unsigned calls = 0;
  std::vector<MultiRefItem> store;
  resolver.many = [&](MultiRefItems& out) {
    ++calls;
    store.push_back({&a, 0});
    entered.count_down();
    release.wait();
    store.push_back({&b, 0});
    out = named(store);
    return RefState::Resolved;
  };
  std::array<bool, 8> correct{};
  std::jthread producer([&] { (void)ref.resolve(); });
  entered.wait();
  std::vector<std::jthread> readers;
  for (unsigned i = 0; i < correct.size(); ++i)
    readers.emplace_back([&, i] {
      correct[i] = ref.size() == 2 && ref.get(0) == &a && ref.get(1) == &b;
    });
  observed(ref);
  release.count_down();
  producer.join();
  for (auto& reader : readers) reader.join();
  for (bool ok : correct) CHECK(ok);
  CHECK(calls == 1);
}

void cancelling_a_wait_unwinds_its_dependent_reference() {
  Resolver resolver;
  Node target;
  Ref a, b;
  cursiv::detail::RefAccess::resolver(a) =
      cursiv::detail::RefAccess::resolver(b) = &resolver;
  cursiv::detail::RefAccess::handle(b) = 1;
  CancellationTokenSource stop;
  const auto token = stop.get_token();
  std::latch entered(1), release(1);
  resolver.one = [&](unsigned id) {
    if (id == 0) return answer(b.get());
    entered.count_down();
    release.wait();
    return answer(&target);
  };
  std::jthread producer([&] { (void)b.resolve(); });
  entered.wait();
  bool cancelled = false;
  std::jthread reader([&] {
    try { force(a, token); }
    catch (const OperationCancelled&) { cancelled = true; }
  });
  observed(b);
  stop.request_stop();
  reader.join();
  CHECK(cancelled);
  CHECK(raw(a) == RefState::Unresolved &&
        cursiv::detail::RefAccess::target(a) == nullptr);
  release.count_down();
  producer.join();
  CHECK(a.get() == &target);
}

void a_cancelled_producer_can_be_replaced_without_duplicate_items() {
  Resolver resolver;
  Node a, b;
  Multi ref;
  cursiv::detail::RefAccess::resolver(ref) = &resolver;
  std::latch entered(1), release(1);
  std::latch secondEntered(1), secondRelease(1);
  unsigned calls = 0;
  std::vector<MultiRefItem> store;
  resolver.many = [&](MultiRefItems& out) {
    store.clear();
    store.push_back({&a, 0});
    if (++calls == 1) {
      entered.count_down();
      release.wait();
      throw OperationCancelled();
    }
    secondEntered.count_down();
    secondRelease.wait();
    store.push_back({&b, 0});
    out = named(store);
    return RefState::Resolved;
  };
  bool cancelled = false;
  std::jthread producer([&] {
    try { (void)ref.resolve(); }
    catch (const OperationCancelled&) { cancelled = true; }
  });
  entered.wait();
  std::array<std::size_t, 6> sizes{};
  std::vector<std::jthread> readers;
  for (unsigned i = 0; i < sizes.size(); ++i)
    readers.emplace_back([&, i] { sizes[i] = ref.size(); });
  observed(ref);
  release.count_down();
  producer.join();
  secondEntered.wait();
  // Readers of the cancelled computation must register again if
  // they wake after a new producer has already claimed the same reference.
  observed(ref);
  secondRelease.count_down();
  for (auto& reader : readers) reader.join();
  CHECK(cancelled && calls == 2);
  for (auto size : sizes) CHECK(size == 2);
  CHECK(ref.get(0) == &a && ref.get(1) == &b);
}

void exceptions_wake_the_readers() {
  Resolver resolver;
  Ref ref;
  cursiv::detail::RefAccess::resolver(ref) = &resolver;
  std::latch entered(1), release(1);
  resolver.one = [&](unsigned) -> RefResolver::Answer {
    entered.count_down();
    release.wait();
    throw std::runtime_error("provider failed");
  };
  bool threw = false;
  std::jthread producer([&] {
    try { (void)ref.resolve(); }
    catch (const std::runtime_error&) { threw = true; }
  });
  entered.wait();
  RefState result = RefState::Unresolved;
  std::jthread reader([&] { result = ref.resolve(); });
  observed(ref);
  release.count_down();
  producer.join(); reader.join();
  CHECK(threw && result == RefState::ErrorNotFound);
}

void rebinding_a_multi_reference_discards_its_previous_targets() {
  Resolver resolver;
  Node a, b;
  Node* target = &a;
  Multi ref;
  cursiv::detail::RefAccess::resolver(ref) = &resolver;
  std::vector<MultiRefItem> store;
  resolver.many = [&](MultiRefItems& out) {
    store.assign(1, {target, 0});
    out = named(store);
    return RefState::Resolved;
  };
  cursiv::detail::RefAccess::bind(ref, kNoNode, "a");
  CHECK(ref.size() == 1 && ref.get(0) == &a);
  target = &b;
  cursiv::detail::RefAccess::bind(ref, kNoNode, "b");
  CHECK(ref.size() == 1 && ref.get(0) == &b);
}

// The phase is cancelled between the linker's poll and the wait under it.
void an_already_cancelled_wait_never_registers_a_callback_under_lock() {
  Resolver resolver;
  Node target;
  Ref ref, outer;
  cursiv::detail::RefAccess::resolver(ref) =
      cursiv::detail::RefAccess::resolver(outer) = &resolver;
  cursiv::detail::RefAccess::handle(outer) = 1;
  CancellationTokenSource stop;
  std::latch entered(1), release(1);
  resolver.one = [&](unsigned id) {
    if (id == 1) {
      stop.request_stop();
      return answer(ref.get());
    }
    entered.count_down();
    release.wait();
    return answer(&target);
  };
  std::jthread producer([&] { (void)ref.resolve(); });
  entered.wait();
  bool cancelled = false;
  try { force(outer, stop.get_token()); }
  catch (const OperationCancelled&) { cancelled = true; }
  CHECK(cancelled && raw(outer) == RefState::Unresolved);
  release.count_down(); producer.join();
}

// Two threads, one link plural: the loop is found through either reference.
void a_cycle_across_threads_through_a_multi_reference() {
  Resolver resolver;
  Ref one;
  Multi many;
  cursiv::detail::RefAccess::resolver(one) =
      cursiv::detail::RefAccess::resolver(many) = &resolver;
  std::barrier entered(2);
  resolver.one = [&](unsigned) {
    entered.arrive_and_wait();
    return answer(many.size() != 0 ? many.get(0) : nullptr);
  };
  resolver.many = [&](MultiRefItems&) {
    entered.arrive_and_wait();
    return one.get() != nullptr ? RefState::Resolved : RefState::ErrorNotFound;
  };
  RefState first = RefState::Unresolved;
  RefState second = RefState::Unresolved;
  {
    std::jthread a([&] { first = one.resolve(); });
    std::jthread b([&] { second = many.resolve(); });
  }
  CHECK(first == RefState::ErrorCycle && second == RefState::ErrorCycle);
}

// References whose `RefResolver` reads each one's out-edges: a DAG resolves, a
// ring is a cycle.
struct Graph : RefResolver {
  std::vector<Ref> refs;
  std::vector<std::vector<unsigned>> edges;
  Node target;
  mutable std::atomic<unsigned> cycles{0};

  explicit Graph(unsigned n) : refs(n), edges(n) {
    for (unsigned i = 0; i < n; ++i) {
      cursiv::detail::RefAccess::resolver(refs[i]) = this;
      cursiv::detail::RefAccess::handle(refs[i]) = i;
    }
  }
  Answer resolve(std::uint32_t id) const override {
    bool failed = false;
    for (const unsigned child : edges[id]) {
      std::this_thread::yield();
      const RefState s = refs[child].resolve();
      if (s == RefState::ErrorCycle) ++cycles;
      failed = failed || s != RefState::Resolved;
    }
    if (failed) return {};
    return {const_cast<Node*>(&target), RefState::Resolved, 0};
  }
  RefState resolveAll(std::uint32_t, MultiRefItems&) const override {
    return RefState::ErrorNotFound;
  }
};

// Threads started at random links, half of them under a phase that may be
// cancelled while they wait on each other.
void race_through(const Graph& graph, std::mt19937& rng, unsigned threads,
                  bool cancel) {
  step();
  CancellationTokenSource stop;
  std::vector<std::jthread> running;
  for (unsigned t = 0; t < threads; ++t) {
    const Ref& start = graph.refs[rng() % graph.refs.size()];
    const bool rooted = rng() % 2 == 0;
    running.emplace_back([&stop, &start, rooted] {
      try {
        if (rooted)
          force(start, stop.get_token());
        else
          (void)start.resolve();
      } catch (const OperationCancelled&) {
      }
    });
  }
  if (cancel) {
    std::this_thread::sleep_for(std::chrono::microseconds(rng() % 200));
    stop.request_stop();
  }
}

// However the threads interleave in a DAG, no read answers a cycle: a false one
// would be kept for good.
void threads_through_a_dag_never_see_a_cycle() {
  std::mt19937 rng(7);
  // Cover every graph size and worker count. The latch and barrier cases
  // below force the specific wait and cancellation interleavings.
  for (unsigned round = 0; round < 21; ++round) {
    const unsigned n = 6 + round % 20;
    Graph graph(n);
    for (unsigned i = 0; i < n; ++i)
      for (unsigned j = i + 1; j < n; ++j)
        if (rng() % 4 == 0) graph.edges[i].push_back(j);
    race_through(graph, rng, 2 + round % 7, round % 3 == 0);
    for (const Ref& r : graph.refs) CHECK(!detail::ref_running(raw(r)));
    for (const Ref& r : graph.refs) CHECK(r.resolve() == RefState::Resolved);
    CHECK(graph.cycles.load() == 0);
  }
}

// Around a ring, every link ends a cycle; a cancelled wait is no link of it,
// so a cancelled round ends in errors that are read again as one.
void threads_around_a_ring_all_see_the_cycle() {
  std::mt19937 rng(11);
  // Seven sizes and three cancellation phases complete a cycle in 21 rounds.
  for (unsigned round = 0; round < 21; ++round) {
    const unsigned n = 2 + round % 7;
    Graph graph(n);
    for (unsigned i = 0; i < n; ++i) graph.edges[i].push_back((i + 1) % n);
    const bool cancel = round % 3 == 0;
    race_through(graph, rng, 2 + round % 7, cancel);
    for (const Ref& r : graph.refs) CHECK(!detail::ref_running(raw(r)));
    for (const Ref& r : graph.refs) {
      const RefState s = r.resolve();
      CHECK(cancel ? has_error(s) : s == RefState::ErrorCycle);
    }
  }
}

}  // namespace

void run() {
  // A test that takes no step for 15 s is stalled, loaded or not; the
  // watchdog reports it even when run directly, outside ctest's timeout.
  std::jthread watchdog([](std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    for (unsigned seen = g_steps.load();;) {
      wake.wait_for(lock, stop, 15s, [] { return false; });
      if (stop.stop_requested()) return;
      if (g_steps.load() == seen) {
        std::fputs("syntax_tree: no step in 15 s\n", stderr);
        std::abort();
      }
      seen = g_steps.load();
    }
  });
  const auto then = [](auto&& test) {
    test();
    step();
  };
  then(a_shared_dependency_is_computed_once);
  then(a_cycle_on_one_thread_is_detected);
  then(readers_see_a_complete_multi_reference);
  then(cancelling_a_wait_unwinds_its_dependent_reference);
  then(exceptions_wake_the_readers);
  then(rebinding_a_multi_reference_discards_its_previous_targets);
  then(an_already_cancelled_wait_never_registers_a_callback_under_lock);
  then(threads_through_a_dag_never_see_a_cycle);
  then(threads_around_a_ring_all_see_the_cycle);
  // Each scenario forces its interleaving; keep thread startup bounded.
  for (unsigned i = 0; i < 5; ++i) {
    cycles_cross_threads(2, false);
    cycles_cross_threads(3, true);
    a_cancelled_producer_can_be_replaced_without_duplicate_items();
    a_cycle_across_threads_through_a_multi_reference();
    step();
  }
}

}  // namespace reference_resolution

namespace node_on_its_own {

// A node with another base, made on its own, keeps its members and is freed
// through its `AstNode`.
struct Plain {
  int before = 7;
};
struct Later : cursiv::Extends<Plain, cursiv::AstNode> {
  int after = 3;
};

void run() {
  const cursiv::AstNodePtr node = cursiv::detail::new_ast_node<Later>();
  CHECK(node->type == &cursiv::ast_type_tag<Later>);
  CHECK(node->parent == nullptr && node->cstNodeId == cursiv::kNoNode);
  const auto* const later = static_cast<const Later*>(node.get());
  CHECK(later->before == 7 && later->after == 3);
}

}  // namespace node_on_its_own

namespace block_arena {

// Wasm linear memory, as an allocator without coalescing sees it: it only
// grows, and a freed range serves again only a request that fits in it. The
// bytes come from the heap; only their place in that memory is modelled.
class LinearMemory final : public std::pmr::memory_resource {
 public:
  std::size_t peak = 0;

 private:
  void* do_allocate(std::size_t bytes, std::size_t align) override {
    // Out of memory well before the address space is.
    if (bytes > std::numeric_limits<std::size_t>::max() / 4)
      throw std::bad_alloc();
    // The smallest freed range that holds it, else new memory at the top.
    auto range = bytes;
    if (const auto fit = _free.lower_bound(bytes); fit != _free.end()) {
      range = *fit;
      _free.erase(fit);
    } else {
      _top += bytes;
      peak = std::max(peak, _top);
    }
    auto* p = std::pmr::new_delete_resource()->allocate(bytes, align);
    _placed.emplace(p, range);
    return p;
  }
  void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
    const auto range = _placed.find(p);
    _free.insert(range->second);
    _placed.erase(range);
    std::pmr::new_delete_resource()->deallocate(p, bytes, align);
  }
  [[nodiscard]] bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::size_t _top = 0;
  std::multiset<std::size_t> _free;
  // Each live request, and the size of the range it took.
  std::map<void*, std::size_t> _placed;
};

// One parse of a document `step` times larger: the AST's 64 KiB chunks around
// the syntax tree's records, one array sized from the text, all from one pool.
std::size_t parse(std::pmr::memory_resource& pool, std::size_t step) {
  constexpr std::size_t kChunk = std::size_t{64} << 10;
  const auto records = step * (std::size_t{700} << 10);
  for (std::size_t i = 0; i < 4 * step; ++i) (void)pool.allocate(kChunk, 16);
  (void)pool.allocate(records, 16);
  for (std::size_t i = 0; i < 4 * step; ++i) (void)pool.allocate(kChunk, 16);
  return 8 * step * kChunk + records;
}

// A document that grows at every reparse needs little more memory than
// its largest parse.
void a_growing_document_reuses_its_blocks() {
  LinearMemory memory;
  cursiv::detail::BlockArena pool(std::size_t{64} << 10, &memory);
  std::size_t largest = 0;
  for (std::size_t step = 1; step <= 40; ++step) {
    largest = std::max(largest, parse(pool, step));
    pool.release();
  }
  CHECK(memory.peak <= 2 * largest);
}

// A request whose block header and alignment do not fit beside it in a
// size_t is refused before any block is taken, and a large one is never
// taken from the current block by a pointer sum that wraps around.
void a_request_past_the_address_space_is_refused() {
  LinearMemory memory;
  cursiv::detail::BlockArena pool(std::size_t{64} << 10, &memory);
  const auto refused = [&pool](std::size_t bytes) {
    try {
      (void)pool.allocate(bytes, 16);
    } catch (const std::bad_alloc&) {
      return true;
    }
    return false;
  };
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  CHECK(refused(kMax - 16));
  (void)pool.allocate(64, 16);
  CHECK(refused(kMax - 16));
  // Past the largest power of two: asked as is, which the memory refuses.
  CHECK(refused(kMax / 2 + 2));
  // ...and the pool still allocates what it can hold.
  CHECK(reinterpret_cast<std::uintptr_t>(pool.allocate(64, 16)) % 16 == 0);
}

// A run that outgrows a block of its own gives it back: the nodes that follow
// the records fit in what it left.
void an_outgrown_run_gives_its_buffer_back() {
  LinearMemory memory;
  cursiv::detail::BlockArena pool(std::size_t{64} << 10, &memory);
  cursiv::CstRun run;
  run.bind(&pool);
  for (cursiv::TextOffset i = 0; i < (1u << 21); ++i) run.leaf(i, i + 1, 0);
  const std::size_t peak = memory.peak;
  for (int i = 0; i < 256; ++i) (void)pool.allocate(std::size_t{64} << 10, 8);
  CHECK(memory.peak == peak);
}

void run() {
  a_growing_document_reuses_its_blocks();
  a_request_past_the_address_space_is_refused();
  an_outgrown_run_gives_its_buffer_back();
}

}  // namespace block_arena

int main() {
  const auto run = [](const char* section, void (*test)()) {
    // Keep the current section visible even if CTest terminates the process.
    std::fprintf(stderr, "syntax_tree: %s\n", section);
    test();
  };
  run("ast_builder", ast_builder::run);
  run("ast_node", ast_node::run);
  run("ast_reflection", ast_reflection::run);
  run("convert", convert::run);
  run("cst", cst::run);
  run("node_on_its_own", node_on_its_own::run);
  run("cst_node", cst_node::run);
  run("cst_utils", cst_utils::run);
  run("reference_resolution", reference_resolution::run);
  run("block_arena", block_arena::run);
  return cursiv::test::report("syntax_tree");
}
