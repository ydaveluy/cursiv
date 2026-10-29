#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/ll_ast_registry.hpp"
#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/lr/gen/lr_ast_registry.hpp"
#include "cursiv/engines/shared/gen/AstEmitter.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/GrammarBindings.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexBacktrack.hpp"
#include "cursiv/engines/shared/gen/LexModes.hpp"
#include "cursiv/engines/shared/gen/LexStrayExpansion.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/gen/NamedHead.hpp"
#include "cursiv/engines/shared/gen/OperatorWords.hpp"
#include "cursiv/engines/shared/gen/TypeSpelling.hpp"
#include "cursiv/engines/shared/gen/write_source.hpp"
#include "cursiv/test/Check.hpp"

namespace astgen {

template <auto Expression>
struct Operator {};

void nested_template_arguments_remain_qualified() {
  // An already qualified spelling stays so, independently of the compiler's
  // pretty form; GCC's class value also needs expression parentheses.
  CHECK(cursiv::gen_qualified(
            "astgen::Operator<cursiv::Lit<cursiv::str<3>{\"is\"}, false, "
            "cursiv::detail::WordBound>()>") ==
        "::astgen::Operator<(cursiv::Lit<cursiv::str<3>{\"is\"}, false, "
        "cursiv::detail::WordBound>())>");
  const auto spelling = cursiv::gen_qualified(
      cursiv::detail::raw_type_name<Operator<cursiv::kw<"is">>>());
  CHECK(spelling.starts_with("::astgen::Operator<"));
  CHECK(spelling.find("detail::WordBound") != std::string::npos);
  CHECK(spelling.find("false, WordBound") == std::string::npos);
  using Repeated = Operator<cursiv::many(cursiv::lit<"x">)>;
  const auto repeated =
      cursiv::gen_qualified(cursiv::detail::raw_type_name<Repeated>());
  CHECK(repeated.find("kUnbounded") != std::string::npos);
  const auto nested = cursiv::gen_qualified(
      cursiv::detail::raw_type_name<
          Operator<cursiv::many(cursiv::lit<"x">) + cursiv::lit<"y">>>());
  CHECK(nested.find("kUnbounded") != std::string::npos);

  const auto maximum = std::to_string(cursiv::kUnbounded);
  const auto maximum_literal =
      maximum + (std::numeric_limits<std::size_t>::digits >
                         std::numeric_limits<long long>::digits
                     ? "ull"
                     : "");
  CHECK(repeated.find(maximum) == std::string::npos);
  CHECK(nested.find(maximum) == std::string::npos);
  CHECK(cursiv::gen_qualified("cursiv::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::cursiv::Repeat<0,::cursiv::kUnbounded,astgen::Token>");
  CHECK(cursiv::gen_qualified("cursiv::Repeat<" + maximum +
                              ",7,astgen::Token>") ==
        "::cursiv::Repeat<" + maximum_literal + ",7,astgen::Token>");
  CHECK(cursiv::gen_qualified("cursiv::Repeat<2,7,astgen::Token>") ==
        "::cursiv::Repeat<2,7,astgen::Token>");
  CHECK(cursiv::gen_qualified("astgen::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::astgen::Repeat<0," + maximum_literal + ",astgen::Token>");
  CHECK(cursiv::gen_qualified("astgen::cursiv::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::astgen::cursiv::Repeat<0," + maximum_literal + ",astgen::Token>");
  const std::string quoted =
      "astgen::Literal<\"cursiv::Repeat<0," + maximum + ",Token>\">";
  CHECK(cursiv::gen_qualified(quoted) == "::" + quoted);

  CHECK(cursiv::gen_qualified(
            cursiv::detail::raw_type_name<
                Operator<std::numeric_limits<long long>::min()>>()) ==
        "::astgen::Operator<(-9223372036854775807ll - 1)>");
  CHECK(cursiv::gen_qualified("astgen::Operator<-9223372036854775808L>") ==
        "::astgen::Operator<(-9223372036854775807l - 1)>");
  CHECK(cursiv::gen_qualified("astgen::Operator<18446744073709551615>") ==
        "::astgen::Operator<18446744073709551615ull>");
  CHECK(cursiv::gen_widen("\"-9223372036854775808 18446744073709551615\"") ==
        "\"-9223372036854775808 18446744073709551615\"");
}

struct Base : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct RefOnly : cursiv::Extends<cursiv::AstNode> {};
struct Derived : cursiv::Extends<Base> {
  reference<RefOnly> target;
};
struct Wrapper : cursiv::Extends<Derived> {
  Derived* child = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<create<Derived>(assign<&Base::name>(ref<ID>) +
                             assign<&Derived::target>(ref<ID>) +
                             many(nest<Wrapper, &Wrapper::child>(lit<"!">)))> {
  };
  using entry = Root;
};

// A registry type is spelled by its index among the grammar's bound types.
template <class T>
std::size_t type_of(const cursiv::AstRegistry& registry) {
  const auto source = std::format(
      "::cursiv::detail::ast_bound_t<{}, {}>",
      cursiv::gen_qualified(cursiv::detail::raw_type_name<G>()),
      cursiv::detail::index_of<
          T, cursiv::detail::GrammarBindings<G>::types>::value);
  for (std::size_t i = 0; i < registry.types.size(); ++i)
    if (registry.types[i].source == source) return i;
  return registry.types.size();
}

template <auto P>
std::size_t field_of(const cursiv::AstRegistry& registry) {
  using F = cursiv::detail::field_tag<P>;
  const auto owner = type_of<typename F::owner>(registry);
  for (std::size_t i = 0; i < registry.fields.size(); ++i)
    if (registry.fields[i].owner == owner && registry.fields[i].name == F::name)
      return i;
  return registry.fields.size();
}

void reflection_only_types_need_no_factory(
    const cursiv::AstRegistry& registry) {
  const cursiv::AstEntryTable table;
  cursiv::AstEmitter emitter(registry, table, "astgen",
                             "AstGeneration_test.cpp");
  const auto base = type_of<Base>(registry);
  const auto reference = type_of<RefOnly>(registry);
  const auto derived = type_of<Derived>(registry);
  const auto wrapper = type_of<Wrapper>(registry);
  CHECK(std::max({base, reference, derived, wrapper}) < registry.types.size());
  CHECK(!registry.types[base].built && !registry.types[reference].built);
  CHECK(registry.types[derived].built && registry.types[wrapper].built);
  CHECK(emitter.applier_name(base) == "a_none");
  CHECK(emitter.applier_name(reference) == "a_none");
  CHECK(emitter.applier_name(derived) != "a_none");
  CHECK(emitter.applier_name(wrapper) != "a_none");
  emitter.emit_types();
  const auto factory = [&registry]<class T>() {
    return "make_node<" + registry.types[type_of<T>(registry)].source + ">";
  };
  CHECK(emitter.out.find(factory.template operator()<Base>()) ==
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<RefOnly>()) ==
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<Derived>()) !=
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<Wrapper>()) !=
        std::string::npos);
  emitter.emit_writers();
  const auto name = field_of<&Base::name>(registry);
  CHECK(emitter.out.find("case " + std::to_string(name) + "u: w" +
                         std::to_string(name)) != std::string::npos);
}

void a_truncated_operator_product_keeps_every_opening() {
  using cursiv::OperatorWords;
  auto first = OperatorWords::none(), second = first;
  for (unsigned i = 1; i <= 17; ++i) first.unite(OperatorWords::token(i));
  for (unsigned i = 1000; i < 1017; ++i) second.unite(OperatorWords::token(i));
  const auto product = first.then(second);
  for (unsigned i = 1; i <= 17; ++i) {
    const auto word = OperatorWords::token(i).then(OperatorWords::token(1000));
    CHECK(product.meets(word) && word.meets(product));
  }
  CHECK(!product.meets(OperatorWords::token(18)));
  CHECK(!product.then(OperatorWords::none()).meets(product));
  CHECK(!OperatorWords::none().then(product).meets(product));
  CHECK(OperatorWords::nothing().then(product).meets(product));
  CHECK(product.then(OperatorWords::nothing()).meets(product));
}

void long_refused_words_emit_bounded_functions() {
  cursiv::ByteSet bytes;
  bytes.add('a');
  cursiv::HeadMatcher head;
  head.words.emplace_back(8192, cursiv::HeadMatcher::Step{bytes});
  cursiv::ByteSet lead;
  lead.add(0xC3);
  head.words.back().push_back({{}, lead, true});
  // A decoded word boundary needs no separate scalar pool.
  const auto source = head.source("inline", "refuses", {"word, 1u", "nullptr"});
  CHECK(source.find("nullptr") == std::string::npos);
  CHECK(source.size() < 1024 * 1024);
  unsigned depth = 0, deepest = 0;
  for (const char c : source) {
    if (c == '{') deepest = std::max(deepest, ++depth);
    if (c == '}') --depth;
  }
  CHECK(depth == 0 && deepest < 16);
}

struct LexCfg : cursiv::LowerPools {
  std::uint16_t nterms = 0;
  cursiv::ByteSet wordBytes;
};

void named_heads_compare_the_scalars_after_the_same_lead_byte() {
  LexCfg g;
  g.nterms = 2;
  g.terms[1] = {};
  cursiv::ByteSet a;
  a.add('a');
  const auto plain = cursiv::lex_intern_set(g, a);
  cursiv::LexCodePoints e9, ea;
  e9.ranges.push_back({0xE9, 0xE9});
  ea.ranges.push_back({0xEA, 0xEA});
  const auto boundA = cursiv::lex_intern_cps_bound(g, a, e9);
  const auto boundB = cursiv::lex_intern_cps_bound(g, a, ea);
  CHECK(g.sets[boundA] == g.sets[boundB]);
  CHECK(cursiv::lex_decode_bytes(g, boundA) ==
        cursiv::lex_decode_bytes(g, boundB));
  const auto ch = g.push_lex({cursiv::LexKind::chars, plain});
  const auto body = [&](std::uint16_t bound) {
    const auto classes = g.push_lex_kid(bound);
    const auto guard =
        g.push_lex({cursiv::LexKind::nguard, cursiv::kNoSet, classes, 1});
    const auto base = g.push_lex_kid(ch);
    g.push_lex_kid(guard);
    return g.push_lex({cursiv::LexKind::seq, base, 2});
  };
  const auto rootA = body(boundA), rootB = body(boundB);
  g.termBuild[1].lex = rootB;
  const cursiv::NamedHead head(g);
  CHECK(head.read(rootB, 1) == 2);
  CHECK(head.read(rootA, 1) == 1);
  // Equal scalar lists at different offsets still describe the same class.
  const auto scalar = static_cast<std::uint32_t>(g.scalarRanges.size());
  g.scalarRanges[scalar] = {0xEA, 0xEA};
  const auto twin = g.nsets++;
  g.sets[twin] = g.sets[boundB];
  g.setDecode[twin] = g.setDecode[boundB];
  g.setScalars[twin] = scalar << 16 | 1u;
  const auto rootTwin = body(twin);
  CHECK(cursiv::NamedHead(g).read(rootTwin, 1) == 2);
}

void stray_expansion_keeps_predicate_cycles_and_their_exits() {
  LexCfg g;
  cursiv::ByteSet lead, rest, edge;
  lead.add(0xC2, 0xDF);
  rest.add(0x80, 0xBF);
  edge = rest;
  edge.add('b');
  const auto leadSet = cursiv::lex_intern_set(g, lead);
  const auto restSet = cursiv::lex_intern_set(g, rest);
  const auto edgeSet = cursiv::lex_intern_set(g, edge);
  const auto classes = g.push_lex_kid(restSet);
  const auto word = g.push_lex({.kind = cursiv::LexKind::nguard,
                                .a = cursiv::kNoSet,
                                .b = classes,
                                .min = 1});
  cursiv::LexNfaPool n;
  const int cycle = n.hole(), match = n.match(1);
  int resumed = cycle;
  constexpr unsigned depth = 8000;
  for (unsigned i = 0; i < depth; ++i) {
    const int relay = static_cast<int>(n.relays.size());
    cursiv::LexRelayBuild assertion;
    assertion.row.predicate = 0;
    assertion.resumeOut = resumed;
    n.relays.push_back(assertion);
    resumed = n.relay(relay);
  }
  const int reads = n.chars(edgeSet, match);
  const int exits = n.split(match, reads);
  n.s[cycle].t1 = n.split(resumed, exits);
  const int start = n.chars_stray(leadSet, word, cycle);
  cursiv::LexStrayExpansion(n, g).run();
  CHECK(!g.lowerFail());
  CHECK(n.relays.size() == 2 * depth);
  // Follow successful predicates as zero-width edges, including the loop.
  // Both the bounded empty exit and the byte outside the scalar survive.
  std::vector<bool> seen(n.s.size());
  std::vector<int> todo{n.s[start].t1};
  unsigned predicates = 0, acceptances = 0, bytes = 0;
  while (!todo.empty()) {
    const int at = todo.back();
    todo.pop_back();
    if (at < 0 || seen[at]) continue;
    seen[at] = true;
    const auto st = n.s[at];
    if (st.set >= 0) {
      CHECK(g.sets[st.set].has('b'));
      CHECK(!g.sets[st.set].intersects(rest));
      CHECK(n.s[st.t1].accept == 1);
      ++bytes;
      continue;
    }
    if (st.accept >= 0) {
      CHECK(st.accept == 1 && st.strayBound && st.bound == restSet);
      ++acceptances;
    }
    if (st.relay >= 0) {
      todo.push_back(n.relays[st.relay].resumeOut);
      ++predicates;
    }
    todo.push_back(st.t1);
    todo.push_back(st.t2);
  }
  CHECK(predicates == depth && acceptances == 1 && bytes == 1);
}

template <class A>
void backtracking_modes_require_an_accepted_prefix_and_a_failed_cycle() {
  A a;
  a.ndfa = 15;
  a.ncls = 2;
  a.nmodes = 7;
  for (unsigned s = 0; s < a.ndfa; ++s) {
    a.dfaAccept[s] = 0;
    a.dfaBound[s] = cursiv::kNoSet;
    a.dfaRelay[s] = cursiv::kNoRelay;
    if constexpr (requires { a.lexChecks; }) a.lexChecks[s] = 0;
    for (unsigned c = 0; c < a.ncls; ++c)
      a.dfaNext[s * a.ncls + c] = cursiv::kDeadState;
  }
  const auto edge = [&](unsigned from, unsigned c, std::uint16_t to) {
    a.dfaNext[from * a.ncls + c] = to;
  };
  // ID+, a quoted token, a short token beside a longer failed token, and
  // the same backup across a cycle of two different states.
  edge(0, 0, 1);
  edge(1, 0, 1);
  edge(2, 0, 3);
  edge(3, 0, 3);
  edge(3, 1, 4);
  edge(5, 0, 6);
  edge(6, 0, 7);
  edge(7, 0, 7);
  edge(7, 1, 8);
  edge(9, 0, 10);
  edge(10, 0, 11);
  edge(11, 0, 12);
  edge(12, 0, 11);
  // A cycle accepting unconditionally every other byte has bounded backup.
  edge(13, 0, 14);
  edge(14, 0, 13);
  for (unsigned s : {1u, 4u, 6u, 8u, 10u, 13u}) a.dfaAccept[s] = 1;
  const std::uint16_t starts[] = {0, 2, 5, 9, 7, 8, 13};
  for (unsigned m = 0; m < a.nmodes; ++m) a.modeStart[m] = starts[m];
  const std::vector<std::uint8_t> want{0, 0, 1, 1, 0, 0, 0};
  std::vector<std::uint8_t> cycles{42};
  CHECK(cursiv::lex_backtrack_modes(a, &cycles) == want);
  std::vector<std::uint8_t> wantCycles(a.ndfa, 0);
  for (unsigned state : {3u, 7u, 11u, 12u}) wantCycles[state] = 1;
  CHECK(cycles == wantCycles);
  // A relay excludes only the paths that reach it; the independent mode
  // with a failed cycle of two states still needs shared suffixes.
  if constexpr (requires { a.nrelays; })
    a.nrelays = 1;
  else
    a.relays.push_back({});
  a.dfaRelay[7] = 0;
  auto relayed = want;
  relayed[2] = 0;
  CHECK(cursiv::lex_backtrack_modes(a, &cycles) == relayed);
  wantCycles[7] = 0;
  CHECK(cycles == wantCycles);
  a.dfaRelay[7] = cursiv::kNoRelay;
  if constexpr (requires { a.nrelays; })
    a.nrelays = 0;
  else
    a.relays.clear();
  // A mode that reads a refusing terminal never shares suffixes; the others
  // keep their own answer.
  a.modeHeads.assign(a.nmodes, {});
  a.modeHeads[2].set(1);
  auto refusing = want;
  refusing[2] = 0;
  CHECK(cursiv::lex_backtrack_modes(a, &cycles) == refusing);
  wantCycles[7] = 1;
  CHECK(cycles == wantCycles);
  a.modeHeads[3].set(1);
  CHECK(cursiv::lex_backtrack_modes(a, &cycles).empty());
  CHECK(cycles.empty());
  a.modeHeads.clear();

  // A boundary or a conditional election removes the guarantee, even when
  // the state still names a terminal. Other modes keep their own answer.
  a.dfaBound[1] = 0;
  auto bounded = want;
  bounded[0] = 1;
  CHECK(cursiv::lex_backtrack_modes(a) == bounded);
  a.dfaBound[1] = cursiv::kNoSet;
  if constexpr (requires { a.lexChecks; }) {
    a.lexChecks[1] = 1;
    CHECK(cursiv::lex_backtrack_modes(a) == bounded);
    a.lexChecks[1] = 0;
  }
  a.dfaBound[13] = 0;
  auto periodic = want;
  periodic[6] = 1;
  CHECK(cursiv::lex_backtrack_modes(a) == periodic);
  a.dfaBound[13] = cursiv::kNoSet;
  a.dfaAccept[6] = a.dfaAccept[10] = 0;
  // No prefix before the two remaining failed cycles can be retained.
  // An unaffected table emits no mode metadata or cache specialization.
  cycles = {42};
  CHECK(cursiv::lex_backtrack_modes(a, &cycles).empty());
  CHECK(cycles.empty());
}

struct CountedBacktrackGraph {
  unsigned ndfa = 0, ncls = 2, nmodes = 2;
  std::vector<std::uint16_t> dfaAccept, dfaBound, modeStart;
  mutable std::uint64_t steps = 0;
  std::uint16_t next_dfa(unsigned s, unsigned) const {
    ++steps;
    return static_cast<std::uint16_t>(s + 1 < ndfa ? s + 1 : ndfa - 2);
  }
};

void backtracking_analysis_handles_long_paths_with_linear_work() {
  for (unsigned count : {8000u, 32000u}) {
    CountedBacktrackGraph a;
    a.ndfa = count;
    a.dfaAccept.assign(count, 0);
    a.dfaBound.assign(count, cursiv::kNoSet);
    a.dfaAccept[1] = 1;
    a.modeStart = {0, 2};
    CHECK((cursiv::lex_backtrack_modes(a) == std::vector<std::uint8_t>{1, 0}));
    CHECK(a.steps <= 4u * std::uint64_t{count} * a.ncls);
  }
}

void a_longer_mode_cannot_consume_the_last_slot_twice() {
  LexCfg g;
  cursiv::LexModes modes(g);
  constexpr unsigned count = 65534;
  modes.set.resize(count);
  modes.longer.resize(count);
  modes.longerMode.resize(count, cursiv::kNoMode);
  modes.tag.resize(count);
  for (unsigned i = 0; i < count; ++i) modes.tag[i] = i + 1;
  cursiv::DynSet want, beyond;
  want.set(1);
  beyond.set(2);
  bool refused = false;
  try {
    modes.intern(want, 0, beyond);
  } catch (const cursiv::LowerRefused& f) {
    refused = f.why == cursiv::LowerFail::tokenGraphTooLarge;
  }
  CHECK(refused && modes.set.size() == cursiv::kNoMode);
  CHECK(modes.intern(beyond) == count);
}

}  // namespace astgen

