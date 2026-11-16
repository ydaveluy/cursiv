// The corpus generators, one per shipped grammar.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What a throughput bench needs: a stopwatch and deterministic generators.
// Keep it small.

namespace bench {

// The stopwatch. The measurement loops stay in the tools, whose protocols
// differ on purpose.
inline double seconds_of(auto&& fn) {
  const auto t0 = std::chrono::steady_clock::now();
  fn();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
      .count();
}

// splitmix64, never seeded from the clock: a given size produces a
// byte-identical document on every run and every machine, or two measurements
// are not of the same thing.
inline std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// An identifier whose first letter rotates through the alphabet, so names do
// not all start like a keyword.
inline std::string makeIdent(std::string_view prefix, std::size_t index,
                             std::uint64_t seed) {
  static constexpr char firstLetters[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static constexpr char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::uint64_t state = seed;
  std::string id(1,
                 firstLetters[splitmix64(state) % (sizeof(firstLetters) - 1)]);
  id += prefix;
  // The decimal digits make a collision with a reserved word impossible.
  id += std::to_string(index);
  // 8..16, and fixed: shorter identifiers would make the lexer look faster
  // than it is.
  const std::size_t targetLen =
      8 + static_cast<std::size_t>(splitmix64(state) % 9);
  if (id.size() > 16) id.resize(16);
  while (id.size() < targetLen)
    id += alphabet[splitmix64(state) % (sizeof(alphabet) - 1)];
  return id;
}

}  // namespace bench
// Synthetic arithmetics-source generator: expressions nested 3 to 5
// operators deep over all six operators, so the precedence climb is walked
// past its last level -- the hot shape no other generator here produces.

namespace bench {

constexpr std::size_t kArithNumDefs = 30;

struct ArithFuncInfo {
  std::string name;
  int arity;  // number of declared parameters, 0..2
};

// A leaf expression: a number, a parameter in scope, or a call to an earlier
// function with number arguments.
inline std::string genArithLeaf(std::uint64_t& state,
                                const std::vector<std::string>& params,
                                const std::vector<ArithFuncInfo>& funcs) {
  std::uint64_t r = bench::splitmix64(state);
  int choice = static_cast<int>(r % 3);
  if (choice == 0 || (params.empty() && funcs.empty())) {
    std::uint64_t r2 = bench::splitmix64(state);
    int intPart = 1 + static_cast<int>(r2 % 100);
    if (r2 % 7 == 0) {
      std::uint64_t r3 = bench::splitmix64(state);
      int frac = static_cast<int>(r3 % 1000);
      return std::to_string(intPart) + "." + std::to_string(frac);
    }
    return std::to_string(intPart);
  }
  // A parameter when one is asked, or when no function is declared yet.
  if ((choice == 1 && !params.empty()) || funcs.empty()) {
    return params[bench::splitmix64(state) % params.size()];
  }
  const ArithFuncInfo& f = funcs[bench::splitmix64(state) % funcs.size()];
  std::string s = f.name;
  if (f.arity > 0) {
    s += "(";
    for (int a = 0; a < f.arity; ++a) {
      if (a) s += ",";
      std::uint64_t r4 = bench::splitmix64(state);
      s += std::to_string(1 + static_cast<int>(r4 % 50));
    }
    s += ")";
  }
  return s;
}

// Builds an expression with exactly `budget` binary operators, recursively
// splitting the remaining budget between the left and right operand --
// bounded and deterministic.
inline std::string genArithExpr(std::uint64_t& state, int budget,
                                const std::vector<std::string>& params,
                                const std::vector<ArithFuncInfo>& funcs) {
  if (budget <= 0) return genArithLeaf(state, params, funcs);
  static constexpr const char* ops[] = {"%", "^", "*", "/", "+", "-"};
  int op = static_cast<int>(bench::splitmix64(state) % 6);
  int leftBudget =
      budget > 1 ? static_cast<int>(bench::splitmix64(state) % budget) : 0;
  int rightBudget = budget - 1 - leftBudget;
  if (rightBudget < 0) rightBudget = 0;
  std::string l = genArithExpr(state, leftBudget, params, funcs);
  std::string r = genArithExpr(state, rightBudget, params, funcs);
  std::string expr = l + " " + ops[op] + " " + r;
  if (bench::splitmix64(state) % 4 == 0) expr = "(" + expr + ")";
  return expr;
}

// A valid arithmetics source of about `targetBytes`, always the same: a pool of
// `def` statements, then evaluations that call into them.
inline std::string generateArithmeticsSource(std::size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 4096);
  // Lowercase deliberately: the module name is the only identifier the
  // canonical dump prints, so an already-lowercase literal keeps that dump
  // byte-comparable whatever a grammar's ID terminal does to case.
  out += "module bench\n\n";

  std::vector<ArithFuncInfo> funcs;
  funcs.reserve(kArithNumDefs);

