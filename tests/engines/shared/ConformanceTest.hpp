#pragma once

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/Check.hpp"
#include "support/AstText.hpp"

namespace conformance_test {

inline constexpr const char* kEngine[] = {"LL", "LR"};

// Prefixes select entries of the same two generated languages. Reuse their
// parsers across cases, as a language service does across documents.
inline const auto& engines() {
  static const std::array<std::unique_ptr<cursiv::Parser>, 2> parsers{
      tconformance::generated::Ll::create_parser(),
      tconformance::generated::Lr::create_parser()};
  return parsers;
}

struct Pair {
  std::string_view prefix;

  const auto& operator[](std::size_t i) const { return engines()[i]; }
  auto begin() const { return engines().begin(); }
  auto end() const { return engines().end(); }
  std::string source(std::string_view text) const {
    return std::string(prefix) + std::string(text);
  }
};

inline bool clean(const cursiv::ParseResult& parsed) {
  return parsed.accepted && parsed.diagnostics.empty() &&
         parsed.error == cursiv::AstBuildError::None;
}

// Both engines build one tree: the same records, spans and standing.
inline void one_tree(const Pair& pair, std::string_view text) {
  const std::string source = pair.source(text);
  const std::string ll = support::records(pair[0]->parse(source).cst());
  const std::string lr = support::records(pair[1]->parse(source).cst());
  if (ll != lr)
    std::printf("[%s]\n  LL %s\n  LR %s\n", source.c_str(), ll.c_str(),
                lr.c_str());
  CHECK(ll == lr);
}

inline void expect(const Pair& pair, std::string_view text, bool want) {
  for (unsigned i = 0; i < 2; ++i) {
    const std::string source = pair.source(text);
    const bool got = clean(pair[i]->parse(source));
    if (got != want)
      std::printf("%s [%.*s]: wanted %s\n", kEngine[i],
                  static_cast<int>(text.size()), text.data(),
                  want ? "read" : "refused");
    CHECK(got == want);
  }
}

}  // namespace conformance_test
