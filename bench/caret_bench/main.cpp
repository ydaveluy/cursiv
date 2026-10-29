// caret_bench -- what a completion costs in the middle of a large document:
// one parse, then `nextFeaturesAt` asked at a few carets, reps times. Run it
// under `perf stat` with 0 and with N reps: the difference is the questions.
//
//   caret_bench <ll|lr> <statements> [reps] [block] [--print]
//
// `block` statements make one loop at the top level; 0 leaves them all there.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "benchcaretll/generated/Ll/Module.hpp"
#include "benchcaretlr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"

namespace {

std::string statement(std::size_t i) {
  const std::string n = std::to_string(i);
  switch (i % 4) {
    case 0:
      return "let v" + n + " = (a" + n + " + 3) * b" + n + ";\n";
    case 1:
      return "for i" + n + " in v" + n + " + 1 {\n  c" + n + " * 2;\n}\n";
    case 2:
      return "// note " + n + "\nd" + n + " - 7;\n";
    default:
      return "let w" + n + " = x" + n + " / (y" + n + " - 1);\n";
  }
}

std::string document(std::size_t statements, std::size_t block) {
  std::string text;
  text.reserve(statements * 40);
  for (std::size_t i = 0; i < statements; ++i) {
    if (block != 0 && i % block == 0)
      text += "for g" + std::to_string(i) + " in u {\n";
    text += statement(i);
    if (block != 0 && (i + 1) % block == 0) text += "}\n";
  }
  if (block != 0 && statements % block != 0) text += "}\n";
  return text;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view engine = argc > 1 ? argv[1] : "lr";
  const std::size_t statements =
      argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 20000;
  const int reps = argc > 3 ? std::atoi(argv[3]) : 20;
  const std::size_t block =
      argc > 4 && argv[4][0] != '-' ? std::strtoul(argv[4], nullptr, 10) : 200;
  bool print = false;
  for (int i = 4; i < argc; ++i)
    print = print || std::string_view(argv[i]) == "--print";
  if (engine != "ll" && engine != "lr") {
    std::fprintf(stderr, "caret_bench: the engine is ll or lr\n");
    return 2;
  }
  const std::unique_ptr<cursiv::Parser> parser =
      engine == "ll" ? tcaretphrase::generated::Ll::create_parser()
                     : tcaretphrase::generated::Lr::create_parser();
  const std::string text = document(statements, block);
  const cursiv::ParseResult parsed = parser->parse(text);
  if (!parsed.accepted || !parsed.diagnostics.empty()) {
    std::fprintf(stderr, "caret_bench: the document is refused\n");
    return 1;
  }

  // Between two statements, where an expression starts, and past an operator.
  const std::size_t middle = text.find("let v", text.size() / 2);
  const std::vector<cursiv::TextOffset> carets = {
      static_cast<cursiv::TextOffset>(text.find('\n', middle)),
      static_cast<cursiv::TextOffset>(text.find("= ", middle) + 2),
      static_cast<cursiv::TextOffset>(text.find('+', middle) + 1)};

  std::vector<cursiv::NextFeature> next;
  if (print)
    for (const cursiv::TextOffset at : carets) {
      next.clear();
      parser->nextFeaturesAt(parsed.cst(), at, at, next);
      std::printf("at %u:", static_cast<unsigned>(at));
      for (const cursiv::NextFeature& f : next) {
        const auto spellings =
            parser->getGrammar().terminalSpellings(f.feature);
        const std::string_view shown =
            spellings.size() == 1
                ? spellings.front()
                : parser->getGrammar().terminalName(f.feature);
        std::printf(" %.*s:%.*s", static_cast<int>(shown.size()), shown.data(),
                    static_cast<int>(f.property.size()), f.property.data());
      }
      std::printf("\n");
    }

  std::size_t sink = 0;
  for (const cursiv::TextOffset at : carets) {
    next.clear();
    parser->nextFeaturesAt(parsed.cst(), at, at, next);
    sink += next.size();
  }
  const auto t0 = std::chrono::steady_clock::now();
  for (int r = 0; r < reps; ++r)
    for (const cursiv::TextOffset at : carets) {
      next.clear();
      parser->nextFeaturesAt(parsed.cst(), at, at, next);
      sink += next.size();
    }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
          .count();
  const double calls = static_cast<double>(reps) * carets.size();
  std::printf(
      "%.*s statements %zu bytes %zu calls %.0f us/call %.1f sink %zu\n",
      static_cast<int>(engine.size()), engine.data(), statements, text.size(),
      calls, calls != 0 ? seconds * 1e6 / calls : 0.0, sink);
  return 0;
}
