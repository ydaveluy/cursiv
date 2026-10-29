#include <atomic>
#include <stdexcept>
#include <thread>
#include "cursiv/engines/ll/runtime/LlEpisodeMemo.hpp"
// What the descent owes: the tree the table engine writes for the same
// language, a tree for a refused document, a cancellation answered where it
// stands, a nesting deeper than any machine stack read like any other, and a
// precedence table as its grammar said.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "indirectfoldll/generated/Ll/Module.hpp"
#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/ll/runtime/LlLanguageParser.hpp"
#include "cursiv/engines/ll/runtime/LlPredictionSession.hpp"
#include "cursiv/engines/ll/runtime/LlReturnStack.hpp"
#include "cursiv/engines/ll/runtime/ll_caret.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_repair.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/runtime/AstConvThunks.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ParityGrammar.hpp"
#include "engines/ll/TagGrammar.hpp"
#include "menull/generated/Ll/Module.hpp"
#include "menulr/generated/Lr/Module.hpp"
#include "parityll/generated/Ll/Grammar.hpp"
#include "parityll/generated/Ll/Module.hpp"
#include "parityll/generated/Ll/Table.hpp"
#include "paritylr/generated/Lr/Module.hpp"
#include "tagll/generated/Ll/Module.hpp"
#include "taglr/generated/Lr/Module.hpp"

namespace {

namespace ast_cancellation {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
thread_local cursiv::CancellationTokenSource* stop = nullptr;
thread_local unsigned conversions = 0;
using Converter = cursiv::detail::text_conv<[](std::string_view text) {
  ++conversions;
  if (stop != nullptr) stop->request_stop();
  return text;
}>;

cursiv::AstNode* make(cursiv::AstBuildCtx& context, cursiv::NodeId id) {
  auto* node = static_cast<Node*>(cursiv::detail::make_node<Node>(context, id));
  CHECK(cursiv::detail::invoke_conv<Converter, std::string_view, 0, false>(
      context, id, &node->text));
  return node;
}

void entry(cursiv::AstReducer& builder, cursiv::NodeId id) {
  builder.push_made(id, builder.create<make, nullptr, false>(0, id));
}

const cursiv::AstMetaData metadata{};
const cursiv::detail::MakeFn makes[] = {make};
const cursiv::AstEntryFn entries[] = {entry};
const cursiv::AstActions actions{
    .meta = &metadata, .makes = makes, .entries = entries, .entryCount = 1};

cursiv::Cst tape(std::string_view text) {
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  const auto opened = builder.enter();
  for (cursiv::TextOffset at = 0; at < text.size(); ++at)
    builder.leaf(at, at + 1u, cursiv::StampLayout{}.make(0, 0));
  const auto root =
      builder.exit(opened, text.size(), cursiv::StampLayout{}.make(0, 0));
  builder.bind_entry(0, tree.size(), root);
  builder.bind_accepted(true);
  return tree;
}

void drive(cursiv::LlContext& context) {
  context.last =
      context.tape.leaf(0, context.len, cursiv::StampLayout{}.make(0, 0));
  context.at = context.len;
}

}  // namespace ast_cancellation

void ast_conversion_observes_cancellation_before_return() {
  using namespace ast_cancellation;
  // A converter requests the stop and returns a value normally. Observing it
  // is the builder's job, including when the conversion is the final action.
  for (const std::string& text : {std::string("x"), std::string(512, 'x')}) {
    cursiv::CancellationTokenSource source;
    stop = &source;
    conversions = 0;
    bool threw = false;
    try {
      (void)cursiv::AstReducer::build<cursiv::StampLayout{}>(
          actions, tape(text), source.get_token());
    } catch (const cursiv::OperationCancelled&) {
      threw = true;
    }
    stop = nullptr;
    CHECK(source.stop_requested() && threw);
    CHECK(conversions != 0);
  }

  cursiv::CancellationTokenSource source;
  source.request_stop();
  conversions = 0;
  bool threw = false;
  try {
    (void)cursiv::AstReducer::build<cursiv::StampLayout{}>(actions, tape("x"),
                                                           source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw && conversions == 0);

  const auto complete =
      cursiv::AstReducer::build<cursiv::StampLayout{}>(actions, tape("x"));
  CHECK(complete.value != nullptr &&
        complete.error == cursiv::AstBuildError::none);
}

void the_parser_propagates_cancellation_to_ast_materialisation() {
  using namespace ast_cancellation;
  const cursiv::LlLanguageParser<tpar::generated::Ll::kTable> parser(
      tpar::generated::Ll::kGrammarMetaData, actions, drive);
  cursiv::CancellationTokenSource source;
  stop = &source;
  conversions = 0;
  bool threw = false;
  try {
    (void)parser.parse("x", source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  stop = nullptr;
  CHECK(source.stop_requested() && threw && conversions == 1);
  const auto complete = parser.parse("x");
  CHECK(complete.value != nullptr && complete.diagnostics.empty());
}

void ast_materialisation_rejects_an_invalid_entry_extent() {
  for (const cursiv::NodeCount extent : {0u, 2u}) {
    cursiv::Cst tree("x");
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    const auto root = builder.leaf(0, 1, cursiv::StampLayout{}.make(0, 0));
    builder.stream().at(root).subtreeSize = extent;
    builder.bind_entry(0, tree.size(), root);
    builder.bind_accepted(true);
    const auto result = cursiv::AstReducer::build<cursiv::StampLayout{}>(
        ast_cancellation::actions, std::move(tree));
    CHECK(result.error == cursiv::AstBuildError::malformed_cst &&
          result.value == nullptr && !result.diagnostics.empty());
  }
}

void ast_with_spans(const cursiv::AstReflection& reflection,
                    const cursiv::Cst& tree, const cursiv::AstNode& node,
                    std::string& text) {
  text += reflection.getTypeMetaData(node.type).name;
  CHECK(node.cstNodeId < tree.size());
  if (node.cstNodeId >= tree.size()) return;
  const auto& span = tree[node.cstNodeId];
  text +=
      '[' + std::to_string(span.begin) + ',' + std::to_string(span.end) + "]{";
  reflection.visitMembers(node,
                          [&](std::string_view member, std::uint32_t index,
                              cursiv::AstNode& child) {
                            text += member;
                            if (index != cursiv::AstReflection::kNoIndex)
                              text += std::to_string(index);
                            text += '=';
                            ast_with_spans(reflection, tree, child, text);
                            text += ' ';
                          });
  text += '}';
}

// Call(ID '(' ... ')') and Name(ID) deliberately share a scalar prefix.
// Unlike an owning node prefix, this choice cannot become an optional Nest;
// tests of the prediction cache therefore meet the decision before that ID.
std::uint16_t name_or_call_decision() {
  const cursiv::LlLowCfg& cfg = cursiv::LlLowering<tpar::Ll>::table().cfg;
  for (std::uint16_t i = 0; i < cfg.ndecisions; ++i)
    if (cfg.rule_name(cfg.decisions[i].rule) == "Primary" &&
        !cfg.repeats(cfg.decisions[i]))
      return i;
  return cursiv::kNoRow;
}

// The choice of a document, asked at a place of it: a context that stands
// there with nothing consumed, whose plan is the parse's.
template <const auto& table>
struct PredictionAt {
  PredictionAt(std::string_view source, std::uint16_t choice)
      : text(source),
        tree(text),
        builder(tree),
        context((builder.bind_stream(), text), builder),
        decision(choice) {}

  std::uint16_t operator()(cursiv::TextOffset at) {
    context.at = at;
    context.triviaAt = at;
    context.laValid = false;
    context.laMode = cursiv::kInvalidSymbol;
    cursiv::ll_peek<table>(context, table.decisions[decision].mode);
    return cursiv::ll_episode<table>(context, decision);
  }

  std::string_view text;
  cursiv::Cst tree;
  cursiv::CstBuilder builder;
  cursiv::LlContext context;
  std::uint16_t decision;
};

// A prediction that saw the end of the document cannot answer for the same
// spelling with text after it: each answer is the document's, and the end is
// a place of it.
void predictions_keep_the_end_they_depended_on() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  const auto decision = name_or_call_decision();
  CHECK(decision != cursiv::kNoRow);
  if (decision == cursiv::kNoRow) return;
  cursiv::LlPredictionSession<table> session;
  PredictionAt<table> at("name;name();name", decision);
  const auto name = at(0);
  const auto call = at(5);
  const auto incomplete = at(12);
  CHECK(name != cursiv::kNoRow && call != cursiv::kNoRow && name != call);
  CHECK(incomplete != name);
  for (unsigned round = 0; round < 2; ++round) {
    CHECK(at(12) == incomplete);
    CHECK(at(0) == name);
    CHECK(at(5) == call);
  }

  const auto parser = tpar::generated::Ll::create_parser();
  for (const std::string_view text : {"show name;", "show name();"}) {
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* unit = result.value_as<tpar::ast::Unit>();
    CHECK(unit != nullptr && unit->shows.size() == 1);
    if (unit == nullptr || unit->shows.size() != 1) continue;
    CHECK(unit->shows[0]->items.size() == 1);
    if (unit->shows[0]->items.size() != 1) continue;
    const auto* value = unit->shows[0]->items[0]->value;
    if (text == "show name;") {
      const auto* node = cursiv::ast_cast<tpar::ast::Name>(value);
      CHECK(node != nullptr && node->id == "name");
    } else {
      const auto* node = cursiv::ast_cast<tpar::ast::Call>(value);
      CHECK(node != nullptr && node->name == "name" && node->args.empty());
    }
  }
}

// Lookahead can carry more return frames than the live descent. Losing a
// known continuation there introduces unrelated callers and changes which
// token the exact simulation accepts.
void deep_prediction_keeps_its_known_continuation() {
  const cursiv::LlAtnState states[] = {
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.edge = 0, .nedges = 1},
      {.edge = 1, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1},
      {.edge = 2, .nedges = 1},
  };
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 0},
      {.kind = cursiv::LlEdgeKind::atom, .label = 42, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 43, .target = 3},
  };
  const std::uint32_t callerBase[] = {0, 1, 1};
  const std::uint32_t callerAt[] = {4};
  const cursiv::LlAtnView atn{
      .states = states,
      .nstates = 5,
      .edges = edges,
      .callerBase = callerBase,
      .callerAt = callerAt,
  };
  cursiv::LlSimulation simulation(atn);
  auto& contexts = simulation.contexts();
  auto stack = contexts.push(cursiv::kLlCtxNone, 2);
  for (unsigned i = 0; i < 1536; ++i) stack = contexts.push(stack, 1);
  simulation.seed(0, stack);
  bool actual = false, unrelated = false;
  for (const auto& config : simulation.configs()) {
    actual |= config.state == 2;
    unrelated |= config.state == 4;
  }
  CHECK(actual && !unrelated);
  CHECK(simulation.step(42));
  CHECK(simulation.settled() == 0);
}

