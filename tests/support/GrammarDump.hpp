// A grammar as reviewable text, as the engine reads it: `dump_declared<G>()`
// and `dump_compiled<G>()` print the whole grammar, `dump_closure<G, R>()` a
// rule and what it reaches, `dump_expr<E>()` one expression, `dump_indexed<G>()`
// the grammar with its rule dependencies. Safe on several threads at once.
#pragma once

#include <string>
#include <type_traits>

#include "cursiv/engine/generator/GrammarPrinter.hpp"
#include "cursiv/engine/grammar/ImageOf.hpp"
#include "cursiv/engine/grammar/View.hpp"

namespace support {

// One expression, which names no grammar: a keyword is guarded by the default
// word class.
template <class E>
std::string dump_expr() {
  using namespace cursiv::engine::gen;
  GrammarImage image;
  image.bound = image.plain = class_of_bound<std::remove_cvref_t<
      decltype(cursiv::GrammarSpec::w)>>::value;
  return GrammarPrinter(image).spell(
      image.node(kNode<void, std::remove_cvref_t<E>>));
}

// The whole grammar with the rules each rule uses, by index.
template <class G>
cursiv::engine::gen::GrammarDump dump_indexed() {
  using namespace cursiv::engine::gen;
  GrammarImage image = grammar_image<View<G>>();
  return GrammarPrinter(image).render();
}

template <class G>
std::string dump_declared() {
  return dump_indexed<G>().text;
}

// Rule `R` of grammar `G` and everything it reaches, one per line, in order of
// first appearance.
template <class G, class R>
std::string dump_closure() {
  using namespace cursiv::engine::gen;
  GrammarImage image = grammar_image<View<G>>();
  return GrammarPrinter(image).closure(
      image.rule(kRule<View<G>, cursiv::detail::declared_of_t<R>>));
}

// The engine reads one form of a grammar: its overrides are resolved.
template <class G>
std::string dump_compiled() {
  return dump_declared<G>();
}

}  // namespace support