  for (std::size_t i = 0; i < kArithNumDefs; ++i) {
    std::string name = bench::makeIdent("f", i, 5000 + i);
    std::uint64_t state = 6000 + i;
    int arity = static_cast<int>(bench::splitmix64(state) % 3);  // 0..2
    std::vector<std::string> params;
    params.reserve(static_cast<std::size_t>(arity));
    for (int p = 0; p < arity; ++p)
      params.push_back(bench::makeIdent("p", static_cast<std::size_t>(p),
                                        7000 + i * 10 + p));
    int depth = 3 + static_cast<int>(bench::splitmix64(state) % 3);  // 3..5
    std::string body = genArithExpr(state, depth, params, funcs);

    out += "def ";
    out += name;
    if (!params.empty()) {
      out += "(";
      for (std::size_t p = 0; p < params.size(); ++p) {
        if (p) out += ",";
        out += params[p];
      }
      out += ")";
    }
    out += ": ";
    out += body;
    out += ";\n";

    funcs.push_back({std::move(name), arity});
  }
  out += "\n";

  std::string evalText;
  evalText.reserve(targetBytes);
  std::size_t i = 0;
  while (out.size() + evalText.size() < targetBytes) {
    std::uint64_t state = 8000 + i;
    int depth = 3 + static_cast<int>(bench::splitmix64(state) % 3);  // 3..5
    evalText += genArithExpr(state, depth, {}, funcs);
    evalText += ";\n";
    ++i;
  }
  out += evalText;
  return out;
}

}  // namespace bench
// Domain-model source: a chain of entities in one package, each extending the
// previous one and holding a feature that names it. Names are sequential, so
// the source is always the same.

namespace bench {

// Generates a valid domain-model source of approximately `targetBytes`. The
// last entity is emitted whole, so the result overshoots the target by at most
// one entity plus the closing brace.
inline std::string generateDomainModelSource(std::size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 1024);
  out += "datatype String\n";
  out += "package bench {\n";

  std::size_t index = 0;
  while (out.size() < targetBytes) {
    out += "  entity Entity" + std::to_string(index);
    if (index > 0) out += " extends Entity" + std::to_string(index - 1);
    out += " {\n";
    out += "    name: String\n";
    if (index > 0) out += "    prev: Entity" + std::to_string(index - 1) + "\n";
    out += "  }\n";
    ++index;
  }

  out += "}\n";
  return out;
}

}  // namespace bench
// Per-file generators for the multi-document lane, with names unique across
// files. `intra`: each file resolves its own references; `cross`: file 0
// declares a pool the others name through the workspace index.

namespace bench {

// statemachine, intra: self-contained -- 64 events, 64 commands, a chain of
// states, and the last state closing the loop back onto the first, so the
// file resolves 100% of its own references.
inline std::string multidocStatemachineFile(std::size_t fileIndex,
                                            std::size_t targetBytes) {
  constexpr std::size_t kEvents = 64;
  constexpr std::size_t kCommands = 64;
  const std::string tag = std::to_string(fileIndex);
  std::string out;
  out.reserve(targetBytes + 4096);
  out += "statemachine Bench" + tag + "\nevents";
  for (std::size_t i = 0; i < kEvents; ++i)
    out += " Ev" + tag + "_" + std::to_string(i);
  out += "\ncommands";
  for (std::size_t i = 0; i < kCommands; ++i)
    out += " Cmd" + tag + "_" + std::to_string(i);
  out += "\ninitialState St" + tag + "_0\n";

  std::size_t state = 0;
  while (out.size() < targetBytes) {
    out += "state St" + tag + "_" + std::to_string(state) + " actions { Cmd" +
           tag + "_" + std::to_string(state % kCommands) + " }\n";
    out += "Ev" + tag + "_" + std::to_string(state % kEvents) + " => St" + tag +
           "_" + std::to_string(state + 1) + "\n";
    out += "end\n";
    ++state;
  }
  // The last state closes the loop, which is what makes the corpus resolve
  // whole: the previous state named `state+1`, and this is it.
  out += "state St" + tag + "_" + std::to_string(state) + " actions { Cmd" +
         tag + "_0 }\nEv" + tag + "_0 => St" + tag + "_0\nend\n";
  return out;
}

// domainmodel, intra: one package per file, a chain of entities each
// extending the previous one and holding a feature that names it -- every
// reference is a walk up this file's own containment.
inline std::string multidocDomainModelFile(std::size_t fileIndex,
                                           std::size_t targetBytes) {
  const std::string tag = std::to_string(fileIndex);
  std::string out;
  out.reserve(targetBytes + 1024);
  out += "datatype String" + tag + "\n";
  out += "package bench" + tag + " {\n";
  std::size_t index = 0;
  while (out.size() < targetBytes) {
    out += "  entity Entity" + std::to_string(index);
    if (index > 0) out += " extends Entity" + std::to_string(index - 1);
    out += " {\n    name: String" + tag + "\n";
    if (index > 0) out += "    prev: Entity" + std::to_string(index - 1) + "\n";
    out += "  }\n";
    ++index;
  }
  out += "}\n";
  return out;
}

// requirements, cross: file 0 is the pool and nothing else declares an
// environment, so every reference in every other file misses its local
// scope -- exactly the path this regime is here to measure.
inline constexpr std::size_t kMultidocEnvironments = 24;

inline std::string multidocRequirementsPool(std::size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 1024);
  out += "contact: \"bench\"\n";
  for (std::size_t i = 0; i < kMultidocEnvironments; ++i)
    out += "environment Env" + std::to_string(i) + ": \"Environment " +
           std::to_string(i) + "\"\n";
  std::size_t index = 0;
  while (out.size() < targetBytes) {
    out += "req Pool" + std::to_string(index) + " \"Pool requirement " +
           std::to_string(index) + "\" applicable for Env" +
           std::to_string(index % kMultidocEnvironments) + ", Env" +
           std::to_string((index + 1) % kMultidocEnvironments) + "\n";
    ++index;
  }
  return out;
}

