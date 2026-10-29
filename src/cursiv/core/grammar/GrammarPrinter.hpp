#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/GrammarDump.hpp"
#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/core/text/ClassItem.hpp"
#include "cursiv/core/utils/ctstring.hpp"

// Renders a grammar image as reviewable text: `{T}` marks where a node is
// allocated, `'kw'` a keyword, `"lit"` a raw literal. The same image reads
// as written or as the pipeline resolved it, so a rewrite is reviewable side
// by side rather than merely asserted.

namespace cursiv {

// Enough precedence to decide brackets: `(name=X)*` and `name=X*` print apart.
enum class Prec { Choice, Sequence, Prefix, Item };

enum class ImageView : std::uint8_t { written = 0, resolved = 1 };

namespace detail {

std::string decimal(std::size_t v);
void quote(std::string& out, std::string_view text, char q = '\'');
void charset(std::string& out, const CharSet& set);

}  // namespace detail

// Rules are named in order of first appearance: the second of two rules called
// `Dup` in two namespaces gets its qualified name.
class GrammarPrinter {
 public:
  GrammarPrinter(GrammarImage& image, ImageView view)
      : _image(image), _view(view) {}

  GrammarDump render();
  // Keeps the names `render` gave, for a reader that spells more of the text.
  void prime();

  std::string spell(ImageId node);
  std::string name_for(std::uint32_t rule);
  std::string rule_line(std::uint32_t rule);
  std::string closure(std::uint32_t rule);

 private:
  struct Entry {
    std::string name;
    std::uint32_t rule = 0;
    std::vector<std::size_t> dependencies;
  };

  std::size_t index_for(std::uint32_t rule);
  std::string reference_name(std::uint32_t rule);
  void note_edge(std::size_t target);
  void name_list(std::string& out, std::uint32_t at, std::uint32_t count);
  void line(std::string& out, std::uint32_t rule);

  Prec prec(ImageId node) const;
  void emit(std::string& out, ImageId node);
  void item(std::string& out, ImageId node, Prec here);
  void emit_lit(std::string& out, const ImageNode& n);
  void emit_class(std::string& out, const ImageNode& n) const;
  void emit_choice(std::string& out, const ImageNode& n);
  void emit_repeat(std::string& out, const ImageNode& n);
  void emit_write(std::string& out, const ImageNode& n, std::string_view op);
  bool implied_pair(const ImageNode& n) const;
  bool printed_by_next(const ImageNode& choice, std::uint32_t arm) const;

  GrammarImage& _image;
  ImageView _view;
  std::vector<Entry> _entries;
  std::size_t _current = static_cast<std::size_t>(-1);
  int _synth = 0;
};

}  // namespace cursiv
