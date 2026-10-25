// The units both generators write alike: the grammar's text, its AST and
// record facets, and its module. An engine writes its table and its parser.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/GrammarDump.hpp"
#include "cursiv/engines/shared/gen/AstEntryTable.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/GenCode.hpp"
#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"

namespace cursiv {

struct CommonArtifacts {
  std::string_view ns;
  std::string_view header;
  const GrammarDump& dump;
  const AstRegistry& registry;
  const AstEntryTable& entries;
  const std::vector<RecordGen>& records;
  const std::vector<RecordTerminal>& terminals;
  const LanguageIdentity& lang;
  // The engine's parser the module installs: its header and its class.
  std::string_view parserHeader;
  std::string_view parserClass;
};

// The first unit that fails to land names the code.
GenCode write_common_artifacts(const std::string& dir,
                               const CommonArtifacts& what);

}  // namespace cursiv