// The room a larger walk grew is given back for the next, and the table
// answers as before.
void a_table_gives_back_the_room_a_larger_walk_left() {
  cursiv::LlStamped<std::uint64_t, cursiv::LlKeyHash> keys;
  bool fresh = false;
  for (std::uint64_t k = 0; k < 100000; ++k)
    keys.at(k, fresh) = static_cast<std::uint32_t>(k);
  keys.clear();
  keys.trim();
  for (std::uint64_t k = 0; k < 10; ++k)
    keys.at(k, fresh) = static_cast<std::uint32_t>(k + 1);
  CHECK(keys.find(3) != nullptr && *keys.find(3) == 4);
  CHECK(keys.find(50000) == nullptr);
  keys.clear();
  keys.trim();
  CHECK(keys.find(3) == nullptr);
}

// A walk gives the pool back as it found it: what it made is gone, what was
// there stands, and the same walk made again makes the same nodes.
void a_pool_gives_back_what_a_walk_made() {
  cursiv::LlCtxPool pool;
  const auto base = pool.push(cursiv::kLlCtxNone, 1);
  const auto kept = pool.push(base, 2);
  const auto third = pool.push(kept, 3);
  const auto walk = [&](unsigned round) {
    std::vector<std::uint32_t> made;
    auto node = kept;
    for (unsigned i = 0; i < 300; ++i) {
      node = pool.push(node, 10 + (i * round) % 9);
      made.push_back(node);
    }
    const auto joined = pool.merge(made[10], made[200]);
    CHECK(joined == cursiv::kLlCtxApart || joined > third);
    CHECK(pool.push(base, 2) == kept);
    return made;
  };
  for (unsigned round = 0; round < 40; ++round) {
    CHECK(pool.begin_scratch() && !pool.begin_scratch());
    const auto made = walk(round);
    pool.end_scratch();
    CHECK(pool.push(cursiv::kLlCtxNone, 1) == base);
    CHECK(pool.push(base, 2) == kept);
    CHECK(pool.push(kept, 3) == third);
    CHECK(pool.begin_scratch());
    CHECK(walk(round) == made);
    pool.end_scratch();
  }
}

// Alias calls may join a cell opened before their own caller. Creation order
// must not make two different continuation sets indistinguishable.
void call_cells_are_sealed_after_their_actual_dependencies() {
  using namespace cursiv;

  // Root = A | B; A = Z | R b | R c; B = Z | S d | S e;
  // Z = Q a; R = Q; S = Q; Q = x. The shared Z continuation is state 5.
  LlAtnState s[21]{};
  for (auto& state : s) state.rule = 0;
  s[8].kind = LlAtnKind::ruleStop;
  s[10].rule = s[11].rule = 1;
  s[11].kind = LlAtnKind::ruleStop;
  s[12].rule = s[13].rule = 2;
  s[13].kind = LlAtnKind::ruleStop;
  s[19].rule = s[20].rule = 3;
  s[20].kind = LlAtnKind::ruleStop;
  const LlAtnEdge e[] = {
      {.kind = LlEdgeKind::epsilon, .target = 1},
      {.kind = LlEdgeKind::epsilon, .target = 14},
      {.kind = LlEdgeKind::epsilon, .target = 2},
      {.kind = LlEdgeKind::epsilon, .target = 3},
      {.kind = LlEdgeKind::epsilon, .target = 4},
      {.kind = LlEdgeKind::rule, .label = 1, .target = 10, .follow = 5},
      {.kind = LlEdgeKind::rule, .label = 2, .target = 12, .follow = 6},
      {.kind = LlEdgeKind::rule, .label = 2, .target = 12, .follow = 7},
      {.kind = LlEdgeKind::atom, .label = 2, .target = 8},
      {.kind = LlEdgeKind::atom, .label = 3, .target = 8},
      {.kind = LlEdgeKind::atom, .label = 4, .target = 8},
      {.kind = LlEdgeKind::atom, .label = 5, .target = 8},
      {.kind = LlEdgeKind::atom, .label = 1, .target = 11},
      {.kind = LlEdgeKind::rule, .label = 1, .target = 10, .follow = 13},
      {.kind = LlEdgeKind::epsilon, .target = 2},
      {.kind = LlEdgeKind::epsilon, .target = 15},
      {.kind = LlEdgeKind::epsilon, .target = 16},
      {.kind = LlEdgeKind::rule, .label = 3, .target = 19, .follow = 17},
      {.kind = LlEdgeKind::rule, .label = 3, .target = 19, .follow = 18},
      {.kind = LlEdgeKind::atom, .label = 6, .target = 8},
      {.kind = LlEdgeKind::atom, .label = 7, .target = 8},
      {.kind = LlEdgeKind::rule, .label = 1, .target = 10, .follow = 20}};
  s[0].edge = 0;
  s[0].nedges = 2;
  s[0].decision = 0;
  s[1].edge = 2;
  s[1].nedges = 3;
  s[1].decision = 1;
  for (unsigned i = 2; i < 8; ++i) {
    s[i].edge = i + 3;
    s[i].nedges = 1;
  }
  s[9].edge = 11;
  s[9].nedges = 1;
  s[10].edge = 12;
  s[10].nedges = 1;
  s[12].edge = 13;
  s[12].nedges = 1;
  s[14].edge = 14;
  s[14].nedges = 3;
  s[14].decision = 2;
  for (unsigned i = 15; i <= 19; ++i) {
    s[i].edge = i + 2;
    s[i].nedges = 1;
  }
  const unsigned starts[] = {0, 10, 12, 19}, callBase[] = {0, 0, 3, 5, 7},
                 calls[] = {5, 13, 20, 6, 7, 17, 18};
  unsigned classes[21];
  for (unsigned i = 0; i < 21; ++i) classes[i] = i;
  classes[13] |= kLlTailReturn;
  classes[20] |= kLlTailReturn;
  const LlAtnView atn{.states = s,
                      .nstates = 21,
                      .edges = e,
                      .ruleStart = starts,
                      .callerBase = callBase,
                      .callerAt = calls,
                      .returnClass = classes};
  LlReturnPool pool;
  pool.read_returns_by_class(classes);
  LlEpisodeSim sim(atn, pool, false);
  const std::uint8_t shared[] = {0, 1, 1, 1};
  sim.share_calls(shared);
  for (unsigned round = 0; round < 2; ++round) {
    sim.reset();
    std::vector<LlThread> threads;
    sim.seed(0, kLlCtxNone, threads);
    CHECK(threads.size() == 2);
    if (threads.size() != 2) return;
    CHECK(sim.stacks().canon(threads[0].stack) !=
          sim.stacks().canon(threads[1].stack));
    CHECK(!ll_stuck_apart(sim, threads));
    for (const auto term : {std::uint16_t{1}, std::uint16_t{6}}) {
      std::vector<LlThread> origins, next;
      for (const auto& t : threads)
        sim.advance(t, &term, 1, 0, LlEpisodeSim::kStart, origins);
      sim.close(origins, next);
      threads.swap(next);
    }
    CHECK(!threads.empty());
    for (const auto& t : threads) {
      CHECK(t.root == 1);
    }
  }
}

// Only two frontier threads remain over this deep shared graph. Asking for
// its first path or common prefix must not use one native frame per cell.
void deep_cell_paths_keep_their_order_and_stop_at_the_first_refusal() {
  using namespace cursiv;
  // R_i = R_(i+1) | (R_(i+1) 'c'); R_N = 'x'.
  for (const unsigned n : {12u, 10000u}) {
    std::vector<LlAtnState> states(5 * (n + 1));
    std::vector<LlAtnEdge> edges;
    std::vector<unsigned> starts(n + 1), classes(states.size());
    for (unsigned i = 0; i < states.size(); ++i) classes[i] = i;
    for (unsigned r = 0; r <= n; ++r) {
      unsigned s = 5 * r;
      starts[r] = s;
      for (unsigned j = 0; j < 5; ++j)
        states[s + j].rule = static_cast<std::uint16_t>(r);
      states[s + 4].kind = LlAtnKind::ruleStop;
      classes[s + 4] |= kLlTailReturn;
      if (r == n) {
        states[s].edge = edges.size();
        states[s].nedges = 1;
        edges.push_back(
            {.kind = LlEdgeKind::atom, .label = 1, .target = s + 4});
        continue;
      }
      states[s].edge = edges.size();
      states[s].nedges = 2;
      states[s].decision = static_cast<std::uint16_t>(r);
      edges.push_back({.kind = LlEdgeKind::epsilon, .target = s + 1});
      edges.push_back({.kind = LlEdgeKind::epsilon, .target = s + 2});
      states[s + 1].edge = edges.size();
      states[s + 1].nedges = 1;
      edges.push_back({.kind = LlEdgeKind::rule,
                       .label = static_cast<std::uint16_t>(r + 1),
                       .target = s + 5,
                       .follow = s + 4});
      states[s + 2].edge = edges.size();
      states[s + 2].nedges = 1;
      edges.push_back({.kind = LlEdgeKind::rule,
                       .label = static_cast<std::uint16_t>(r + 1),
                       .target = s + 5,
                       .follow = s + 3});
      states[s + 3].edge = edges.size();
      states[s + 3].nedges = 1;
      edges.push_back({.kind = LlEdgeKind::atom, .label = 2, .target = s + 4});
    }
    const LlAtnView atn{.states = states.data(),
                        .nstates = static_cast<std::uint32_t>(states.size()),
                        .edges = edges.data(),
                        .ruleStart = starts.data(),
                        .returnClass = classes.data()};
    LlReturnPool pool;
    pool.read_returns_by_class(classes.data());
    LlEpisodeSim sim(atn, pool, false);
    std::vector<std::uint8_t> shared(n + 1, 1);
    shared[0] = 0;
    sim.share_calls(shared.data());
    std::vector<LlThread> threads;
    sim.seed(0, kLlCtxNone, threads);

    CHECK(threads.size() == 2);
    for (const auto& thread : threads) {
      CHECK(sim.standing(thread.stack) == 2);
      unsigned visited = 0;
      LlHistory::Node first = LlHistory::kRoot;
      const bool complete =
          sim.each_arm(thread.stack, thread.arm, [&](auto arm) {
            ++visited;
            first = arm;
            return false;
          });
      CHECK(!complete && visited == 1 && first == thread.arm);
      if (n == 12) {
        unsigned paths = 0;
        CHECK(sim.each_arm(thread.stack, thread.arm, [&](auto) {
          ++paths;
          return true;
        }));
        CHECK(paths == (1u << (n - 1)));
      }
      LlHistory::Node common = LlHistory::kRoot;
      bool met = false;
      CHECK(sim.meet_arms(thread.stack, thread.arm, common, met, 0));
      CHECK(met && sim.arms().depth(common) == 1);
      if (sim.arms().depth(common) == 1)
        CHECK(sim.arms().event(common, 0).word == thread.root);
    }
  }
}

