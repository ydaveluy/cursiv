// Generated text, written as it goes into one buffer.
#pragma once

#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cursiv {

namespace detail {

// What `joined` gives.
template <class Items, class Cell>
struct Joined {
  const Items& items;
  std::string_view separator;
  Cell cell;
};

}  // namespace detail

// What it writes are parts: text, a number, a character, any type `std::format`
// knows, or a list from `joined`. A part may hold line breaks: each line starts
// at the current indentation and keeps no blanks at its end.
class TextBuilder {
 public:
  struct Options {
    // What `open` and `indented` add.
    std::size_t step = 4;
    // The indentation stops growing past this width.
    std::size_t maxIndent = std::numeric_limits<std::size_t>::max();
  };

  TextBuilder() = default;
  explicit TextBuilder(Options options) : _options(options) {}

  // Lines written while this exists start further to the right.
  class Indented {
   public:
    Indented(const Indented&) = delete;
    Indented& operator=(const Indented&) = delete;
    ~Indented() { _builder._indent -= _by; }

   private:
    friend class TextBuilder;
    Indented(TextBuilder& builder, std::size_t by)
        : _builder(builder), _by(by) {
      _builder._indent += by;
    }
    TextBuilder& _builder;
    std::size_t _by;
  };

  // Writes `parts` on the current line, which goes on.
  template <class... Parts>
  TextBuilder& write(const Parts&... parts) {
    (add(parts), ...);
    return *this;
  }

  // Writes `parts` and ends the line.
  template <class... Parts>
  TextBuilder& line(const Parts&... parts) {
    (add(parts), ...);
    end_line();
    return *this;
  }

  // A line, if any, then what follows it further to the right, until `close`.
  template <class... Parts>
  TextBuilder& open(const Parts&... parts) {
    if constexpr (sizeof...(Parts) != 0) line(parts...);
    _indent += _options.step;
    return *this;
  }

  // Back to the left, then a line, if any.
  template <class... Parts>
  TextBuilder& close(const Parts&... parts) {
    if (_indent < _options.step)
      throw std::logic_error("TextBuilder: a close with no open");
    _indent -= _options.step;
    if constexpr (sizeof...(Parts) != 0) line(parts...);
    return *this;
  }

  [[nodiscard]] Indented indented() { return {*this, _options.step}; }
  [[nodiscard]] Indented indented(std::size_t by) { return {*this, by}; }

  [[nodiscard]] std::size_t size() const noexcept { return _text.size(); }
  // The text written since `at`, taken out.
  [[nodiscard]] std::string cut_from(std::size_t at);

  void reserve(std::size_t bytes) { _text.reserve(bytes); }

  [[nodiscard]] const std::string& text() const noexcept { return _text; }

  // The text, handed over: the builder starts again from nothing.
  [[nodiscard]] std::string take();

 private:
  template <class Part>
  void add(const Part& part) {
    if constexpr (std::is_convertible_v<const Part&, std::string_view>) {
      add_text(part);
    } else if constexpr (std::is_same_v<Part, char>) {
      add_text(std::string_view(&part, 1));
    } else if constexpr (std::integral<Part> && !std::is_same_v<Part, bool>) {
      if constexpr (std::is_signed_v<Part>)
        add_number(static_cast<long long>(part));
      else
        add_number(static_cast<unsigned long long>(part));
    } else {
      add_text(std::format("{}", part));
    }
  }

  template <class Items, class Cell>
  void add(const detail::Joined<Items, Cell>& list) {
    std::string_view separator;
    for (const auto& item : list.items) {
      add_text(separator);
      add(std::invoke(list.cell, item));
      separator = list.separator;
    }
  }

  // Out of line: generators are compiled without optimisation, where a string
  // operation written here would cost many times what it costs there.
  void add_text(std::string_view text);
  void add_number(long long value);
  void add_number(unsigned long long value);
  void start_line();
  void end_line();

  std::string _text;
  std::size_t _indent = 0;
  bool _started = false;
  Options _options;
};

// A list to write: `items` in turn, with `separator` between two. `cell` gives
// what to write for an item, the item itself by default. Use it where it
// stands: it keeps a reference to `items`.
template <class Items, class Cell = std::identity>
  requires std::ranges::input_range<const Items>
[[nodiscard]] detail::Joined<Items, Cell> joined(const Items& items,
                                                 std::string_view separator,
                                                 Cell cell = {}) {
  return {items, separator, std::move(cell)};
}

}  // namespace cursiv
