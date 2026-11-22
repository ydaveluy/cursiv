#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "conformancelr/generated/Suite/Module.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/CstAudit.hpp"

namespace conformance_test {

// Prefixes select entries of the generated language. Its parser is reused
// across cases, as a language service does across documents.
inline const cursiv::Parser& parser() {
  static const auto instance = tconformance::generated::Suite::createParser();
  return *instance;
}

struct Entry {
  std::string_view prefix;

  std::string source(std::string_view text) const {
    return std::string(prefix) + std::string(text);
  }
};

inline bool clean(const cursiv::ParseResult& parsed) {
  return parsed.accepted && parsed.diagnostics.empty() &&
         parsed.error == cursiv::AstBuildError::None;
}

// The tree a text builds keeps the structural laws of a syntax tree.
inline void one_tree(const Entry& entry, std::string_view text) {
  const std::string source = entry.source(text);
  const auto parsed = parser().parse(source);
  checks::audit_cst_structure(parsed, source, [&](const char* breach) {
    std::printf("[%s]\n  %s\n", source.c_str(), breach);
    CHECK(false);
  });
}

inline void expect(const Entry& entry, std::string_view text, bool want) {
  const std::string source = entry.source(text);
  const bool got = clean(parser().parse(source));
  if (got != want)
    std::printf("[%.*s]: wanted %s\n", static_cast<int>(text.size()),
                text.data(), want ? "read" : "refused");
  CHECK(got == want);
}

}  // namespace conformance_test