struct CountedContextKey {
  std::uint64_t value;
  static inline std::uint64_t comparisons = 0;
  bool operator==(const CountedContextKey& other) const {
    ++comparisons;
    return value == other.value;
  }
};
struct CountedContextHash {
  auto operator()(const CountedContextKey& key) const {
    return cursiv::LlKeyHash{}(key.value);
  }
};

void context_keys_keep_both_halves_on_every_platform() {
  constexpr unsigned count = 8192;
  cursiv::LlStamped<CountedContextKey, CountedContextHash> keys;
  CountedContextKey::comparisons = 0;
  for (unsigned i = 0; i < count; ++i) {
    bool fresh = false;
    keys.at({(std::uint64_t{i} << 32) | 999999u}, fresh) = i + 1;
    CHECK(fresh);
  }
  CHECK(CountedContextKey::comparisons < 8u * count);
  for (unsigned i = 0; i < count; ++i) {
    const auto* value = keys.find({(std::uint64_t{i} << 32) | 999999u});
    CHECK(value != nullptr && *value == i + 1);
  }
  CHECK(CountedContextKey::comparisons < 16u * count);
}

void deep_context_joins_keep_their_canonical_stacks() {
  cursiv::LlCtxPool pool;
  auto a = pool.push(cursiv::kLlCtxNone, 1);
  auto b = pool.push(cursiv::kLlCtxNone, 2);
  constexpr unsigned depth = 20000;
  for (unsigned i = 0; i < depth; ++i) {
    a = pool.push(a, 3);
    b = pool.push(b, 3);
  }
  const auto joined = pool.merge(a, b);
  CHECK(joined != cursiv::kLlCtxApart && joined != cursiv::kLlCtxNone);
  CHECK(pool.merge(b, a) == joined && pool.merge(joined, joined) == joined);
  auto at = joined;
  for (unsigned i = 0; i < depth; ++i) {
    CHECK(pool.count(at) == 1 && pool.ret(at, 0) == 3);
    at = pool.parent(at, 0);
  }
  CHECK(pool.count(at) == 2);
  CHECK(pool.ret(at, 0) == 1 && pool.ret(at, 1) == 2);
  CHECK(pool.parent(at, 0) == cursiv::kLlCtxNone &&
        pool.parent(at, 1) == cursiv::kLlCtxNone);
  CHECK(pool.merge(pool.push(a, 4, 1), pool.push(b, 4, 2)) ==
        cursiv::kLlCtxApart);
}

struct LlAnalysisFixture {
  cursiv::LlAtnView atn;
  unsigned nrules;
  const cursiv::TermInfo* terms = nullptr;
};
LlAnalysisFixture longOwedRun;

void shared_head_calls_are_settled_in_dependency_order() {
  constexpr unsigned count = 4096;
  std::vector<cursiv::LlAtnState> states(2 * count);
  std::vector<cursiv::LlAtnEdge> edges(count + 1);
  std::vector<std::uint32_t> starts(count);
  for (unsigned r = 0; r < count; ++r) {
    starts[r] = 2 * r;
    states[2 * r] = {
        .rule = static_cast<std::uint16_t>(r), .edge = r + 1, .nedges = 1};
    states[2 * r + 1] = {.kind = cursiv::LlAtnKind::ruleStop,
                         .rule = static_cast<std::uint16_t>(r)};
    edges[r + 1] =
        r + 1 < count
            ? cursiv::LlAtnEdge{.kind = cursiv::LlEdgeKind::rule,
                                .label = static_cast<std::uint16_t>(r + 1),
                                .target = 2 * (r + 1),
                                .follow = 2 * r + 1}
            : cursiv::LlAtnEdge{.kind = cursiv::LlEdgeKind::atom,
                                .label = 1,
                                .target = 2 * r + 1};
  }
  edges[0] = edges[1];
  states[0].edge = 0;
  states[0].nedges = 2;
  const cursiv::LlAtnView atn{.states = states.data(),
                              .nstates = 2 * count,
                              .edges = edges.data(),
                              .ruleStart = starts.data()};
  const auto shared = cursiv::ll_shared_rules(atn, count);
  CHECK(shared[0] == 0);
  for (unsigned r = 1; r < count; ++r) CHECK(shared[r] == 1);
}

void head_sharing_stops_at_conditions_and_closes_cycles() {
  using Kind = cursiv::LlEdgeKind;
  static constexpr cursiv::LlAtnState states[] = {
      {.rule = 0, .edge = 0, .nedges = 2},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.rule = 0, .edge = 2, .nedges = 1},
      {.rule = 0, .edge = 3, .nedges = 1},
      {.rule = 1, .edge = 4, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1},
      {.rule = 2, .edge = 5, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 2},
      {.rule = 3, .edge = 6, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 3}};
  static constexpr cursiv::LlAtnEdge edges[] = {
      {.kind = Kind::rule, .label = 2, .target = 6, .follow = 2},
      {.kind = Kind::rule, .label = 2, .target = 6, .follow = 2},
      {.kind = Kind::rule, .label = 1, .target = 4, .follow = 3},
      {.kind = Kind::rule, .label = 3, .target = 8, .follow = 1},
      {.kind = Kind::condition, .target = 5},
      {.kind = Kind::epsilon, .target = 7},
      {.kind = Kind::atom, .label = 1, .target = 9}};
  static constexpr std::uint32_t starts[] = {0, 4, 6, 8};
  static constexpr LlAnalysisFixture guarded{
      {.states = states, .nstates = 10, .edges = edges, .ruleStart = starts},
      4};
  auto shared = cursiv::ll_shared_rules(guarded.atn, guarded.nrules);
  CHECK(shared[0] == 0 && shared[1] == 1 && shared[2] == 0 && shared[3] == 0);

  static constexpr cursiv::LlAtnState cycleStates[] = {
      {.rule = 0, .edge = 0, .nedges = 2},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.rule = 1, .edge = 2, .nedges = 2},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1},
      {.rule = 2, .edge = 4, .nedges = 2},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 2}};
  static constexpr cursiv::LlAtnEdge cycleEdges[] = {
      {.kind = Kind::rule, .label = 1, .target = 2, .follow = 1},
      {.kind = Kind::rule, .label = 2, .target = 4, .follow = 1},
      {.kind = Kind::rule, .label = 2, .target = 4, .follow = 3},
      {.kind = Kind::atom, .label = 1, .target = 3},
      {.kind = Kind::rule, .label = 1, .target = 2, .follow = 5},
      {.kind = Kind::atom, .label = 2, .target = 5}};
  static constexpr std::uint32_t cycleStarts[] = {0, 2, 4};
  static constexpr LlAnalysisFixture cycle{{.states = cycleStates,
                                            .nstates = 6,
                                            .edges = cycleEdges,
                                            .ruleStart = cycleStarts},
                                           3};
  shared = cursiv::ll_shared_rules(cycle.atn, cycle.nrules);
  CHECK(shared[0] == 0 && shared[1] == 1 && shared[2] == 1);
}

void owed_prefixes_walk_deep_paths_and_keep_the_shortest_readings() {
  constexpr unsigned count = 20000;
  std::vector<cursiv::LlAtnState> states(count + 2);
  std::vector<cursiv::LlAtnEdge> edges(count + 1);
  for (unsigned i = 0; i <= count; ++i) {
    states[i] = {.rule = 0, .edge = i, .nedges = 1};
    edges[i] = {.kind = i < count ? cursiv::LlEdgeKind::epsilon
                                  : cursiv::LlEdgeKind::atom,
                .label = 1,
                .target = i + 1};
  }
  states.back() = {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0};
  const std::uint32_t start = 0;
  const cursiv::TermInfo terms[4]{};
  longOwedRun = {{.states = states.data(),
                  .nstates = count + 2,
                  .edges = edges.data(),
                  .ruleStart = &start},
                 1,
                 terms};
  cursiv::LlOwed<longOwedRun> owed;
  cursiv::LlSimulation sim(longOwedRun.atn);
  sim.load(std::vector<cursiv::LlConfig>{{.state = 0}});
  CHECK(owed.first(sim) == 1);

  using Kind = cursiv::LlEdgeKind;
  const cursiv::LlAtnState choices[] = {
      {.rule = 0, .edge = 0, .nedges = 3},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.rule = 0, .edge = 3, .nedges = 1},
      {.rule = 1, .edge = 4, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1},
      {.rule = 0, .edge = 5, .nedges = 1}};
  const cursiv::LlAtnEdge paths[] = {
      {.kind = Kind::atom, .label = 1, .target = 1},
      {.kind = Kind::rule, .label = 1, .target = 3, .follow = 2},
      {.kind = Kind::atom, .label = 2, .target = 5},
      {.kind = Kind::atom, .label = 3, .target = 1},
      {.kind = Kind::condition, .target = 4},
      {.kind = Kind::atom, .label = 2, .target = 1}};
  const std::uint32_t starts[] = {0, 3};
  longOwedRun = {
      {.states = choices, .nstates = 6, .edges = paths, .ruleStart = starts},
      2,
      terms};
  cursiv::LlOwed<longOwedRun> shortest;
  cursiv::LlSimulation branch(longOwedRun.atn);
  branch.load(std::vector<cursiv::LlConfig>{{.state = 0}});
  std::vector<std::uint16_t> due;
  shortest.due(branch, due, [](auto) { return true; });
  CHECK((due == std::vector<std::uint16_t>{1, 3}));
  CHECK(shortest.first(branch) == cursiv::kNoElement);
}

// The stack the descent stands on is one node of the pool, and an ask costs
// the frames that changed since the last.
void a_return_stack_pushes_only_the_frames_that_changed() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  cursiv::LlSimulation sim(table.atn);
  cursiv::LlReturnStack stack;
  CHECK(stack.context(sim) == cursiv::kLlCtxNone);
  stack.push_back(3);
  stack.push_back(4);
  const auto two = stack.context(sim);
  const auto nodes = sim.contexts().size();
  CHECK(two != cursiv::kLlCtxNone && nodes == 2);
  CHECK(stack.context(sim) == two && sim.contexts().size() == nodes);
  stack.push_back(5);
  const auto three = stack.context(sim);
  CHECK(three != two && sim.contexts().size() == nodes + 1);
  stack.pop_back();
  CHECK(stack.context(sim) == two && sim.contexts().size() == nodes + 1);
  stack.push_back(5);
  CHECK(stack.context(sim) == three && sim.contexts().size() == nodes + 1);
  cursiv::LlReturnStack other;
  other.push_back(3);
  other.push_back(4);
  other.push_back(5);
  CHECK(other.context(sim) == three);
  sim.contexts().clear();
  stack.pop_back();
  CHECK(stack.context(sim) != cursiv::kLlCtxNone && sim.contexts().size() == 2);
}

