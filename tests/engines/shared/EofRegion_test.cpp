#include "engines/shared/EofRegionAst.hpp"
#include "engines/shared/Checks.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "eofpurell/generated/PureLl/Module.hpp"
#include "eofpurelr/generated/PureLr/Module.hpp"
#include "eofregionaux/generated/Aux/Module.hpp"
#include "eofregionll/generated/Ll/Module.hpp"
#include "eofregionlr/generated/Lr/Module.hpp"
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace eof_region_cases {

void run() {
  const auto ll = teofregion::generated::Ll::create_parser();
  const auto lr = teofregion::generated::Lr::create_parser();
  const auto aux = teofregion::generated::Aux::create_parser();
  for (const auto* parser : {ll.get(), lr.get(), aux.get()}) {
    for (const std::string_view source :
         {"a <? x ?> b", "a <? x   ", "a <?   "}) {
      const std::string input = "island:" + std::string(source);
      const auto result = parser->parse(input);
      CHECK(result.accepted && !result.cst().recovered() &&
            result.diagnostics.empty());
      const auto* document = result.value_as<teofregion::Document>();
      const auto count = source.ends_with(" b") ? 3u : 2u;
      CHECK(document && document->parts.size() == count);
      if (!document || document->parts.size() != count) continue;
      const auto* text = std::get_if<std::string_view>(&document->parts[0]);
      const auto* island =
          std::get_if<teofregion::Island*>(&document->parts[1]);
      CHECK(text && *text == "a ");
      CHECK(island && *island && (*island)->parent == document);
      if (island && *island) {
        CHECK((*island)->words.size() ==
              (source.find('x') == std::string_view::npos ? 0u : 1u));
        if (!(*island)->words.empty()) CHECK((*island)->words[0] == "x");
      }
      if (count == 3)
        CHECK(std::get<std::string_view>(document->parts[2]) == " b");
    }
    const auto rejected = parser->parse("island:<? x bad");
    CHECK(!rejected.diagnostics.empty() || rejected.cst().recovered() ||
          !rejected.accepted);
  }
  for (const auto* parser : {ll.get(), lr.get(), aux.get()}) {
    for (const std::string_view source :
         {"inner", "inner   ", "ref-inner   ", "node   ", "directx", "callx"}) {
      const std::string input = "chain:" + std::string(source);
      const auto result = parser->parse(input);
      CHECK(result.accepted && !result.cst().recovered() &&
            result.diagnostics.empty());
      const auto* doc = result.value_as<teofregion::ChainDocument>();
      CHECK(doc);
      if (doc && source.starts_with("node"))
        CHECK(doc->empty && doc->empty->parent == doc);
    }
    for (const std::string_view source :
         {"outer   ", "ref-outer   ", "directx   ", "callx   "}) {
      const std::string input = "chain:" + std::string(source);
      const auto result = parser->parse(input);
      CHECK(!result.accepted || result.cst().recovered() ||
            !result.diagnostics.empty());
    }
  }
  const auto pureLl = teofregion::generated::PureLl::create_parser();
  const auto pureLr = teofregion::generated::PureLr::create_parser();
  for (const auto* parser : {pureLl.get(), pureLr.get()}) {
    for (const std::string_view source : {"", " ", "   "}) {
      const auto result = parser->parse(source);
      CHECK(result.accepted && !result.cst().recovered() &&
            result.diagnostics.empty());
      CHECK(result.value_as<teofregion::ChainDocument>());
    }
    const auto invalid = parser->parse(" x");
    CHECK(!invalid.accepted || invalid.cst().recovered() ||
          !invalid.diagnostics.empty());
  }
}

}  // namespace eof_region_cases

