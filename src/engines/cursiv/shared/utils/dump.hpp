#pragma once

#include <string>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/grammar/ImageOf.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarDump.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"

// Renders a grammar as reviewable text: one rule as written, or the whole
// grammar as the pipeline resolved it. Each call reads an image of its own, so
// dumps may run on several threads at once.

namespace cursiv::engine {

// One rule, as `Name: body;`.
template <class R>
std::string dump_rule() {
  GrammarImage image;
  GrammarPrinter printer(image, ImageView::written);
  return printer.rule_line(image.rule(kRule<void, declared_of_t<R>>));
}

template <class C>
GrammarDump dump_compiled_indexed() {
  GrammarImage image = grammar_image<C>();
  return GrammarPrinter(image, ImageView::resolved).render();
}

}  // namespace cursiv::engine
