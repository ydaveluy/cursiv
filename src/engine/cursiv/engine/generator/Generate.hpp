// What a language's generator runs: the grammar read, its parser built, and
// the sources of the language written into a directory.
#pragma once

#include <string_view>

#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/engine/grammar/GrammarImage.hpp"

namespace cursiv::engine::gen {

// The language as its build names it: the names its sources are written
// under, the header that declares its grammar, and how its files are told.
struct LanguageNames {
  // The name the generator's messages begin with.
  std::string_view name;
  // The namespace of the generated sources.
  std::string_view ns;
  std::string_view header;
  std::string_view grammar;
  std::string_view id;
  // Comma-separated, as the build passes them.
  std::string_view extensions;
  std::string_view fileNames;
};

// Runs a generator's command line, `[--textmate=<file>] <directory>`: writes
// the language's sources there and answers the process's exit status. A
// grammar the engine does not read is said on the error stream.
int generate(GrammarImage& image, const LowerDesc& desc, const LanguageNames& names,
             int argc, char** argv);

}  // namespace cursiv::engine::gen