namespace nullable_region_cases {

void run() {
  const auto ll = teofregion::generated::Ll::create_parser();
  const auto lr = teofregion::generated::Lr::create_parser();
  const auto aux = teofregion::generated::Aux::create_parser();
  for (const cursiv::Parser* parser : {ll.get(), lr.get(), aux.get()}) {
    for (std::string_view source :
         {"", "  # empty\n", "1", "\"#{}\"", "\"#{ 1 }\"",
          "\"#{# comment\n 1}\"", "\"#{\"#{2}\"}\""}) {
      const std::string input = "nullable:" + std::string(source) + "@END@";
      const auto r = parser->parse(input);
      CHECK(r.accepted && !r.cst().recovered() && r.diagnostics.empty());
      const auto* root = r.value_as<tnullableregion::Block>();
      CHECK(root);
      if (!root) continue;
      if (source.empty() || source.starts_with("  #")) {
        CHECK(!root->value);
        continue;
      }
      CHECK(root->value && root->value->parent == root);
      if (source == "1") {
        const auto* n = cursiv::ast_cast<tnullableregion::Number>(root->value);
        CHECK(n && n->text == "1");
        continue;
      }
      const auto* s = cursiv::ast_cast<tnullableregion::String>(root->value);
      CHECK(s && s->hole && s->hole->parent == s);
      if (!s || !s->hole) continue;
      if (source == "\"#{}\"") {
        CHECK(!s->hole->value);
        continue;
      }
      const auto* n = cursiv::ast_cast<tnullableregion::Number>(s->hole->value);
      if (source.starts_with("\"#{\"")) {
        const auto* nested =
            cursiv::ast_cast<tnullableregion::String>(s->hole->value);
        CHECK(nested && nested->parent == s->hole && nested->hole &&
              nested->hole->parent == nested);
        n = nested && nested->hole
                ? cursiv::ast_cast<tnullableregion::Number>(nested->hole->value)
                : nullptr;
        CHECK(n && n->text == "2" && n->parent == nested->hole);
      } else
        CHECK(n && n->text == "1" && n->parent == s->hole);
    }
    for (std::string_view source :
         {"x", "\"#{1 2}\"", "\"#{1} x\"", "\"#{1} \"", "\"#{1}"}) {
      const std::string input = "nullable:" + std::string(source) + "@END@";
      const auto r = parser->parse(input);
      CHECK(!r.accepted || r.cst().recovered() || !r.diagnostics.empty());
    }
  }
}

}  // namespace nullable_region_cases

namespace regional_read_mode_cases {

void run() {
  const auto ll = teofregion::generated::Ll::create_parser();
  const auto lr = teofregion::generated::Lr::create_parser();
  struct Case {
    std::string_view source, prefix, suffix, value;
    std::vector<std::string_view> args = {};
  };
  // A `?` or `(` operand ends before `?>`, which implies the `;`.
  const Case cases[] = {
      {" \n// html<?php echo 1;?> \t ", " \n// html", " \t ", "1"},
      {"<?= 1 ?>", "", "", "1"},
      {"<?= 1 ?> \t ", "", " \t ", "1"},
      {"<?php echo 1;?>tail", "", "tail", "1"},
      {"<?php echo 1;?>\nhtml", "", "html", "1"},
      {"<?php echo 1; \t ", "", "", "1"},
      {"<?php echo 1;?>", "", "", "1"},
      {"<?php echo a ? b : c ?> \t ", "", " \t ", "a", {"b", "c"}},
      {"<?php echo a(b) ?> \t ", "", " \t ", "a", {"b"}},
  };
  for (const auto* parser : {ll.get(), lr.get()}) {
    for (const auto& test : cases) {
      const std::string input = "regional:" + std::string(test.source);
      const auto result = parser->parse(input);
      CHECK(result.accepted);
      CHECK(!result.cst().recovered());
      CHECK(result.diagnostics.empty());
      const auto* document = result.value_as<tregionalreadmode::Doc>();
      CHECK(document);
      const std::size_t prefix = !test.prefix.empty();
      const std::size_t count = 1u + prefix + !test.suffix.empty();
      CHECK(document && document->items.size() == count);
      if (!document || document->items.size() != count) continue;
      const auto check_text = [&](std::size_t index,
                                  std::string_view expected) {
        const auto* text =
            cursiv::ast_cast<tregionalreadmode::Text>(document->items[index]);
        CHECK(text && text->name == expected && text->parent == document);
      };
      if (prefix) check_text(0, test.prefix);
      if (!test.suffix.empty()) check_text(count - 1u, test.suffix);
      const auto* block =
          cursiv::ast_cast<tregionalreadmode::Block>(document->items[prefix]);
      CHECK(block && block->parent == document && block->args.size() == 1);
      if (!block || block->args.size() != 1) continue;
      const auto* echo =
          cursiv::ast_cast<tregionalreadmode::Echo>(block->args[0]);
      CHECK(echo && echo->parent == block && echo->args.size() == 1);
      if (!echo || echo->args.size() != 1) continue;
      const auto* value = echo->args[0];
      CHECK(value && value->parent == echo && value->name == test.value);
      CHECK(value && value->args.size() == test.args.size());
      if (!value || value->args.size() != test.args.size()) continue;
      for (std::size_t i = 0; const std::string_view arg : test.args) {
        const auto* operand = value->args[i++];
        CHECK(operand && operand->parent == value && operand->name == arg &&
              operand->args.empty());
      }
    }
  }
}

}  // namespace regional_read_mode_cases

