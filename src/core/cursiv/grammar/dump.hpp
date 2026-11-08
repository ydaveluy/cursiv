#pragma once

#include <string>

#include "cursiv/grammar/GrammarDump.hpp"
#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/grammar/GrammarPrinter.hpp"
#include "cursiv/grammar/ImageOf.hpp"

// Renders a grammar as reviewable text: one rule as written, or the whole
// grammar as the pipeline resolved it. Each call reads an image of its own, so
// dumps may run on several threads at once.

namespace cursiv {

// One rule, as `Name: body;`.
template <class R>
std::string dump_rule() {
  detail::GrammarImage image;
  GrammarPrinter printer(image, ImageView::written);
  return printer.rule_line(
      image.rule(detail::kRule<void, detail::declared_of_t<R>>));
}

template <class C>
GrammarDump dump_compiled_indexed() {
  detail::GrammarImage image = detail::grammar_image<C>();
  return GrammarPrinter(image, ImageView::resolved).render();
}

}  // namespace cursiv