void an_abandon_replays_equivalent_returns_without_collapsing_frames() {
  const cursiv::LlAtnState states[] = {
      {},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.rule = 0, .edge = 0, .nedges = 1},
      {.rule = 1, .edge = 1, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1}};
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 1},
      {.kind = cursiv::LlEdgeKind::atom, .label = 42, .target = 4}};
  const std::uint32_t callers[] = {0, 0, 0};
  const std::uint32_t classes[] = {
      0, cursiv::kLlTailReturn | 1u, cursiv::kLlTailReturn | 1u, 3, 4};
  cursiv::LlAtnView atn{states, 5, edges, nullptr, callers, nullptr, classes};
  for (const bool classified : {true, false}) {
    if (!classified) atn.returnClass = nullptr;
    cursiv::LlSimulation sim(atn);
    const auto ctx = sim.contexts().push(cursiv::kLlCtxNone, 1, 7);
    sim.seed(3, ctx);
    CHECK(sim.leave(1, 2) == classified);
    if (classified) {
      CHECK(sim.configs().size() == 1 && sim.configs()[0].state == 1);
      CHECK(sim.contexts().start(ctx, 0) == 7);
    }
    sim.seed(3, ctx);
    CHECK(sim.leave(1, 1));
  }
}

constexpr std::uint16_t parity_terminal(std::string_view name) {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  for (std::uint16_t t = 1; t < table.nterms; ++t)
    if (table.term_name(t) == name) return t;
  return cursiv::kInvalidSymbol;
}

void a_search_reads_keywords_in_the_shared_alphabet() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  constexpr auto keyword = parity_terminal("export"),
                 identifier = parity_terminal("ID");
  static_assert(keyword != cursiv::kInvalidSymbol &&
                identifier != cursiv::kInvalidSymbol);
  std::array<cursiv::LlConfig, 2> cfg;
  for (std::uint32_t s = 0; s < table.atn.nstates; ++s)
    for (const auto& atom : cursiv::ll_atoms<table>().at(s)) {
      if (atom.label == keyword) cfg[0].state = s;
      if (atom.label == identifier) cfg[1].state = s;
    }
  cursiv::Cst tree("export");
  cursiv::CstBuilder builder(tree);
  cursiv::LlContext c("export", builder);
  cursiv::LlSearch search(table.atn, c.len);
  search.limit = 10000;
  const cursiv::Token token{0, 6, keyword};
  std::vector<std::uint16_t> terms;
  cursiv::ll_search_terms<table>(c, search, cfg.data(), 2, token, terms);
  CHECK(terms == std::vector<std::uint16_t>{keyword});
  cursiv::ll_search_terms<table>(c, search, cfg.data() + 1, 1, token, terms);
  CHECK(terms == std::vector<std::uint16_t>{identifier});
}

// A state after an optional prefix is also reachable from the rule's entry.
// Its required child owes a token only when that call really read the prefix.
void an_optional_prefix_keeps_owing_across_a_required_call() {
  static constexpr const auto& base = tpar::generated::Ll::kTable;
  constexpr auto keyword = parity_terminal("export"),
                 identifier = parity_terminal("ID");
  static_assert(keyword != cursiv::kInvalidSymbol &&
                identifier != cursiv::kInvalidSymbol);
  static constexpr cursiv::LlAtnState states[] = {
      {.rule = 0, .edge = 0, .nedges = 2},
      {.rule = 0, .edge = 2, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.rule = 1, .edge = 3, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 1}};
  static constexpr cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 1},
      {.kind = cursiv::LlEdgeKind::atom, .label = keyword, .target = 1},
      {.kind = cursiv::LlEdgeKind::rule, .label = 1, .target = 3, .follow = 2},
      {.kind = cursiv::LlEdgeKind::atom, .label = identifier, .target = 4}};
  static constexpr std::uint32_t starts[] = {0, 3}, callers[] = {0, 0, 1},
                                 returns[] = {2};
  static constexpr std::uint16_t edgeNodes[] = {
      cursiv::kNoRow, cursiv::kNoRow, cursiv::kNoRow, cursiv::kNoRow};
  static constexpr auto table = [] {
    auto t = base;
    t.nrules = 2;
    t.atn = {states, 5, edges, starts, callers, returns};
    t.edgeNode = edgeNodes;
    return t;
  }();
  // No prefix; a simulated real prefix; a real caller's CST prefix; and a
  // caller without that evidence, which must grant no free insertion.
  for (unsigned source = 0; source < 4; ++source) {
    cursiv::Cst tree("export");
    cursiv::CstBuilder builder(tree);
    if (source == 2) builder.leaf(0, 6, 0);
    cursiv::LlContext c("export", builder);
    cursiv::LlSearch s(table.atn, c.len);
    s.limit = 10000;
    s.sim.resume();
    s.sim.share_epsilon_contexts();
    s.sim.track_calls();
    s.sim.keep_steps();
    s.sim.at_token(source == 1 ? 1 : 0);
    auto ctx = cursiv::kLlCtxNone;
    if (source >= 2) {
      c.stack.push_back(2, 0);
      s.sourceTokens.assign(1, cursiv::kLlNoNode);
      s.sourceScanned = builder.stream().records();
      ctx = s.sim.contexts().push(ctx, 2, -1);
    }
    s.sim.seed(0, ctx);
    cursiv::LlReading r;
    if (source != 0) CHECK(s.sim.step(keyword));
    r.tokens = source == 1 ? 1 : 0;
    cursiv::ll_search_keep<table>(s, r);
    s.nodes.push_back(r);
    const auto& due = cursiv::ll_search_facts<table>(c, s, 0).due;
    CHECK((source == 1 || source == 2)
              ? due == std::vector<std::uint16_t>{identifier}
              : due.empty());
  }
}

void speculative_conditions_leave_the_real_filter_pending() {
  const cursiv::LlAtnState states[] = {
      {.edge = 0, .nedges = 2},
      {.edge = 2, .nedges = 1},
      {.edge = 3, .nedges = 1},
      {.edge = 4, .nedges = 1},
      {.edge = 5, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
  };
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 1},
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 2},
      {.kind = cursiv::LlEdgeKind::condition, .label = 0, .target = 3},
      {.kind = cursiv::LlEdgeKind::condition, .label = 1, .target = 4},
      {.kind = cursiv::LlEdgeKind::atom, .label = 42, .target = 5},
      {.kind = cursiv::LlEdgeKind::atom, .label = 43, .target = 5},
  };
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::LlAtnView atn{
      .states = states,
      .nstates = 6,
      .edges = edges,
      .callerBase = callerBase,
  };
  cursiv::LlSimulation simulation(atn);
  simulation.start(0, cursiv::kLlCtxNone);
  const auto before = simulation.configs();
  std::vector<std::uint16_t> possible;
  simulation.possible_terms(
      [&](std::uint16_t term) { possible.push_back(term); });
  CHECK(possible == std::vector<std::uint16_t>({42, 43}));
  CHECK(simulation.configs() == before && simulation.needs_filter());
  CHECK(simulation.filter(
      [](std::uint16_t condition) { return condition == 1; }));
  CHECK(simulation.settled() == 1 && simulation.step(43));
}

// A plan names places of one document: it answers the choices it elected where
// the descent stands, says nothing of the others, goes with a repair, and is
// not the next parse's.
void a_plan_is_the_parses_own_and_goes_with_a_repair() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  const auto decision = name_or_call_decision();
  CHECK(decision != cursiv::kNoRow);
  if (decision == cursiv::kNoRow) return;
  const std::string text = "name;name();name";
  {
    cursiv::LlPredictionSession<table> session;
    PredictionAt<table> at(text, decision);
    const auto name = at(0);
    CHECK(name != cursiv::kNoRow);
    CHECK(cursiv::ll_planned<table>(at.context, decision) == name);
    CHECK(cursiv::ll_planned<table>(at.context, decision) ==
          cursiv::kLlUnplanned);
    at.context.at = 3;
    CHECK(cursiv::ll_planned<table>(at.context, decision) ==
          cursiv::kLlUnplanned);
    at.context.at = 0;
    at.context.repaired.mark();
    CHECK(cursiv::ll_planned<table>(at.context, decision) ==
          cursiv::kLlUnplanned);
    CHECK(session.work().tokens != 0);
  }
  cursiv::LlPredictionSession<table> next;
  PredictionAt<table> at(text, decision);
  CHECK(cursiv::ll_planned<table>(at.context, decision) ==
        cursiv::kLlUnplanned);
}

// A word is reserved only in an alphabet that actually admits its keyword.
// Prediction uses the broad DFA under a changing mask, just as the descent
// can re-read the same spelling under an identifier-only mode.
void prediction_reserves_only_viable_keywords() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  constexpr std::uint16_t identifier = [] {
    for (std::uint16_t t = 1; t < table.nterms; ++t)
      if (table.term_name(t) == "ID") return t;
    return cursiv::kNoRow;
  }();
  constexpr std::uint16_t keyword = [] {
    for (std::uint16_t t = 1; t < table.nterms; ++t)
      if (table.term_text(t) == "let") return t;
    return cursiv::kNoRow;
  }();
  static_assert(identifier != cursiv::kNoRow && keyword != cursiv::kNoRow);
  cursiv::LlMask<table.termWords> viable;
  std::vector<std::uint16_t> read;
  viable.set(identifier);
  CHECK(cursiv::ll_match_in<table>("let;", 4, 0, viable, read) == 3);
  CHECK(read == std::vector<std::uint16_t>{identifier});
  viable.set(keyword);
  CHECK(cursiv::ll_match_in<table>("let;", 4, 0, viable, read) == 3);
  CHECK(read == std::vector<std::uint16_t>{keyword});
  CHECK(cursiv::ll_match_in<table>("lets;", 5, 0, viable, read) == 4);
  CHECK(read == std::vector<std::uint16_t>{identifier});

  const auto parser = tpar::generated::Ll::create_parser();
  for (const std::string_view text :
       {"let let = 1;", "show let, export;", "show x as as;"}) {
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.diagnostics.empty() &&
          result.value != nullptr);
  }
}

