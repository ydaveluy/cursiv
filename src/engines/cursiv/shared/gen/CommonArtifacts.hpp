// The units a generator writes: the engine's table and parser, then what both
// engines write the same -- the grammar's text, its AST and record facets, its
// module, and the keywords of the editor extension's TextMate file.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/AstEntryTable.hpp"
#include "cursiv/shared/gen/AstRegistry.hpp"
#include "cursiv/shared/gen/EmitRecords.hpp"
#include "cursiv/shared/gen/GenCode.hpp"
#include "cursiv/shared/gen/LanguageIdentity.hpp"
#include "cursiv/shared/gen/TextMateKeywords.hpp"
#include "cursiv/shared/utils/GrammarDump.hpp"

namespace cursiv::engine {

// The units a parser's code spreads over, `ParserRules<k>.cpp` or
// `ParserGroups<k>.cpp`, all written, empty where unused.
// cmake/CursivGrammar.cmake reads the count from the line below.
inline constexpr unsigned kParserUnits = 8;

struct CommonArtifacts {
  // The engine's table, its parser's declaration, and the units the parser is
  // written in, each a file name and its text.
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
  // The TextMate file of the editor extension, empty for none, and the
  // keywords written into it. A file that does not exist is left alone.
  std::string_view textmate;
  std::vector<TextMateKeyword> keywords;
  // The engine's parser the module installs: its header and its class.
  std::string_view parserHeader;
  std::string_view parserClass;
};

// The first unit that fails to write gives the code.
GenCode write_common_artifacts(const std::string& dir,
                               const CommonArtifacts& what);

}  // namespace cursiv::engine
