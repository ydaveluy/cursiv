// The units a generator writes: the engine's table and parser, then what both
// engines write alike -- the grammar's text, its AST and record facets, and its
// module.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/GrammarDump.hpp"
#include "cursiv/engines/shared/gen/AstEntryTable.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/GenCode.hpp"
#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"

namespace cursiv {

// The units a parser's code spreads over, `ParserRules<k>.cpp` or
// `ParserGroups<k>.cpp`. The build lists them before a generator runs, so
// every grammar writes them all, the ones its parser does not fill empty.
inline constexpr unsigned kParserUnits = 8;

struct CommonArtifacts {
  // The engine's table, its parser's declaration, and the units the parser is
  // spelled in, each a file name and its text.
  std::string table;
  std::string parser;
  std::span<const std::pair<std::string, std::string>> parserUnits;
  std::string_view ns;
  std::string_view header;
  GrammarDump dump;
  const AstRegistry& registry;
  const AstEntryTable& entries;
  const std::vector<RecordGen>& records;
  std::vector<TerminalGen> terminals;
  LanguageIdentity lang;
  // The engine's parser the module installs: its header and its class.
  std::string_view parserHeader;
  std::string_view parserClass;
};

// The first unit that fails to land names the code.
GenCode write_common_artifacts(const std::string& dir,
                               const CommonArtifacts& what);

}  // namespace cursiv