// A region with no ignored whitespace can leave its last lookahead on the
// newline after the entry rule. Finishing the document reads that gap with
// the grammar's default trivia, including any recorded comment.
void trailing_trivia_is_read_after_the_entry_region() {
  constexpr const auto& table = tpar::generated::Ll::kTable;
  const std::string_view text = "let a = 1;\n// tail\n";
  for (const bool classified : {false, true}) {
    cursiv::Cst tree(text);
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    builder.leaf(0, 10, cursiv::StampLayout{}.make(cursiv::kNoRow, 0));
    cursiv::LlContext context(text, builder);
    context.at = context.pendEnd = 10;
    context.la = cursiv::Token{10, 10, cursiv::kInvalidSymbol};
    context.laValid = true;
    if (classified) {
      const auto scan = cursiv::ll_match_text<table>(text.data(), text.size(),
                                                     table.errMode, 10);
      context.la = cursiv::Token{10, scan.end, scan.term};
      context.laMode = table.errMode;
      CHECK(table.terms[scan.term].trivia);
    }
    CHECK(cursiv::ll_tail<table>(context));
    cursiv::ll_flush_trivia<table>(context);
    CHECK(!context.repaired && context.la.sym == cursiv::kEofSymbol);
    CHECK(context.triviaAt == text.size());
    CHECK(tree.size() == 2 && tree[1].hidden && !tree[1].recovered);
    CHECK(tree[1].begin == 11 && tree[1].end == 18);
  }
}

const std::string_view kCorpus[] = {
    "let a = 1;",
    "let let = 1;",
    "show let, export;",
    "show x as as;",
    "let a b = 1;",
    "show x;",
    "show /* c */ x;",
    "show // c\n x;",
    "show all x, y;",
    "let a = 1 + 2 * 3;",
    "export let a = f(1, 2) - (3 + b);",
    "let a = 1; // tail\nlet b = a * a;",
    "let a = f();",
    "let a = f(g(h(1)));",
    "let a = (1);",
    "let a = (1 + 2) * 3;",
    "let a = ~(a) + (b * (c - 1));",
    "let a = (f(1))!;",
    "show (a);",
    "show (1 + 2) * 3 as v, ~(b);",
    "show a, (b), f((c));",
    "let a = 1 /* x */ + 2 /**/ * 3;",
    "let a = 1; /* runs to the end",
    "let a = 1; /* ends ** before the end */",
    "",
};

// The two engines read one language into one tree. The records are compared
// by span, extent and standing -- an element id is each engine's own
// numbering and says nothing across the two -- and the AST by class and
// member, which is what a consumer of either module holds.
void the_two_engines_write_one_tree() {
  const std::unique_ptr<cursiv::Parser> lr =
      tpar::generated::Lr::create_parser();
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  const std::unique_ptr<cursiv::AstReflection> lrR =
      tpar::generated::Lr::create_ast_reflection();
  const std::unique_ptr<cursiv::AstReflection> llR =
      tpar::generated::Ll::create_ast_reflection();
  for (const std::string_view text : kCorpus) {
    const cursiv::ParseResult a = lr->parse(text);
    const cursiv::ParseResult b = ll->parse(text);
    CHECK(a.accepted == b.accepted);
    CHECK(cursiv::test::records(a.cst()) == cursiv::test::records(b.cst()));
    CHECK((a.value == nullptr) == (b.value == nullptr));
    if (a.value == nullptr || b.value == nullptr) continue;
    std::string x, y;
    ast_with_spans(*lrR, a.cst(), *a.value, x);
    ast_with_spans(*llR, b.cst(), *b.value, y);
    if (x != y)
      std::fprintf(stderr, "AST mismatch input=[%.*s]\nLR=%s\nLL=%s\n",
                   static_cast<int>(text.size()), text.data(), x.c_str(),
                   y.c_str());
    CHECK(x == y);
  }
}

void boolean_directives_name_their_written_features() {
  for (const bool ll : {false, true}) {
    auto parser = ll ? tpar::generated::Ll::create_parser()
                     : tpar::generated::Lr::create_parser();
    const auto result = parser->parse("export let a = name; show all a;");
    CHECK(result.accepted && result.diagnostics.empty() && result.value);
    const auto& grammar = parser->getGrammar();
    for (const std::string_view feature : {"exported", "all"}) {
      bool named = false, written = false;
      for (cursiv::NodeId id = 0; id < result.cst().size(); ++id) {
        const auto& node = result.cst()[id];
        named |= grammar.recordFeature(node) == feature;
        written |= grammar.recordWrites(node, feature) ||
                   grammar.recordFills(node, feature);
      }
      CHECK(named && written);
    }
  }
}

void a_result_keeps_its_grammar_after_the_parser_dies() {
  for (const bool ll : {false, true}) {
    auto parser = ll ? tpar::generated::Ll::create_parser()
                     : tpar::generated::Lr::create_parser();
    auto result = parser->parse("let a = name;");
    CHECK(result.accepted && result.diagnostics.empty() && result.value);
    const auto* grammar = &parser->getGrammar();
    std::vector<std::string> rules, features;
    for (cursiv::NodeId id = 0; id < result.cst().size(); ++id) {
      const auto& node = result.cst()[id];
      rules.emplace_back(grammar->recordRule(node));
      features.emplace_back(grammar->recordFeature(node));
    }
    parser.reset();
    cursiv::ParseResult moved(std::move(result));
    result = std::move(moved);
    CHECK(result.cst().grammar() == grammar);
    for (cursiv::NodeId id = 0; id < result.cst().size(); ++id) {
      CHECK(grammar->recordRule(result.cst()[id]) == rules[id]);
      CHECK(grammar->recordFeature(result.cst()[id]) == features[id]);
    }
  }
}

// A block comment closes on its closer, or with the text: both engines read
// it as one hidden record, and a closer never written refuses nothing.
void a_block_comment_closes_with_the_text() {
  const std::unique_ptr<cursiv::Parser> lr =
      tpar::generated::Lr::create_parser();
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  for (const std::string_view text :
       {"let a = 1; /* open", "let a = 1; /*", "let a = 1; /* x */ /* y *",
        "let a = 1 /* x */ ;"}) {
    for (const cursiv::Parser* p : {lr.get(), ll.get()}) {
      const cursiv::ParseResult r = p->parse(text);
      CHECK(r.accepted && r.diagnostics.empty());
    }
  }
}

// A type an author instantiates with strings is named in each engine's unit as
// the author spelled it -- `<` and `,` inside the string, a number past the
// signed range -- so both units compile and write the same tree.
void a_string_instance_is_spelled_as_written() {
  const std::unique_ptr<cursiv::Parser> lr =
      ttag::generated::Lr::create_parser();
  const std::unique_ptr<cursiv::Parser> ll =
      ttag::generated::Ll::create_parser();
  const std::unique_ptr<cursiv::AstReflection> lrR =
      ttag::generated::Lr::create_ast_reflection();
  const std::unique_ptr<cursiv::AstReflection> llR =
      ttag::generated::Ll::create_ast_reflection();
  const cursiv::ParseResult a = lr->parse("ab , cd");
  const cursiv::ParseResult b = ll->parse("ab , cd");
  CHECK(a.accepted && b.accepted && a.diagnostics.empty() &&
        b.diagnostics.empty());
  CHECK(a.value != nullptr && b.value != nullptr);
  if (a.value == nullptr || b.value == nullptr) return;
  std::string x, y;
  cursiv::test::ast_of(*lrR, *a.value, x);
  cursiv::test::ast_of(*llR, *b.value, y);
  CHECK(x == y);
  CHECK(x.find("first=") != std::string::npos &&
        x.find("second=") != std::string::npos);
  for (const auto* parser : {lr.get(), ll.get()}) {
    const auto parsed = parser->parse("ab,cd,i,l,u,z");
    const auto* holder = cursiv::ast_cast<ttag::ast::Holder>(parsed.value);
    CHECK(parsed.diagnostics.empty() && holder && holder->numbers.size() == 4);
    if (!holder || holder->numbers.size() != 4) continue;
    const auto check = [&]<auto Value>(unsigned index, std::string_view name) {
      const auto* node = cursiv::ast_cast<
          ttag::ast::Numeric<Value, ::detail::CursivTag>>(holder->numbers[index]);
      CHECK(node && node->name == name);
    };
    check.template operator()<1>(0, "i");
    check.template operator()<1L>(1, "l");
    check.template operator()<1U>(2, "u");
    check.template operator()<1UL>(3, "z");
  }
}

// A document the grammar refuses is still a tree: the descent repairs where
// it stands, so every byte is under a record and the parse says what it did.
// The repairs themselves are each engine's own -- one searches with a cost
// model, the other mends in place -- so what is pinned here is the contract,
// not the shape.
const std::string_view kBroken[] = {
    "let a = ;", "let = 1;",    "let a 1;",       "let a = f(1;",  "let a = 1",
    "garbage",   "let a = (1;", "let a = 1; let", "export a = 1;",
};

void a_refused_document_is_still_a_tree() {
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  for (const std::string_view text : kBroken) {
    const cursiv::ParseResult r = ll->parse(text);
    CHECK(r.cst().size() != 0);
    // Every byte of the document is under some record.
    cursiv::TextOffset covered = 0;
    for (cursiv::NodeId i = 0; i < r.cst().size(); ++i) {
      CHECK(r.cst()[i].begin <= r.cst()[i].end);
      CHECK(r.cst()[i].end <= text.size());
      CHECK(r.cst()[i].subtreeSize != 0 && r.cst()[i].subtreeSize <= i + 1u);
      if (r.cst()[i].end > covered) covered = r.cst()[i].end;
    }
    CHECK(covered == static_cast<cursiv::TextOffset>(text.size()));
    bool syntax = false;
    for (const cursiv::ParseDiagnostic& d : r.diagnostics)
      syntax = syntax || d.isSyntax();
    CHECK(syntax);
  }
}