inline std::string multidocRequirementsFile(std::size_t fileIndex,
                                            std::size_t targetBytes) {
  if (fileIndex == 0) return multidocRequirementsPool(targetBytes);
  const std::string tag = std::to_string(fileIndex);
  std::string out;
  out.reserve(targetBytes + 1024);
  std::size_t index = 0;
  while (out.size() < targetBytes) {
    out += "req R" + tag + "_" + std::to_string(index) + " \"Requirement " +
           std::to_string(index) + "\" applicable for Env" +
           std::to_string(index % kMultidocEnvironments) + ", Env" +
           std::to_string((index + 1) % kMultidocEnvironments) + "\n";
    ++index;
  }
  return out;
}

}  // namespace bench
// Requirements source (`.req`): a contact header, 24 environments, then
// requirements naming two of them each. The `.tst` language shares its rules,
// so it has no corpus of its own.

namespace bench {

constexpr std::size_t kReqNumEnvironments = 24;

// Generates a valid requirements source of approximately `targetBytes`. The
// last requirement is emitted whole, so the result overshoots the target by at
// most one line.
inline std::string generateRequirementsSource(std::size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 1024);
  out += "contact: \"bench\"\n";
  for (std::size_t i = 0; i < kReqNumEnvironments; ++i)
    out += "environment Env" + std::to_string(i) + ": \"Environment " +
           std::to_string(i) + "\"\n";

  std::size_t index = 0;
  while (out.size() < targetBytes) {
    out += "req REQ" + std::to_string(index) + " \"Requirement " +
           std::to_string(index) + "\" applicable for Env" +
           std::to_string(index % kReqNumEnvironments) + ", Env" +
           std::to_string((index + 1) % kReqNumEnvironments) + "\n";
    ++index;
  }

  return out;
}

}  // namespace bench
// Synthetic statemachine-source generator, shared by the throughput benches and
// the recovery metric.

namespace bench {

constexpr std::size_t kSmNumEvents = 50;
constexpr std::size_t kSmNumCommands = 20;

// A valid statemachine source of about `targetBytes`, always the same. A
// transition may name a state ahead of it.
inline std::string generateStatemachineSource(std::size_t targetBytes) {
  std::string out;
  out.reserve(targetBytes + 4096);

  out += "statemachine Bench\n\n";

  std::vector<std::string> eventNames;
  eventNames.reserve(kSmNumEvents);
  out += "events\n";
  for (std::size_t i = 0; i < kSmNumEvents; ++i) {
    std::string name = bench::makeIdent("e", i, 1000 + i);
    out += "  " + name + "\n";
    eventNames.push_back(std::move(name));
  }
  out += "\n";

  std::vector<std::string> commandNames;
  commandNames.reserve(kSmNumCommands);
  out += "commands\n";
  for (std::size_t i = 0; i < kSmNumCommands; ++i) {
    std::string name = bench::makeIdent("cmd", i, 2000 + i);
    out += "  " + name + "\n";
    commandNames.push_back(std::move(name));
  }
  out += "\n";

  out += "initialState s0\n\n";

  // State names are created lazily on first reference (either as the state
  // being emitted, or as a transition target ahead of the current state).
  std::vector<std::string> stateNames;
  auto stateName = [&](std::size_t i) -> const std::string& {
    while (stateNames.size() <= i) {
      std::size_t idx = stateNames.size();
      stateNames.push_back(idx == 0 ? std::string("s0")
                                    : bench::makeIdent("s", idx, 3000 + idx));
    }
    return stateNames[i];
  };

  std::string statesText;
  statesText.reserve(targetBytes);
  std::size_t i = 0;
  while (out.size() + statesText.size() < targetBytes) {
    const std::string& name = stateName(i);
    std::uint64_t rngState = 4000 + i;
    std::size_t numTransitions =
        2 + static_cast<std::size_t>(bench::splitmix64(rngState) % 3);  // 2..4

    statesText += "state ";
    statesText += name;
    statesText += "\n  actions { ";
    statesText += commandNames[i % kSmNumCommands];
    statesText += " }\n";
    for (std::size_t t = 0; t < numTransitions; ++t) {
      const std::string& ev = eventNames[(i * 7 + t * 13) % kSmNumEvents];
      const std::string& target = stateName(i + 1 + (t % 3));
      statesText += "  ";
      statesText += ev;
      statesText += " => ";
      statesText += target;
      statesText += "\n";
    }
    statesText += "end\n\n";

    ++i;
  }

  out += statesText;
  return out;
}

}  // namespace bench