void ast_generation_cases_run() {
  astgen::nested_template_arguments_remain_qualified();
  astgen::reflection_only_types_need_no_factory(
      cursiv::lr_ast_registry<astgen::G>());
  astgen::reflection_only_types_need_no_factory(
      cursiv::ll_ast_registry<astgen::G>());
  astgen::a_truncated_operator_product_keeps_every_opening();
  astgen::long_refused_words_emit_bounded_functions();
  astgen::named_heads_compare_the_scalars_after_the_same_lead_byte();
  astgen::a_longer_mode_cannot_consume_the_last_slot_twice();
  astgen::stray_expansion_keeps_predicate_cycles_and_their_exits();
  astgen::backtracking_modes_require_an_accepted_prefix_and_a_failed_cycle<
      cursiv::LlBuiltProgram>();
  astgen::backtracking_modes_require_an_accepted_prefix_and_a_failed_cycle<
      cursiv::LrBuildTable>();
  astgen::backtracking_analysis_handles_long_paths_with_linear_work();
}

namespace write_source_cases {

// A generated file whose text did not move is left as it is, whatever its
// size; one that moved is written, in whole.

namespace {

namespace fs = std::filesystem;

std::string seen(const fs::path& path) {
  return cursiv::test::read_file(path.string());
}

}  // namespace

void run() {
  const fs::path dir =
      fs::temp_directory_path() /
      ("cursiv-write-source-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string path = (dir / "Table.hpp").string();

  CHECK(cursiv::write_source(path, "one"));
  CHECK(seen(path) == "one");

  // Unmoved: the file keeps the time it was written at.
  fs::last_write_time(path, fs::last_write_time(path) - std::chrono::hours(1));
  const auto then = fs::last_write_time(path);
  CHECK(cursiv::write_source(path, "one"));
  CHECK(fs::last_write_time(path) == then);

  // Moved, at the same size and at another.
  CHECK(cursiv::write_source(path, "two"));
  CHECK(seen(path) == "two" && fs::last_write_time(path) != then);
  CHECK(cursiv::write_source(path, "three and more"));
  CHECK(seen(path) == "three and more");
  CHECK(cursiv::write_source(path, "short"));
  CHECK(seen(path) == "short");

  // Empty text, over a file and over none; and bytes a text reader would fold.
  CHECK(cursiv::write_source(path, ""));
  CHECK(fs::file_size(path) == 0);
  CHECK(cursiv::write_source(path, ""));
  const std::string folded("a\r\nb\0c", 6);
  CHECK(cursiv::write_source(path, folded));
  CHECK(cursiv::write_source(path, folded));
  CHECK(seen(path) == folded);
  CHECK(cursiv::write_source((dir / "New.hpp").string(), ""));
  CHECK(fs::exists(dir / "New.hpp"));

  // A path that cannot be written says so.
  CHECK(!cursiv::write_source((dir / "missing" / "Table.hpp").string(), "x"));

  fs::remove_all(dir);
}

}  // namespace write_source_cases

int main() {
  cursiv::test::count_expectations();
  ast_generation_cases_run();
  write_source_cases::run();
  return cursiv::test::report("AstGeneration_test");
}
