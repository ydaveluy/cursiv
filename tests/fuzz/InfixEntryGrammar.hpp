#pragma once

// `infix<>` as the entry: the model gives a climb no children of its own
// (`GrammarCost`'s own note), and a walk that reached the entry through the
// children of a rule wrote nothing at all.

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace finfix {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Node> {
  string_view text;
};
struct Bin : cursiv::Extends<Node> {
  Node* left = nullptr;
  string_view op;
  Node* right = nullptr;
};
// Reads what it views as it goes, so whatever builds a tree keeps the text
// longer than its nodes.
struct Text : cursiv::Extends<Node> {
  inline static std::size_t read = 0;
  string_view value;
  ~Text() override {
    for (const char c : value) read += static_cast<unsigned char>(c);
  }
};
struct Words : cursiv::Extends<Node> {
  vector<string> items;
};
// With no empty state, and never assigned.
struct Ident {
  std::string text;
  explicit Ident(std::string value) : text(std::move(value)) {}
};
struct Seal {
  const std::string text;
  explicit Seal(std::string value) : text(std::move(value)) {}
};
struct Held : cursiv::Extends<Node> {
  Ident only{"none"};
  optional<Ident> first;
  vector<Seal> seals;
  variant<int64, string> answer;
  int64 wide = -1;
};
}  // namespace ast

inline std::string unquote(std::string_view text) {
  return std::string(text.substr(1, text.size() - 2));
}
// Refuses about half of what it reads: the member then keeps its default.
inline cursiv::ConversionResult<std::string> checked(std::string_view text) {
  using Result = cursiv::ConversionResult<std::string>;
  if (text.size() % 2 == 0) return Result::err("an even length");
  return Result::ok(std::string(text));
}
// Reads its parts, one for a token; and reads nothing at all.
inline std::string from_parts(std::span<const std::string_view> parts) {
  return "parts:" + std::string(parts.front());
}
inline std::string_view from_nothing() { return "nothing"; }
// A whole list it refuses for about half of what it reads.
inline cursiv::ConversionResult<std::vector<std::string>> checked_list(
    std::string_view text) {
  using Result = cursiv::ConversionResult<std::vector<std::string>>;
  if (text.size() % 2 == 0) return Result::err("an even length");
  return Result::ok(std::vector<std::string>{std::string(text)});
}
// A value the engine does not join: only a view or a string is text there.
inline const char* constant(std::string_view) { return "converted!"; }
inline std::vector<std::string> split(std::string_view text) {
  std::vector<std::string> words;
  for (std::size_t at = 0; at < text.size();) {
    const std::size_t end = std::min(text.find(',', at), text.size());
    words.emplace_back(text.substr(at, end - at));
    at = end + 1;
  }
  return words;
}

inline ast::Ident ident(std::string_view text) {
  return ast::Ident(std::string(text.substr(1)));
}
inline ast::Seal seal(std::string_view text) {
  return ast::Seal(std::string(text.substr(1)));
}
inline std::variant<std::int64_t, std::string> answer(std::string_view text) {
  if (text.size() % 2 == 0) return std::int64_t{1};
  return std::string(text.substr(1));
}

struct InfixEntryG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  // A converter that builds its text, alone and in the parts of a data rule.
  struct QUOTED
      : terminal<as<&unquote>(lit<"\""> + many(cr<"a-z">) + lit<"\"">)> {};
  struct Joined : rule<ref<QUOTED> + ref<QUOTED>> {};
  // A converter that answers a whole list.
  struct WORDS : terminal<as<&split>(lit<"#"> + some(cr<"a-z,">))> {};
  struct CHECKED : terminal<as<&checked>(lit<"'"> + some(cr<"a-z">))> {};
  struct CheckedJoined : rule<ref<CHECKED> + ref<CHECKED>> {};
  struct BANG : terminal<as<&constant>(lit<"!"> + some(cr<"a-z">))> {};
  struct Banged : rule<ref<BANG> + ref<BANG>> {};
  struct PARTED : terminal<as<&from_parts>(lit<"%"> + some(cr<"a-z">))> {};
  struct NOTHING : terminal<as<&from_nothing>(lit<"^">)> {};
  struct PartedJoined : rule<ref<PARTED> + ref<NOTHING>> {};
  struct CHECKED_WORDS
      : terminal<as<&checked_list>(lit<"~"> + some(cr<"a-z">))> {};
  struct IDENT : terminal<as<&ident>(lit<"$"> + some(cr<"a-z">))> {};
  struct SEAL : terminal<as<&seal>(lit<"="> + some(cr<"a-z">))> {};
  struct ANSWER : terminal<as<&answer>(lit<"?"> + some(cr<"a-z">))> {};
  struct SMALL : terminal<as<std::int8_t>(some(d))> {};
  struct Sum;
  struct Primary
      : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>)) |
             create<ast::Held>(
                 kw<"held"> + assign<&ast::Held::only>(ref<IDENT>) +
                 option(assign<&ast::Held::first>(ref<IDENT>)) +
                 many(append<&ast::Held::seals>(ref<SEAL>)) +
                 option(assign<&ast::Held::answer>(ref<ANSWER>)) +
                 option(kw<"wide"> + assign<&ast::Held::wide>(ref<SMALL>))) |
             create<ast::Text>(assign<&ast::Text::value>(ref<Joined>)) |
             create<ast::Text>(assign<&ast::Text::value>(ref<PartedJoined>)) |
             create<ast::Words>(
                 assign<&ast::Words::items>(ref<CHECKED_WORDS>)) |
             create<ast::Text>(assign<&ast::Text::value>(ref<CheckedJoined>)) |
             create<ast::Text>(assign<&ast::Text::value>(ref<Banged>)) |
             create<ast::Words>(assign<&ast::Words::items>(ref<WORDS>)) |
             (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Sum : infix<Primary, ast::Bin,
                     (left(lit<"+"> | lit<"-"> | (kw<"NOT"> + kw<"EQUAL">)))> {
  };
  using entry = Sum;
  using ignored = WS;
};

// An operator of several keywords is a data rule: the member holds its parts
// joined, never the blank between them.
struct InfixSequenceG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct Sum;
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>)) |
                        (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Sum
      : infix<Primary, ast::Bin, (left(lit<"+"> | (kw<"NOT"> + kw<"EQUAL">)))> {
  };
  using entry = Sum;
  using ignored = WS;
};

}  // namespace finfix
