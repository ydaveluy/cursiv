#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarDump.hpp"
#include "cursiv/text/ClassItem.hpp"
#include "cursiv/utils/ctstring.hpp"

// Renders a grammar image as reviewable text, as written or as resolved:
// `{T}` marks where a node is allocated, `'kw'` a keyword, `"lit"` a literal.

namespace cursiv::engine {

enum class ImageView : std::uint8_t { written = 0, resolved = 1 };

// Enough precedence to decide brackets: `(name=X)*` and `name=X*` print apart.
enum class Prec { Choice, Sequence, Prefix, Item };

std::string decimal(std::size_t v);
void charset(std::string& out, const CharSet& set);

// Rules are named in order of first appearance: the second of two rules called
// `Dup` in two namespaces gets its qualified name.
class GrammarPrinter {
 public:
  GrammarPrinter(GrammarImage& image, ImageView view)
      : _image(image), _view(view) {}

  GrammarDump render();
  // Renders once, so later calls use the names `render` gives.
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
  Descent<void> emit(std::string& out, ImageId node);
  Descent<void> item(std::string& out, ImageId node, Prec here);
  Descent<void> emit_lit(std::string& out, const ImageNode& n);
  void emit_class(std::string& out, const ImageNode& n) const;
  Descent<void> emit_choice(std::string& out, const ImageNode& n);
  Descent<void> emit_repeat(std::string& out, const ImageNode& n);
  Descent<void> emit_write(std::string& out, const ImageNode& n,
                           std::string_view op);
  bool implied_pair(const ImageNode& n) const;
  bool printed_by_next(const ImageNode& choice, std::uint32_t arm) const;

  GrammarImage& _image;
  ImageView _view;
  std::vector<Entry> _entries;
  std::size_t _current = static_cast<std::size_t>(-1);
  int _synth = 0;
};

}  // namespace cursiv::engine