// ...but a repair both can make is one repair: a note per fault, of one kind
// over one span, and one tree.
void a_repair_both_engines_make_is_one_repair() {
  const std::unique_ptr<cursiv::Parser> lr =
      tpar::generated::Lr::create_parser();
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  const std::unique_ptr<cursiv::AstReflection> lrR =
      tpar::generated::Lr::create_ast_reflection();
  const std::unique_ptr<cursiv::AstReflection> llR =
      tpar::generated::Ll::create_ast_reflection();
  for (const std::string_view text :
       {"let a = 1 1;", "let a  1;", "let a = ;", "let ; = 1;", "a a = 1;",
        "show ;", "show x x;", "x x;"}) {
    const cursiv::ParseResult a = lr->parse(text);
    const cursiv::ParseResult b = ll->parse(text);
    bool alike = a.diagnostics.size() == b.diagnostics.size();
    for (std::size_t i = 0; alike && i < a.diagnostics.size(); ++i)
      alike = a.diagnostics[i].kind == b.diagnostics[i].kind &&
              a.diagnostics[i].begin == b.diagnostics[i].begin &&
              a.diagnostics[i].end == b.diagnostics[i].end;
    std::string x, y;
    if (a.value != nullptr) cursiv::test::ast_of(*lrR, *a.value, x);
    if (b.value != nullptr) cursiv::test::ast_of(*llR, *b.value, y);
    if (!alike || x != y)
      std::fprintf(stderr, "repair parity input=[%.*s] LR=%zu LL=%zu\n",
                   static_cast<int>(text.size()), text.data(),
                   a.diagnostics.size(), b.diagnostics.size());
    CHECK(alike && x == y);
  }
}

// A deletion names the token the reading resumes on, an insertion the terminal
// it owed, as the other engine says them.
void a_repair_names_what_the_other_engine_names() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const cursiv::ParseResult a = ll->parse("let a = 1 ? + 2;");
  const cursiv::ParseResult b = lr->parse("let a = 1 ? + 2;");
  CHECK(a.diagnostics.size() == 1 && b.diagnostics.size() == 1);
  for (std::size_t i = 0; i < a.diagnostics.size(); ++i) {
    const cursiv::ParseDiagnostic& d = a.diagnostics[i];
    CHECK(d.kind == cursiv::DiagnosticKind::deleted && d.begin == 10 &&
          d.end == 11);
    const auto spellings = ll->getGrammar().terminalSpellings(d.element);
    CHECK(spellings.size() == 1 && spellings.front() == "+" && d.also.empty());
    CHECK(d.element == b.diagnostics[i].element);
  }
  const cursiv::ParseResult c = ll->parse("show x as 2;");
  const cursiv::ParseResult e = lr->parse("show x as 2;");
  CHECK(c.diagnostics.size() == 1 && e.diagnostics.size() == 1);
  for (const cursiv::ParseDiagnostic& d : c.diagnostics) {
    CHECK(d.kind == cursiv::DiagnosticKind::inserted);
    CHECK(ll->getGrammar().terminalName(d.element) == "ID");
    if (e.diagnostics.size() != 1) continue;
    const cursiv::ParseDiagnostic& other = e.diagnostics[0];
    CHECK(d.kind == other.kind && d.begin == other.begin &&
          d.end == other.end && d.element == other.element &&
          d.also == other.also);
  }
}

// A token the expression cannot go on from, and no caller of it can either,
// costs the token and what the climb owes it, never the statement around it.
void a_stray_token_in_an_expression_leaves_the_statement() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const auto rl = tpar::generated::Ll::create_ast_reflection();
  for (const std::string_view text :
       {"let x = (a, b) + 3 * (4 - 1);\nshow x as y;",
        "let x = f, b) + 3;\nshow x;", "let c = b) * 2;\nshow c;",
        "show a, b);\nlet x = 1;"}) {
    const cursiv::ParseResult a = ll->parse(text);
    const cursiv::ParseResult b = lr->parse(text);
    CHECK(a.diagnostics.size() == 1);
    for (const cursiv::ParseDiagnostic& d : a.diagnostics)
      CHECK(d.end - d.begin <= 4);
    std::string x;
    cursiv::test::ast_of(*rl, *a.value, x);
    CHECK(x.find("lets0=") != std::string::npos);
    CHECK(x.find("shows0=") != std::string::npos);
    unsigned widest = 0;
    for (const cursiv::ParseDiagnostic& d : b.diagnostics)
      widest = std::max(widest, d.end - d.begin);
    CHECK(widest <= 4);
  }
}

// A skip that reads nothing of what it reaches is no recovery: the statements
// the words stand for are inserted instead.
void a_skip_to_the_end_of_the_text_reads_nothing() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const std::string_view text = "let x = 1;\nshowas y, 2;\n";
  const cursiv::ParseResult a = ll->parse(text);
  const cursiv::ParseResult b = lr->parse(text);
  CHECK(a.diagnostics.size() == b.diagnostics.size());
  for (std::size_t i = 0; i < a.diagnostics.size() && i < b.diagnostics.size();
       ++i)
    CHECK(a.diagnostics[i].kind == cursiv::DiagnosticKind::inserted &&
          a.diagnostics[i].begin == b.diagnostics[i].begin &&
          a.diagnostics[i].end == b.diagnostics[i].end);
}

// Where several tokens repair alike, both engines invent the one the grammar
// wrote first: a number before a name, an operator before the list's comma.
void an_invented_token_is_the_one_written_first() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  struct Case {
    std::string_view text, name, spelled;
  };
  for (const Case& k :
       {Case{"show ;", "NUM", ""}, Case{"let x = ;", "NUM", ""},
        Case{"let x = 1 + ;", "NUM", ""}, Case{"show x x;", "", "*"}}) {
    for (const cursiv::Parser* parser : {ll.get(), lr.get()}) {
      const cursiv::ParseResult r = parser->parse(k.text);
      CHECK(r.diagnostics.size() == 1);
      for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
        const auto spellings =
            parser->getGrammar().terminalSpellings(d.element);
        CHECK(parser->getGrammar().terminalName(d.element) == k.name &&
              (k.spelled.empty()
                   ? spellings.empty()
                   : spellings.size() == 1 && spellings.front() == k.spelled));
      }
    }
  }
}

// Sixty keywords may stand where the text has none, the one with the least id
// written last: both engines invent the first written.
void the_first_written_of_sixty_is_invented() {
  const auto ll = tmenu::generated::Ll::create_parser();
  const auto lr = tmenu::generated::Lr::create_parser();
  for (const cursiv::Parser* parser : {ll.get(), lr.get()}) {
    const cursiv::ParseResult r = parser->parse(";");
    CHECK(r.diagnostics.size() == 1);
    for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
      const auto spellings = parser->getGrammar().terminalSpellings(d.element);
      CHECK(spellings.size() == 1 && spellings.front() == "a00");
    }
  }
}

void repairs_before_trailing_whitespace_keep_parent_spans_valid() {
  // A group opens at the next visible token, while recovery inserts before
  // the ignored gap preceding it. The parent must cover that earlier child.
  cursiv::Cst tree("x  ");
  cursiv::CstBuilder builder(tree);
  const auto open = builder.enter();
  builder.leaf(1, 1, 0);
  const auto parent = builder.exit(open, 3, 0);
  CHECK(tree[parent].begin == 1 && tree[parent].end == 1);

  const auto parser = tpar::generated::Ll::create_parser();
  for (const std::string_view text :
       {"let a = 1; let \n\n", "let a = 1; let /*tail*/",
        "let a = 1 /*gap*/ 1; /*tail*/"}) {
    const auto result = parser->parse(text);
    CHECK(!result.diagnostics.empty());
    const auto* unit = result.value_as<tpar::ast::Unit>();
    CHECK(unit != nullptr && !unit->lets.empty());
    if (unit != nullptr && !unit->lets.empty())
      CHECK(unit->lets[0]->name == "a");
    for (cursiv::NodeId i = 0; i < result.cst().size(); ++i) {
      const auto& node = result.cst()[i];
      CHECK(node.begin <= node.end && node.end <= text.size());
      CHECK(node.subtreeSize != 0 && node.subtreeSize <= i + 1u);
    }
  }
}

std::string lets(std::size_t n) {
  std::string text;
  text.reserve(n * 19);
  for (std::size_t i = 0; i < n; ++i) text += "let a = 1 + 2 * 3;\n";
  return text;
}

// The uncancelled document is read whole, and an already cancelled call
// answers before allocating or descending into that document.
void a_long_document_parses_and_a_cancelled_call_stops() {
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  const std::string src = lets(1u << 14);
  const cursiv::ParseResult whole = ll->parse(src);
  CHECK(whole.accepted && whole.diagnostics.empty());

  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)ll->parse(src, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

// A nesting deeper than any machine stack: the descent's calls live on the
// heap, so the document is read like a shallow one. The comments leave trivia
// pending at every call.
void a_document_deeper_than_the_stack_is_read() {
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  constexpr int kDeep = 100000;
  std::string text = "let a = ";
  for (int i = 0; i < kDeep; ++i) text += "( // c\n";
  text += '1';
  for (int i = 0; i < kDeep; ++i) text += ')';
  text += ';';
  const cursiv::ParseResult r = ll->parse(text);
  CHECK(r.cst().size() != 0);
  for (cursiv::NodeId i = 0; i < r.cst().size(); ++i) {
    const auto& node = r.cst()[i];
    CHECK(node.begin <= node.end && node.end <= text.size());
    CHECK(node.subtreeSize != 0 && node.subtreeSize <= i + 1u);
    if (i != 0) CHECK(r.cst()[i - 1u].end <= node.end);
  }
  CHECK(r.diagnostics.empty() && r.value != nullptr);
}

// A keyword the author spelled wrong is read as the keyword he meant. The
// span stays his -- what the repair names is the terminal his bytes stand
// for -- and everything after the fault is still read.
void a_misspelt_keyword_is_read_as_the_one_it_spells() {
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  const cursiv::ParseResult r = ll->parse("exort let a = 1; let b = 2;");
  CHECK(r.value != nullptr);
  bool replaced = false;
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    replaced = replaced || d.kind == cursiv::DiagnosticKind::replaced;
  CHECK(replaced);
  // Both statements, not just the one before the fault.
  const cursiv::ParseResult whole = ll->parse("export let a = 1; let b = 2;");
  CHECK(whole.accepted && whole.diagnostics.empty());
  CHECK(r.cst().size() == whole.cst().size());
}

// ...and a word no keyword is one edit from is text to skip, not a keyword to
// guess. The loop it stands in reads on rather than ending the document.
void a_word_that_spells_no_keyword_is_skipped() {
  const std::unique_ptr<cursiv::Parser> ll =
      tpar::generated::Ll::create_parser();
  const cursiv::ParseResult r = ll->parse("zzzz let a = 1; let b = 2;");
  CHECK(r.value != nullptr);
  bool deleted = false;
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    deleted = deleted || d.kind == cursiv::DiagnosticKind::deleted;
  CHECK(deleted);
  const cursiv::ParseResult whole = ll->parse("let a = 1; let b = 2;");
  CHECK(whole.accepted && whole.diagnostics.empty());
  // The skip costs one record, and the two statements below it survive.
  CHECK(r.cst().size() == whole.cst().size() + 1);
}

// `levels(left("*" | "/"), left("+" | "-"))`, as the build holds it: four
// operators in the order they were written, two precedences, none
// right-associative, and one member for all four.
void the_precedence_table_says_what_the_grammar_said() {
  const cursiv::LlTable& t = tpar::generated::Ll::kTable;
  const auto built =
      cursiv::ll_build_program(cursiv::LlLowering<tpar::Ll>::table(), {});
  const auto& c = built.cfg.climbs[0];
  CHECK(c.opCount == 4);
  const cursiv::LlClimbOp* const op = built.cfg.climbOps.data() + c.opBase;
  CHECK(op[0].prec == op[1].prec && op[2].prec == op[3].prec);
  CHECK(op[0].prec > op[2].prec);
  const auto first = built.cfg.atomic_operator(op[0].body);
  CHECK(first != cursiv::kNoRow);
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(op[i].assoc == cursiv::Association::left);
    const auto leaf = built.cfg.atomic_operator(op[i].body);
    CHECK(leaf != cursiv::kNoRow);
    if (leaf == cursiv::kNoRow || first == cursiv::kNoRow) continue;
    CHECK(built.cfg.nodes[leaf].row == built.cfg.nodes[first].row);
    CHECK(built.cfg.nodes[leaf].row != cursiv::kNoRow);
    CHECK(t.nodes[leaf].mode == c.mode && !t.nodes[leaf].head &&
          t.nodes[leaf].covers == 0);
    CHECK(built.nodeAfter[leaf] != 0xFFFFFFFFu);
  }
  CHECK(t.nafter == built.cfg.nnodes);
  // The runs a join's two operands owe, and the state a call of the operand
  // returns into: zero here is a field that was never written.
  CHECK(c.leftCount != 0 && c.rightCount != 0);
  CHECK(c.atnEntry != 0);
  // From the second turn on, the node the climb just laid is its left operand.
  CHECK(std::ranges::any_of(
      std::span(built.fuse).subspan(c.leftBase, c.leftCount),
      [&](const cursiv::LlFuse& f) {
        return f.from == c.node && f.to != cursiv::kNoRow;
      }));
}

