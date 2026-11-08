// A grammar rendered as reviewable text: `dump_declared<G>()` prints the
// grammar as written, `dump_compiled<G>()` its resolved form, so a rewrite is
// compared side by side. Each call reads an image of its own, so dumps may run
// on several threads at once.
#pragma once

#include <string>
#include <type_traits>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/grammar/ImageOf.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"

namespace support {

// One expression, as written.
template <class E>
std::string dump_expr() {
  cursiv::detail::GrammarImage image;
  cursiv::engine::GrammarPrinter printer(image,
                                         cursiv::engine::ImageView::written);
  return printer.spell(
      image.node(cursiv::detail::kNode<void, std::remove_cvref_t<E>>));
}

template <class G>
std::string dump_declared() {
  cursiv::detail::GrammarImage image = cursiv::detail::grammar_image<G>();
  return cursiv::engine::GrammarPrinter(image,
                                        cursiv::engine::ImageView::written)
      .render()
      .text;
}

// A rule and everything it reaches, one per line, in order of first
// appearance.
template <class R>
std::string dump_closure() {
  cursiv::detail::GrammarImage image;
  cursiv::engine::GrammarPrinter printer(image,
                                         cursiv::engine::ImageView::written);
  return printer.closure(image.rule(
      cursiv::detail::kRule<void, cursiv::detail::declared_of_t<R>>));
}

template <class C>
std::string dump_compiled() {
  cursiv::detail::GrammarImage image = cursiv::detail::grammar_image<C>();
  return cursiv::engine::GrammarPrinter(image,
                                        cursiv::engine::ImageView::resolved)
      .render()
      .text;
}

}  // namespace support
