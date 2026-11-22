#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engine/grammar/GrammarImage.hpp"
#include "cursiv/text/unicode/UnicodeRange.hpp"
#include "cursiv/utils/Descent.hpp"

// Renders a grammar image as reviewable text: `{T}` marks where a node is
// allocated, `'kw'` a literal, `"kw"` a keyword that ends at a word boundary.

namespace cursiv::engine::gen {

// Writes the members of a bracketed character class, escaping its metacharacters.
void append_class_ranges(std::string& out, std::span<const unicode::Range> ranges,
                         std::string_view escaped);

// The printed grammar: each rule's span of `text` and the rules it uses, by
// index.
struct GrammarDump {
  struct Rule {
    std::uint32_t imageRule;
    std::string name;
    std::size_t begin = 0;
    std::size_t size = 0;
    std::vector<std::size_t> dependencies;
  };
  std::string text;
  std::vector<Rule> rules;
};

// The dump folded for review: long rules broken at their top-level
// alternatives and filled to a column.
[[nodiscard]] std::string dump_wrapped(std::string_view text,
                                       std::size_t width = 92);

// Rules are named in order of first appearance, never two alike: the second
// `Dup` gets its qualified name, or that name spelled from the global scope.
class GrammarPrinter {
 public:
  explicit GrammarPrinter(GrammarImage& image) : _image(image) {}

  GrammarDump render() const { return GrammarPrinter(_image).dump(kNoImage); }
  std::string spell(ImageId node);
  // A rule and every rule it reaches, one per line.
  std::string closure(std::uint32_t rule) const { return GrammarPrinter(_image).dump(rule).text; }

 private:
  // Enough precedence to decide brackets: `(name=X)*` and `name=X*` print
  // apart.
  enum class Prec { Choice, Sequence, Prefix, Item };

  GrammarDump dump(std::uint32_t root);
  std::size_t index_for(std::uint32_t rule);
  const std::string& reference_name(std::uint32_t rule);
  void name_list(std::string& out, ImageRules rules);
  void trivia(std::string& out, const ImageTrivia& scope);
  void line(std::string& out, std::uint32_t rule);
  void grammar_line(std::string& out, std::uint32_t rule);
  void infix_line(std::string& out, std::uint32_t rule);

  Prec prec(ImageId node) const;
  detail::Descent<void> emit(std::string& out, ImageId node);
  detail::Descent<void> item(std::string& out, ImageId node, Prec here);
  detail::Descent<void> emit_lit(std::string& out, const ImageNode& n);
  detail::Descent<void> emit_choice(std::string& out, const ImageNode& n);
  detail::Descent<void> emit_repeat(std::string& out, const ImageNode& n);
  detail::Descent<void> emit_write(std::string& out, const ImageNode& n,
                                   std::string_view op);
  bool printed_by_next(const ImageNode& choice, std::uint32_t arm) const;

  GrammarImage& _image;
  std::vector<GrammarDump::Rule> _entries;
  struct Visit {
    std::uint32_t entry = kNoImage;
    std::uint32_t caller = kNoImage;
  };
  std::vector<Visit> _visits;
  std::unordered_map<std::string, std::size_t> _names;
  std::uint32_t _current = kNoImage;
  std::size_t _synth = 0;
};

}  // namespace cursiv::engine::gen
