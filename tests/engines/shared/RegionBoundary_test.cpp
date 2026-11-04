#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/RegionBoundaryGrammar.hpp"
#include "regionboundaryll/generated/Ll/Module.hpp"
#include "regionboundarylr/generated/Lr/Module.hpp"

namespace {
using tregionboundary::Document;
struct Reading {
  const cursiv::Parser& parser;
  const char* engine;

  cursiv::ParseResult parse(std::string_view source) const {
    return parser.parse(source);
  }
};

bool clean(const cursiv::ParseResult& result) {
  return result.accepted && !result.cst().recovered() &&
         result.diagnostics.empty();
}

void accepts(const Reading& parser, std::string_view source,
             std::size_t count, std::string_view expected = "x") {
  const auto result = parser.parse(source);
  const auto* doc = result.value_as<Document>();
  if (!clean(result))
    std::fprintf(stderr, "region boundary [%s]: expected clean %.*s\n",
                 parser.engine, int(source.size()), source.data());
  CHECK(clean(result));
  CHECK(doc && !doc->parent && result.nodes.size() == 1);
  CHECK(doc && doc->items.size() == count);
  CHECK(result.cst().text() == source);
  if (doc)
    for (const auto item : doc->items) CHECK(item == expected);
}

void rejects(const Reading& parser, std::string_view source) {
  const auto result = parser.parse(source);
  if (clean(result))
    std::fprintf(stderr, "region boundary [%s]: unexpected clean %.*s\n",
                 parser.engine, int(source.size()), source.data());
  CHECK(!clean(result));
}

void required_tail_keeps_the_callers_boundary(const Reading& parser) {
  for (const std::string_view mode : {"required:", "some:", "called:"}) {
    for (const std::string_view text : {"[x].", "[x ].", "[x\n].",
                                        "[xx].", "[xx ].", "[xxx] .", " [xx ] . "}) {
      const auto count = text.find("xxx") != std::string_view::npos ? 3u
                       : text.find("xx") != std::string_view::npos ? 2u : 1u;
      accepts(parser, std::string(mode) + std::string(text), count);
    }
    // A permissive lookahead may skip a blank before reducing the run.
    // Re-entering its tight mode must re-read from the committed position.
    for (const std::string_view text : {"[].", "[ x].", "[x x].",
                                        "[xx x].", "[x\nx].", "[x ~x]."})
      rejects(parser, std::string(mode) + std::string(text));
  }
}

void the_written_closing_token_decides_trivia(const Reading& parser) {
  accepts(parser, "fixed:[xx].", 2);
  accepts(parser, "fixed: [xx ] . ", 2);
  for (const std::string_view source : {"fixed:[x].", "fixed:[xxx].",
                                        "fixed:[ x x].", "fixed:[x x]."})
    rejects(parser, source);
  accepts(parser, "inside:[].", 0);
  accepts(parser, "inside:[xxx] . ", 3);
  for (const std::string_view source : {"inside:[ ].", "inside:[x ].",
                                        "inside:[ x].", "inside:[x x]."})
    rejects(parser, source);
  accepts(parser, "same: [ ] . ", 0);
  accepts(parser, "same: [ ~ x ~ ] . ", 1);
}

void an_empty_tail_keeps_the_callers_boundary(const Reading& parser) {
  for (const std::string_view mode : {"optional:", "called-option:"}) {
    for (const std::string_view text : {"[].", "[ ].", "[\n].", "~[~]~."})
      accepts(parser, std::string(mode) + std::string(text), 0);
    for (const std::string_view text : {"[x].", "[x ].", "[x\n].", "~[x~]~."})
      accepts(parser, std::string(mode) + std::string(text), 1);
    for (const std::string_view text : {"[ x].", "[~x].", "[\nx].", "[xx]."})
      rejects(parser, std::string(mode) + std::string(text));
  }
}

void a_called_first_item_keeps_the_callers_gap(const Reading& parser) {
  for (const std::string_view source : {"called-entry: x].",
                                       "called-entry:\nx ].",
                                       "called-entry:~x~]~."})
    accepts(parser, source, 1);
  accepts(parser, "called-entry: ].", 0);
  for (const std::string_view source : {"called-entry:xx].",
                                       "called-entry:x x].",
                                       "called-entry:x~x].",
                                       "called-entry:x\nx]."})
    rejects(parser, source);
}

void a_larger_floor_keeps_the_same_boundaries(const Reading& parser) {
  for (const std::string_view source : {"two:[xx].", "two:[xx ].",
                                       "two:[xx\n].", "two:~[xx~]~."})
    accepts(parser, source, 2);
  accepts(parser, "two:[xxx ].", 3);
  for (const std::string_view source : {"two:[].", "two:[x].", "two:[ x x].",
                                       "two:[x x].", "two:[xx x]."})
    rejects(parser, source);
}

void hidden_trivia_is_kept_only_outside_the_region(const Reading& parser) {
  const std::string_view source = "required:~[xx~]~.";
  const auto result = parser.parse(source);
  CHECK(clean(result));
  const auto* doc = result.value_as<Document>();
  CHECK(doc && doc->items.size() == 2 && result.nodes.size() == 1);
  CHECK(result.cst().text() == source);
  unsigned notes = 0;
  for (cursiv::NodeId id = 0; id < result.cst().size(); ++id) {
    const auto& node = result.cst()[id];
    if (node.hidden && node.subtreeSize == 1) {
      CHECK(result.cst().text(id) == "~");
      ++notes;
    }
  }
  CHECK(notes == 3);
  for (const std::string_view text : {"required:[~x].", "required:[x~x]."})
    rejects(parser, text);
}

void a_long_run_keeps_the_same_boundaries(const Reading& parser) {
  for (const std::size_t count : {2048u, 4096u}) {
    const std::string text = "required:[" + std::string(count, 'x') + " ] .";
    accepts(parser, text, count);
    const std::string split = "required:[" + std::string(count, 'x') + " x].";
    rejects(parser, split);
  }
}

void distinct_tokens_may_share_a_prefix(const Reading& parser) {
  for (const std::string_view source : {"same-head:[ab.", "same-head:[ ab .",
                                       "same-head:az[azabaz."})
    accepts(parser, source, 0);
  for (const std::string_view source : {"same-head:[aaab.", "same-head:[aa ab .",
                                       "same-head:az[aaazabaz."})
    accepts(parser, source, 1, "aa");
  for (const std::string_view source : {"same-head:[ aaab.",
                                       "same-head:[azaaab.",
                                       "same-head:[aaaaab."})
    rejects(parser, source);
  accepts(parser, "pattern-head:[ab.", 0);
  accepts(parser, "pattern-head:[acac ab.", 2, "ac");
  accepts(parser, "pattern-head:[acccazab.", 1, "accc");
  rejects(parser, "pattern-head:[ac ac ab.");
  accepts(parser, "short-item:[ab.", 0);
  accepts(parser, "short-item:[aab.", 1, "a");
  accepts(parser, "short-item:[a ab.", 1, "a");
  rejects(parser, "short-item:[ aab.");
  accepts(parser, "long-item:[a.", 0);
  accepts(parser, "long-item:[aba.", 1, "ab");
  accepts(parser, "long-item:[ab a.", 1, "ab");
  rejects(parser, "long-item:[ aba.");
}

void a_nest_at_the_head_keeps_the_callers_gap(const Reading& parser) {
  for (const std::string_view source :
       {"nest-head:x.x;", "nest-head:x .x;", "nest-head:x\n.x ;",
        "nest-head:~x~.x~;"}) {
    const auto result = parser.parse(source);
    if (!clean(result))
      std::fprintf(stderr, "region boundary [%s]: expected clean %.*s\n",
                   parser.engine, int(source.size()), source.data());
    CHECK(clean(result));
    const auto* doc = result.value_as<Document>();
    CHECK(doc && doc->items.size() == 1 && doc->in && doc->in->parent == doc);
    CHECK(result.cst().text() == source);
  }
  for (const std::string_view source : {"nest-head:x. x;", "nest-head:x.~x;"})
    rejects(parser, source);
}

void a_commit_at_the_tail_keeps_the_callers_gap(const Reading& parser) {
  for (const std::string_view source :
       {"commit-tail:;", "commit-tail: ;", "commit-tail:~;"})
    accepts(parser, source, 0);
  for (const std::string_view source :
       {"commit-tail:,x;", "commit-tail: ,x ;", "commit-tail:,x\n;",
        "commit-tail:~,x~;"})
    accepts(parser, source, 1);
  accepts(parser, "commit-tail:,x,x ;", 2);
  for (const std::string_view source :
       {"commit-tail:, x;", "commit-tail:,x ,x;", "commit-tail:,x~,x;"})
    rejects(parser, source);
  // A region that skips less than its caller, not only one that skips nothing.
  accepts(parser, "commit-note:[;", 0);
  accepts(parser, "commit-note:[ ,x ,x ~ ;", 2);
  accepts(parser, "commit-note:[,x~;", 1);
  rejects(parser, "commit-note:[~,x;");
  rejects(parser, "commit-note:[,x~,x;");
}

std::vector<std::string> offered(const Reading& reading, std::string text) {
  const std::size_t caret = text.find("<|>");
  text.erase(caret, 3);
  const cursiv::ParseResult result = reading.parse(text);
  std::vector<cursiv::NextFeature> next;
  const auto at = static_cast<cursiv::TextOffset>(caret);
  reading.parser.nextFeaturesAt(result.cst(), at, at, next);
  std::vector<std::string> out;
  for (const cursiv::NextFeature& f : next)
    for (const std::string_view s :
         reading.parser.getGrammar().terminalSpellings(f.feature))
      out.emplace_back(s);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// Each candidate crosses the whole gap in front of it under its own alphabet,
// the comments in it included: a region's token does not cross its caller's
// comment, and the caller's closer does.
void a_caret_crosses_the_gap_under_each_candidates_alphabet(
    const Reading& reading) {
  const auto check = [&](const std::string& text,
                         const std::vector<std::string>& want) {
    const auto got = offered(reading, text);
    CHECK(got == want);
    if (got != want) {
      std::fprintf(stderr, "region caret [%s] %s:", reading.engine,
                   text.c_str());
      for (const std::string& s : got) std::fprintf(stderr, " %s", s.c_str());
      std::fprintf(stderr, "\n");
    }
  };
  check("many:~xx~<|>].", {"]"});
  check("many:~xx<|>~].", {"]", "x"});
  check("many:xx ~ <|>].", {"]"});
  check("commit-note:[ ,x ,x ~ <|>;", {";"});
  check("commit-note:[ ,x ,x ~<|>;", {";"});
  check("commit-note:[ ,x ,x <|>~;", {",", ";"});
  check("commit-note:[,x~<|>;", {";"});
  check("commit-tail:,x~<|>;", {";"});
  check("commit-tail:,x <|>;", {";"});
}

}  // namespace

int main() {
  const auto ll = tregionboundary::generated::Ll::create_parser();
  const auto lr = tregionboundary::generated::Lr::create_parser();
  for (const Reading parser : {Reading{*ll, "LL"}, Reading{*lr, "LR"}}) {
    required_tail_keeps_the_callers_boundary(parser);
    the_written_closing_token_decides_trivia(parser);
    an_empty_tail_keeps_the_callers_boundary(parser);
    a_called_first_item_keeps_the_callers_gap(parser);
    a_larger_floor_keeps_the_same_boundaries(parser);
    hidden_trivia_is_kept_only_outside_the_region(parser);
    a_long_run_keeps_the_same_boundaries(parser);
    distinct_tokens_may_share_a_prefix(parser);
    a_nest_at_the_head_keeps_the_callers_gap(parser);
    a_commit_at_the_tail_keeps_the_callers_gap(parser);
    a_caret_crosses_the_gap_under_each_candidates_alphabet(parser);
  }
  return cursiv::test::report("RegionBoundary_test");
}
