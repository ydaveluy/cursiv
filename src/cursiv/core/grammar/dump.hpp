#pragma once

#include <string>
#include <type_traits>

#include "cursiv/core/grammar/GrammarDump.hpp"
#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/core/grammar/ImageOf.hpp"

// Renders a grammar as reviewable text. `dump_declared<G>()` prints the
// grammar as written, `dump_compiled<G>()` its resolved form, so a rewrite is
// reviewable side by side rather than merely asserted.

namespace cursiv {

template <class E>
std::string dump_expr() {
  GrammarImage image;
  GrammarPrinter printer(image, ImageView::written);
  return printer.spell(image.node(detail::kNode<void, std::remove_cvref_t<E>>));
}

template <class G>
std::string dump_declared() {
  return GrammarPrinter(grammar_image<G>(), ImageView::written).render().text;
}

// One rule, as `Name: body;`.
template <class R>
std::string dump_rule() {
  GrammarImage image;
  GrammarPrinter printer(image, ImageView::written);
  return printer.rule_line(image.rule(detail::kRule<void, declared_of_t<R>>));
}

// A rule and everything it reaches, one per line, in order of first
// appearance.
template <class R>
std::string dump_closure() {
  GrammarImage image;
  GrammarPrinter printer(image, ImageView::written);
  return printer.closure(image.rule(detail::kRule<void, declared_of_t<R>>));
}

template <class C>
std::string dump_compiled() {
  return GrammarPrinter(grammar_image<C>(), ImageView::resolved).render().text;
}

template <class C>
GrammarDump dump_compiled_indexed() {
  return GrammarPrinter(grammar_image<C>(), ImageView::resolved).render();
}

}  // namespace cursiv