namespace byte_condition_cases {

// `eof` is the end of the text in both engines, and `!byte` is a condition
// like any: it asks the token in hand.

namespace {

const char* engine = "";

std::string read(const cursiv::Parser& parser, std::string_view text,
                 bool inside = false) {
  const std::string source = "bytecond:" + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* doc = r.value_as<tbytecond::ast::Doc>();
  if (inside) {
    const auto* section = r.value_as<tbytecond::ast::Section>();
    doc = section != nullptr ? section->doc : nullptr;
  }
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return "<refused>";
  std::string out;
  for (const tbytecond::ast::Item* const item : doc->items) {
    if (!out.empty()) out += '|';
    out += item->text;
  }
  return out;
}

void check(const cursiv::Parser& parser, std::string_view text,
           std::string_view expected, bool inside) {
  const std::string got = read(parser, text, inside);
  same_text(engine, text, got, expected);
}

void exercise(const cursiv::Parser& parser) {
  const auto r = [&](std::string_view text, std::string_view expected,
                     bool inside = false) {
    check(parser, text, expected, inside);
  };
  // `!byte` before a token longer than one byte; a token of one byte is the
  // one `byte` reads.
  r("byte:ab", "ab");
  r("byte:a", "<refused>");
  r("byte:\xC3\xA9", "\xC3\xA9");
  r("byte:\xE9", "<refused>");
  r("byte:", "<refused>");
  // ...as `!any` before a token longer than one character.
  r("any:ab", "ab");
  r("any:a", "<refused>");
  r("any:\xC3\xA9", "<refused>");
  r("any:\xC3\xA9\xC3\xA9", "\xC3\xA9\xC3\xA9");
  // `eof` is the end of the text.
  r("end:ab", "ab");
  r("end:", "<refused>");
  r("more:ab", "ab");
  r("more:", "<refused>");
  r("stop:", "");
  r("stop:a", "<refused>");
  // In a token, `!byte` is the end of the text too.
  r("tbyte:x", "x");
  r("tbyte:xy", "<refused>");
  r("teof:x", "x");
  r("teof:xy", "<refused>");
  // ...and in a section, where the section ends.
  r("<ab>", "ab", true);
  r("<a>", "a", true);
  r("<>", "<refused>", true);
  r("<ab", "<refused>", true);
  r("<ab>>", "<refused>", true);
}

}  // namespace

void run() {
  engine = "LL";
  exercise(*teofregion::generated::Ll::create_parser());
  engine = "LR";
  exercise(*teofregion::generated::Lr::create_parser());
}

}  // namespace byte_condition_cases

int main() {
  cursiv::test::count_expectations();
  eof_region_cases::run();
  nullable_region_cases::run();
  regional_read_mode_cases::run();
  byte_condition_cases::run();
  return cursiv::test::report("EofRegion_test");
}