cursiv::ConversionResult<std::string> borrowed;
using BorrowedConverter =
    cursiv::detail::text_conv<[]() -> auto& { return borrowed; }>;

// A converter answering a result it keeps hands out copies: a second
// conversion reads the same value, and the same failure.
void borrowed_conversion_results_are_not_consumed() {
  std::vector<cursiv::ParseDiagnostic> diagnostics;
  cursiv::AstBuildCtx ctx;
  ctx.diags = &diagnostics;
  const auto convert = [&](std::string& value) {
    return cursiv::detail::invoke_conv<BorrowedConverter, std::string, 0,
                                       false>(ctx, cursiv::kNoNode, &value);
  };
  borrowed = decltype(borrowed)::err("kept failure");
  for (unsigned i = 0; i < 2; ++i) {
    std::string value;
    CHECK(!convert(value));
  }
  CHECK(diagnostics.size() == 2);
  for (const auto& diagnostic : diagnostics)
    CHECK(diagnostic.message == "kept failure");
  borrowed = decltype(borrowed)::ok("kept value");
  for (unsigned i = 0; i < 2; ++i) {
    std::string value;
    CHECK(convert(value) && value == "kept value");
  }
}

using StripFirstConverter =
    cursiv::detail::text_conv<[](std::string_view text) {
      return text.substr(1);
    }>;

// An invented child can sit below a clean data-rule close. The converter
// must leave the field at its default instead of stripping an empty value.
void data_conversions_require_every_part_to_have_been_written() {
  cursiv::Cst tree(" ");
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  const auto open = builder.enter();
  const auto missing = builder.leaf(0, 0, 0);
  builder.stream().at(missing).recovered = true;
  builder.hidden(0, 1, 0);
  const auto root = builder.exit(open, 0, 0);
  CHECK(!tree[root].recovered);
  const cursiv::AstActions actions{};
  cursiv::AstBuildCtx context;
  context.cst = &tree;
  context.actions = &actions;
  context.repaired = true;
  std::string_view value = "default";
  CHECK(!cursiv::detail::invoke_conv<StripFirstConverter, std::string_view, 0,
                                     true>(context, root, &value));
  CHECK(value == "default");
  CHECK(!cursiv::detail::invoke_conv<StripFirstConverter, std::string_view, 0,
                                     false>(context, missing, &value));
  CHECK(value == "default");

  cursiv::Cst written("@name");
  cursiv::CstBuilder writtenBuilder(written);
  writtenBuilder.bind_stream();
  const auto leaf = writtenBuilder.leaf(0, 5, 0);
  context.cst = &written;
  CHECK(cursiv::detail::invoke_conv<StripFirstConverter, std::string_view, 0,
                                    true>(context, leaf, &value));
  CHECK(value == "name");
}

// A stack whose forks multiply its routes is read once per context: each level
// here doubles them, and no return names a member.
void a_forked_stack_is_read_once_per_context() {
  constexpr auto& T = tpar::generated::Ll::kTable;
  const cursiv::StampedGrammar grammar(tpar::generated::Ll::kGrammarMetaData,
                                       cursiv::StampLayout{});
  cursiv::LlSimulation sim(T.atn);
  cursiv::LlCtxPool& pool = sim.contexts();
  constexpr unsigned kLevels = 18;
  std::uint32_t ctx = pool.push(cursiv::kLlCtxNone, 0);
  for (unsigned level = 0; level < kLevels; ++level)
    ctx = pool.merge(pool.push(ctx, 0), pool.push(pool.push(ctx, 0), 1));
  cursiv::LlAgree agree;
  cursiv::ll_member_up<T>(grammar, sim, ctx, agree);
  CHECK(agree.frames <= 4 * kLevels);
}

// The spellings a caret is offered, as a set: the two engines number their
// terminals apart.
std::vector<std::string> spelled_at(const cursiv::Parser& parser,
                                    const cursiv::Cst& cst,
                                    cursiv::TextOffset at) {
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(cst, at, at, next);
  std::vector<std::string> out;
  for (const cursiv::NextFeature& f : next) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    if (spellings.empty()) out.emplace_back();
    for (const std::string_view s : spellings) out.emplace_back(s);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// A caret inside a token is asked at the token's start, as the table engine
// asks it: the answer replaces the token.
void a_caret_inside_a_token_answers_in_its_place() {
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  const std::string text = "let a = 1;";
  const cursiv::ParseResult a = lr->parse(text);
  const cursiv::ParseResult b = ll->parse(text);
  for (const cursiv::TextOffset at : {1u, 2u}) {
    const auto want = spelled_at(*lr, a.cst(), at);
    CHECK(!want.empty() && spelled_at(*ll, b.cst(), at) == want);
  }
}

// A spelling the lexer would run on into a comment is no answer, however far
// past the caret that comment closes.
void a_spelling_is_judged_by_the_text_as_far_as_it_runs() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  const std::string text = "tail:a * bbbbbbbbbbbb /* c */";
  const cursiv::ParseResult b = ll->parse(text);
  CHECK(b.accepted && b.diagnostics.empty() && b.value);
  const auto offered = spelled_at(*ll, b.cst(), 7);
  CHECK(std::find(offered.begin(), offered.end(), "/") == offered.end());
  CHECK(std::find(offered.begin(), offered.end(), "*") != offered.end());
}

void caret_out_of_range_keeps_the_output_and_next_request_intact() {
  const auto parser = tpar::generated::Ll::create_parser();
  constexpr std::string_view text = "let a = name;";
  const auto parsed = parser->parse(text);
  CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
  constexpr cursiv::TextOffset at = 8;
  std::vector<cursiv::NextFeature> baseline;
  parser->nextFeaturesAt(parsed.cst(), at, at, baseline);
  CHECK(!baseline.empty());

  const auto equal = [](const auto& a, const auto& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i].feature != b[i].feature || a[i].property != b[i].property ||
          a[i].narrow != b[i].narrow || a[i].type != b[i].type)
        return false;
    return true;
  };
  const std::vector<cursiv::NextFeature> sentinel{
      {123u, "kept-first", 7u, parsed.value ? parsed.value->type : nullptr},
      {321u, "kept-second", 3u, nullptr}};
  const auto end = static_cast<cursiv::TextOffset>(text.size());
  for (const auto& [badAt, badResume] :
       std::array<std::pair<cursiv::TextOffset, cursiv::TextOffset>, 4>{
           std::pair{end + 1u, end + 1u}, std::pair{at, end + 1u},
           std::pair{at, at - 1u}, std::pair{end + 1u, end}}) {
    auto output = sentinel;
    const auto* data = output.data();
    const auto capacity = output.capacity();
    bool threw = false;
    try {
      parser->nextFeaturesAt(parsed.cst(), badAt, badResume, output);
    } catch (...) {
      threw = true;
    }
    CHECK(!threw && equal(output, sentinel));
    CHECK(output.data() == data && output.capacity() == capacity);
  }
  std::vector<cursiv::NextFeature> normal;
  parser->nextFeaturesAt(parsed.cst(), at, at, normal);
  CHECK(!normal.empty() && equal(normal, baseline));
}

}  // namespace

