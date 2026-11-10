// Guards that decide where a token ends read the same on both engines,
// whatever order the grammar names its tokens in.

#include <string>
#include <string_view>
#include <vector>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/LexGuardsGrammar.hpp"
#include "lexblankheadll/generated/BlankHeadLl/Module.hpp"
#include "lexblankheadlr/generated/BlankHeadLr/Module.hpp"
#include "lexguardsll/generated/Ll/Module.hpp"
#include "lexguardslr/generated/Lr/Module.hpp"
#include "lexovertakell/generated/OvertakeLl/Module.hpp"
#include "lexovertakelr/generated/OvertakeLr/Module.hpp"

namespace {

using Items = std::vector<std::string>;

// The items a parser reads, or one mark where it refuses the text.
Items items(const cursiv::Parser& parser, std::string_view text) {
  const auto r = parser.parse(text);
  const auto* doc = r.value_as<tlexguards::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return {"refused"};
  return {doc->items.begin(), doc->items.end()};
}

// A keyword's guard keeps the complement of its word boundary: it is not the
// literal that refuses the class the boundary leaves out.
void a_keyword_keeps_a_complemented_boundary(const cursiv::Parser& p) {
  CHECK(items(p, "go;") == Items{"go;"});
  CHECK(items(p, "go ;") == Items{"go;"});
  CHECK(items(p, "go!") == Items{"go!"});
  CHECK(items(p, "go; go!") == (Items{"go;", "go!"}));
  CHECK(items(p, "go!go;") == (Items{"go!", "go;"}));
}

// A letter lets `if` pass, and a match ending on it stands where the longer one
// is refused by the scalar after it.
void a_passable_refusal_keeps_the_shorter_match(const cursiv::Parser& p) {
  CHECK(items(p, "xifaλ") == (Items{"xif", "aλ"}));
  CHECK(items(p, "xifa") == Items{"xifa"});
  CHECK(items(p, "xabcλ") == (Items{"xab", "cλ"}));
  CHECK(items(p, "xif") == Items{"refused"});
  CHECK(items(p, "xif aλ") == Items{"refused"});
}

// A choice of more words than a recognizer tests one by one is read as data.
// The word a byte opens may still not be there: `λ` opens as `α` does.
void an_assertion_reads_a_choice_of_many_words(const cursiv::Parser& p) {
  CHECK(items(p, "uαz") == Items{"uαz"});
  CHECK(items(p, "uсz") == Items{"uсz"});
  CHECK(items(p, "uz") == Items{"refused"});
  CHECK(items(p, "uλz") == Items{"refused"});
  CHECK(items(p, "u") == Items{"refused"});
  CHECK(items(p, "u\xCE") == Items{"refused"});
}

// A run of more refusals than a recognizer tests one by one is read as data:
// each word refused, short of the letter that lets it pass.
void an_assertion_reads_a_run_of_many_refusals(const cursiv::Parser& p) {
  CHECK(items(p, "vqz") == Items{"vqz"});
  CHECK(items(p, "vqλz") == Items{"vqλz"});
  CHECK(items(p, "vq") == Items{"vq"});
  CHECK(items(p, "vqαz") == Items{"refused"});
  CHECK(items(p, "vqс") == Items{"refused"});
  CHECK(items(p, "vz") == Items{"refused"});
  CHECK(items(p, "wqz") == Items{"wqz"});
  CHECK(items(p, "wqa") == Items{"wqa"});
  CHECK(items(p, "wqa0z") == Items{"wqa0z"});
  CHECK(items(p, "wqd2z") == Items{"wqd2z"});
  CHECK(items(p, "wqa0") == Items{"refused"});
  CHECK(items(p, "wqa0!") == Items{"refused"});
  CHECK(items(p, "wqd2") == Items{"refused"});
}

// The links a parser reads, `from>to` where a link goes on, or one mark
// where it refuses the text.
Items links(const cursiv::Parser& parser, std::string_view text) {
  const auto r = parser.parse(text);
  const auto* doc = r.value_as<tlexguards::Links>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return {"refused"};
  Items out;
  for (const tlexguards::Link* link : doc->links)
    out.push_back(std::string(link->from) +
                  (link->to.empty() ? "" : ">" + std::string(link->to)));
  return out;
}

// Only the tokens a viable parse expects are tried: where a link goes on with
// `-` or `=`, `->` and `=>` are no token though their assertions hold, and
// where they are expected, they are read.
void an_assertion_decides_no_unexpected_token(const cursiv::Parser& p) {
  CHECK(links(p, "pair a->b;") == Items{"a>b"});
  CHECK(links(p, "pair a=>b;") == Items{"a>b"});
  CHECK(links(p, "arrow a->b") == (Items{"a", "b"}));
  CHECK(links(p, "fat a=>b") == (Items{"a", "b"}));
  CHECK(links(p, "arrow a->b->c") == (Items{"a", "b>c"}));
}

// A token behind an assertion takes the blanks it starts with: no ignored
// run is crossed ahead of the walk that launches the assertion.
void an_asserted_token_keeps_its_leading_blanks(const cursiv::Parser& p) {
  CHECK(items(p, "  !") == Items{"  !"});
  CHECK(items(p, "a  !") == (Items{"a", "  !"}));
  CHECK(items(p, "a b !") == (Items{"a", "b", " !"}));
  CHECK(items(p, "a  b") == (Items{"a", "b"}));
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  auto ll = tlexguards::generated::Ll::create_parser();
  auto lr = tlexguards::generated::Lr::create_parser();
  for (const cursiv::Parser* parser : {ll.get(), lr.get()}) {
    a_keyword_keeps_a_complemented_boundary(*parser);
    a_passable_refusal_keeps_the_shorter_match(*parser);
    an_assertion_reads_a_choice_of_many_words(*parser);
    an_assertion_reads_a_run_of_many_refusals(*parser);
  }
  auto overtakeLl = tlexguards::generated::OvertakeLl::create_parser();
  auto overtakeLr = tlexguards::generated::OvertakeLr::create_parser();
  for (const cursiv::Parser* parser : {overtakeLl.get(), overtakeLr.get()})
    an_assertion_decides_no_unexpected_token(*parser);
  auto blankLl = tlexguards::generated::BlankHeadLl::create_parser();
  auto blankLr = tlexguards::generated::BlankHeadLr::create_parser();
  for (const cursiv::Parser* parser : {blankLl.get(), blankLr.get()})
    an_asserted_token_keeps_its_leading_blanks(*parser);
  return cursiv::test::report("LexGuards_test");
}
