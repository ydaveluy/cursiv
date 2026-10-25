// The language module of one grammar, written as C++ source: settled
// natively, so no consumer evaluates a grammar to obtain it. Only the parser
// it installs names an engine.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/engines/shared/gen/LanguageIdentity.hpp"

namespace cursiv {

// The module the generator owns: what derives from the grammar, installed on
// the services a language holds. The module the author writes adds its own
// over it.
std::string emit_module_decl(std::string_view ns, const LanguageIdentity& lang);

std::string emit_module(std::string_view ns, const LanguageIdentity& lang,
                        std::string_view header, std::string_view parser);

}  // namespace cursiv