namespace prediction_session {

namespace {
// The hand-written drive consumes the entire input as one action; its finish
// scans only EOF. It needs no lexical acceptance predicates. Keep them out of
// this artificial parser to avoid GCC's dormant dummy-array warning.
constexpr auto table = [] {
  auto copy = tpar::generated::Ll::kTable;
  copy.lexChoices = nullptr;
  return copy;
}();
thread_local unsigned errors = 0, calls = 0;
thread_local bool nested = false, child = false, explode = false,
                  nestedThrow = false, nestedCancel = false;
thread_local cursiv::CancellationTokenSource* stop = nullptr;
thread_local const cursiv::Parser* custom = nullptr;
void verify(bool condition) {
  if (!condition) ++errors;
}
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
void drive(cursiv::LlContext& c) {
  auto* ex = cursiv::ll_episode_session_slot<table>();
  verify(ex && !ex->has_value());
  auto& kept = cursiv::ll_episode_cache<table>();
  verify(kept.plan.empty());
  ++calls;
  std::vector<cursiv::LlPlanEntry> laid{
      {.at = calls, .decision = 1, .arm = static_cast<std::uint16_t>(calls)}};
  kept.plan.install(laid, nullptr, 0);
  if (explode) throw std::runtime_error("drive exception");
  c.last = c.tape.leaf(0, c.len, cursiv::StampLayout{}.make(0, 0));
  c.at = c.len;
}
cursiv::AstNode* make(cursiv::AstBuildCtx& ctx, cursiv::NodeId id) {
  auto* node = static_cast<Node*>(cursiv::detail::make_node<Node>(ctx, id));
  node->text = ctx.cst->text();
  if (nested && !child) {
    auto* b = cursiv::ll_episode_session_slot<table>();
    const auto tag = cursiv::ll_episode_cache<table>().plan.entries()[0].arm;
    child = true;
    if (nestedThrow) {
      explode = true;
      bool caught = false;
      try {
        (void)custom->parse("child");
      } catch (const std::runtime_error&) {
        caught = true;
      }
      explode = false;
      verify(caught);
    } else if (nestedCancel) {
      cursiv::CancellationTokenSource source;
      stop = &source;
      bool caught = false;
      try {
        (void)custom->parse("child", source.get_token());
      } catch (const cursiv::OperationCancelled&) {
        caught = true;
      }
      stop = nullptr;
      verify(caught && source.stop_requested());
    } else {
      const auto result = custom->parse("child");
      verify(result.value && result.diagnostics.empty());
    }
    child = false;
    verify(cursiv::ll_episode_session_slot<table>() == b);
    verify(cursiv::ll_episode_cache<table>().plan.entries()[0].arm == tag);
  }
  if (stop) stop->request_stop();
  return node;
}
void entry(cursiv::AstReducer& builder, cursiv::NodeId id) {
  builder.push_made(id, builder.create<make, nullptr, false>(0, id));
}
const cursiv::AstMetaData metadata{};
const cursiv::detail::MakeFn makes[] = {make};
const cursiv::AstEntryFn entries[] = {entry};
const cursiv::AstActions actions{
    .meta = &metadata, .makes = makes, .entries = entries, .entryCount = 1};
void public_calls_are_fresh_and_restore_on_exit() {
  cursiv::LlLanguageParser<table> parser(tpar::generated::Ll::kGrammarMetaData,
                                         actions, drive);
  custom = &parser;
  auto* exact = &cursiv::ll_episode_cache<table>();
  std::vector<cursiv::LlPlanEntry> laid{{.at = 0, .decision = 1, .arm = 987}};
  exact->plan.install(laid, nullptr, 0);
  for (unsigned n = 0; n < 2; ++n) {
    const auto r = parser.parse("value");
    verify(r.value && r.diagnostics.empty());
    verify(static_cast<Node*>(r.value)->text == "value");
  }
  verify(calls == 2);
  verify(cursiv::ll_episode_session_slot<table>() == nullptr);
  verify(&cursiv::ll_episode_cache<table>() == exact &&
         exact->plan.entries()[0].arm == 987);
  nested = true;
  const auto parent = parser.parse("parent");
  nested = false;
  verify(parent.value && static_cast<Node*>(parent.value)->text == "parent" &&
         calls == 4);
  nested = true;
  nestedThrow = true;
  const auto recovered = parser.parse("parent-error");
  nestedThrow = false;
  nestedCancel = true;
  const auto uncancelled = parser.parse("parent-cancel");
  nestedCancel = false;
  nested = false;
  verify(recovered.value && uncancelled.value &&
         recovered.diagnostics.empty() && uncancelled.diagnostics.empty());
  explode = true;
  bool threw = false;
  try {
    (void)parser.parse("bad");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  explode = false;
  verify(threw);
  cursiv::CancellationTokenSource cancel;
  stop = &cancel;
  threw = false;
  try {
    (void)parser.parse("cancel", cancel.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  stop = nullptr;
  verify(threw && cancel.stop_requested());
  verify(cursiv::ll_episode_session_slot<table>() == nullptr);
  const auto after = parser.parse("after");
  verify(after.value && static_cast<Node*>(after.value)->text == "after");
  std::array<unsigned, 4> threadErrors{};
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 4; ++i)
    workers.emplace_back([&, i] {
      custom = &parser;
      for (unsigned j = 0; j < 4; ++j) {
        const auto r = parser.parse("parallel");
        verify(r.value && r.diagnostics.empty());
      }
      verify(calls == 4);
      threadErrors[i] = errors;
    });
  for (auto& worker : workers) worker.join();
  for (auto error : threadErrors) CHECK(error == 0);
  CHECK(errors == 0);
}
void memo_indexes_end_dependencies() {
  cursiv::LlEpisodeMemo memo;
  constexpr std::uint64_t hash = 719;
  const auto arm = [&](cursiv::TextOffset room,
                       std::uint16_t rejected = cursiv::kNoRow) {
    const auto* row = memo.find(hash, room, [&](const auto& k) {
      return k.context == 4 && k.length <= room &&
             (!k.ended || k.length == room) && k.arm != rejected;
    });
    return row != nullptr ? row->arm : cursiv::kNoRow;
  };
  memo.add(hash, {.context = 4, .length = 8, .arm = 1}, {});
  memo.add(hash, {.context = 4, .length = 20, .arm = 2, .ended = true}, {});
  CHECK(arm(20) == 2 && arm(21) == 1);
  memo.add(hash, {.context = 4, .length = 8, .arm = 3}, {});
  CHECK(arm(20) == 3 && arm(21) == 3);
  memo.add(hash, {.context = 4, .length = 20, .arm = 4, .ended = true}, {});
  CHECK(arm(20) == 4 && arm(20, 4) == 3);
  // Index collisions still go through the exact question comparison.
  constexpr auto collision = (hash ^ 20u) * 1099511628211ull;
  memo.add(collision, {.context = 7, .length = 8, .arm = 5}, {});
  CHECK(arm(20) == 4);
  memo.clear();
  CHECK(arm(20) == cursiv::kNoRow);
  for (std::uint32_t length = 1; length <= 10000; ++length)
    memo.add(hash, {.context = 4, .length = length, .arm = 1, .ended = true},
             {});
  unsigned compared = 0;
  CHECK(memo.find(hash, 10001, [&](const auto&) {
    ++compared;
    return false;
  }) == nullptr);
  CHECK(compared == 0);
}
void generated_public_ast_is_unchanged() {
  const auto ll = tpar::generated::Ll::create_parser(),
             lr = tpar::generated::Lr::create_parser();
  const auto reflect = tpar::generated::Ll::create_ast_reflection();
  for (unsigned i = 0; i < 3; ++i)
    for (const auto text :
         {"let a = name;", "let a = call(1,2);", "show (f(1))!, name;"}) {
      const auto a = ll->parse(text), b = lr->parse(text);
      CHECK(a.value && b.value && a.diagnostics.empty() &&
            b.diagnostics.empty());
      if (a.value && b.value) {
        std::string x, y;
        cursiv::test::ast_of(*reflect, *a.value, x);
        cursiv::test::ast_of(*reflect, *b.value, y);
        CHECK(x == y);
      }
      if (std::string_view(text) == "let a = call(1,2);") {
        const auto* unit = a.value_as<tpar::ast::Unit>();
        CHECK(unit && unit->lets.size() == 1);
        if (unit && unit->lets.size() == 1) {
          const auto* declaration = unit->lets[0];
          CHECK(declaration->name == "a" && declaration->parent == unit);
          const auto* call =
              cursiv::ast_cast<tpar::ast::Call>(declaration->value);
          CHECK(call && call->parent == declaration && call->name == "call" &&
                call->args.size() == 2);
          if (call && call->args.size() == 2)
            for (unsigned n = 0; n < 2; ++n) {
              const auto* number =
                  cursiv::ast_cast<tpar::ast::Num>(call->args[n]);
              CHECK(number && number->parent == call &&
                    number->value == double(n + 1));
            }
        }
      }
    }
  CHECK(cursiv::ll_episode_session_slot<tpar::generated::Ll::kTable>() ==
        nullptr);
  // Generated rule translation units must use the same active session as
  // their module. A mixed build would leak learned entries into this fallback.
  CHECK(cursiv::ll_episode_cache<tpar::generated::Ll::kTable>().plan.empty());
}
}  // namespace
void check_prediction_sessions() {
  memo_indexes_end_dependencies();
  public_calls_are_fresh_and_restore_on_exit();
  generated_public_ast_is_unchanged();

}
}

int main() {
  prediction_session::check_prediction_sessions();
  boolean_directives_name_their_written_features();
  ast_conversion_observes_cancellation_before_return();
  the_parser_propagates_cancellation_to_ast_materialisation();
  ast_materialisation_rejects_an_invalid_entry_extent();
  predictions_keep_the_end_they_depended_on();
  deep_prediction_keeps_its_known_continuation();
  a_table_gives_back_the_room_a_larger_walk_left();
  a_pool_gives_back_what_a_walk_made();
  call_cells_are_sealed_after_their_actual_dependencies();
  deep_cell_paths_keep_their_order_and_stop_at_the_first_refusal();
  context_keys_keep_both_halves_on_every_platform();
  deep_context_joins_keep_their_canonical_stacks();
  shared_head_calls_are_settled_in_dependency_order();
  head_sharing_stops_at_conditions_and_closes_cycles();
  owed_prefixes_walk_deep_paths_and_keep_the_shortest_readings();
  a_return_stack_pushes_only_the_frames_that_changed();
  an_abandon_replays_equivalent_returns_without_collapsing_frames();
  a_search_reads_keywords_in_the_shared_alphabet();
  an_optional_prefix_keeps_owing_across_a_required_call();
  caret_out_of_range_keeps_the_output_and_next_request_intact();
  a_forked_stack_is_read_once_per_context();
  a_caret_inside_a_token_answers_in_its_place();
  a_spelling_is_judged_by_the_text_as_far_as_it_runs();
  speculative_conditions_leave_the_real_filter_pending();
  a_plan_is_the_parses_own_and_goes_with_a_repair();
  prediction_reserves_only_viable_keywords();
  trailing_trivia_is_read_after_the_entry_region();
  the_two_engines_write_one_tree();
  a_result_keeps_its_grammar_after_the_parser_dies();
  a_block_comment_closes_with_the_text();
  a_string_instance_is_spelled_as_written();
  a_refused_document_is_still_a_tree();
  a_repair_both_engines_make_is_one_repair();
  a_repair_names_what_the_other_engine_names();
  a_stray_token_in_an_expression_leaves_the_statement();
  a_skip_to_the_end_of_the_text_reads_nothing();
  an_invented_token_is_the_one_written_first();
  the_first_written_of_sixty_is_invented();
  repairs_before_trailing_whitespace_keep_parent_spans_valid();
  borrowed_conversion_results_are_not_consumed();
  data_conversions_require_every_part_to_have_been_written();
  a_long_document_parses_and_a_cancelled_call_stops();
  a_document_deeper_than_the_stack_is_read();
  a_misspelt_keyword_is_read_as_the_one_it_spells();
  a_word_that_spells_no_keyword_is_skipped();
  the_precedence_table_says_what_the_grammar_said();
  return cursiv::test::report("LlEngine_test");
}
