// A translating parser against the same parser reading the decoded text: every
// record, diagnostic and completion answer is the decoded parse's, at the
// document's offsets. Java-like Unicode escapes and C-like line splices, on
// both engines.

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/SourceTranslation.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/test/Check.hpp"
#include "core/parser/TranslationGrammar.hpp"
#include "translatell/generated/Ll/Module.hpp"
#include "translatelr/generated/Lr/Module.hpp"

namespace {

using cursiv::TextOffset;
using cursiv::TextRange;

unsigned hex(char c) {
  return c <= '9' ? unsigned(c - '0') : unsigned((c | 0x20) - 'a' + 10);
}

// Java's escape, reduced: a backslash, one `u` or more, four hex digits.
void unicode_escapes(std::string_view source, cursiv::SourceTranslation& out) {
  for (auto at = source.find("\\u"); at != std::string_view::npos;
       at = source.find("\\u", at)) {
    std::size_t p = at + 1;
    while (p < source.size() && source[p] == 'u') ++p;
    unsigned value = 0;
    unsigned digits = 0;
    for (; digits < 4 && p < source.size() &&
           std::string_view("0123456789abcdefABCDEF").find(source[p]) !=
               std::string_view::npos;
         ++digits, ++p)
      value = value * 16 + hex(source[p]);
    std::string spelling;
    if (digits == 4)
      cursiv::append_scalar(spelling, value);
    else
      out.error(at, p, "malformed escape");
    out.replace(at, p, spelling);
    at = p;
  }
}

// C's line splice: a backslash before a line break joins the two lines.
void line_splices(std::string_view source, cursiv::SourceTranslation& out) {
  for (auto at = source.find("\\\n"); at != std::string_view::npos;
       at = source.find("\\\n", at + 2))
    out.replace(at, at + 2, "");
}

std::string decoded(std::string_view text, cursiv::SourceTranslator translate) {
  cursiv::SourceTranslation t(text);
  translate(text, t);
  t.finish();
  return std::string(t.text());
}

struct Engine {
  std::unique_ptr<const cursiv::Parser> plain;
  std::unique_ptr<const cursiv::Parser> unicode;
  std::unique_ptr<const cursiv::Parser> splices;
};

template <class Make>
Engine engine(Make make) {
  return {make(), cursiv::translating_parser(make(), unicode_escapes),
          cursiv::translating_parser(make(), line_splices)};
}

std::vector<std::string> names(const cursiv::ParseResult& r) {
  std::vector<std::string> out;
  if (const auto* unit = r.value_as<ttrans::Unit>())
    for (const ttrans::Decl* d : unit->decls) out.emplace_back(d->name);
  return out;
}

bool same(const cursiv::ParseDiagnostic& a, const cursiv::ParseDiagnostic& b) {
  return a.kind == b.kind && a.element == b.element && a.also == b.also &&
         a.begin == b.begin && a.end == b.end && a.message == b.message;
}

// The leaf whose span starts at `begin`, visible or hidden.
TextRange leaf_at(const cursiv::Cst& cst, TextOffset begin) {
  TextRange found{~0u, ~0u};
  cst.visit_leaves([&](const cursiv::CstLeaf& l) {
    if (l.begin == begin && found.begin == ~0u) found = {l.begin, l.end};
  });
  return found;
}

std::vector<cursiv::NextFeature> features(const cursiv::Parser& p,
                                          const cursiv::Cst& cst,
                                          TextOffset at) {
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(cst, at, at, out);
  return out;
}

bool same(const std::vector<cursiv::NextFeature>& a,
          const std::vector<cursiv::NextFeature>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].feature != b[i].feature || a[i].property != b[i].property ||
        a[i].narrow != b[i].narrow || a[i].type != b[i].type)
      return false;
  return true;
}

// The translated parse is the decoded parse at the document's offsets: same
// records, repairs and AST, and completion answers the decoded text's except
// strictly inside a replaced piece.
cursiv::ParseResult check_against_decoded(const Engine& e,
                                          const cursiv::Parser& translating,
                                          cursiv::SourceTranslator translate,
                                          const std::string& text) {
  cursiv::ParseResult r = translating.parse(text);
  const std::string plainText = decoded(text, translate);
  const cursiv::ParseResult p = e.plain->parse(plainText);
  const cursiv::SourceTranslation* t = r.cst().translation();
  CHECK(t != nullptr);
  if (t == nullptr) return r;
  CHECK(t->text() == plainText);
  CHECK(r.cst().text() == text);
  CHECK(names(r) == names(p));
  CHECK(r.accepted == p.accepted && r.error == p.error);
  const cursiv::Cst& rc = r.cst();
  const cursiv::Cst& pc = p.cst();
  CHECK(rc.size() == pc.size());
  CHECK(rc.entry_root() == pc.entry_root());
  for (cursiv::NodeId id = 0; id < rc.size() && id < pc.size(); ++id) {
    const cursiv::CstNode& a = rc[id];
    const cursiv::CstNode& b = pc[id];
    const TextRange moved = t->original({b.begin, b.end});
    CHECK(a.begin == moved.begin && a.end == moved.end);
    CHECK(a.subtreeSize == b.subtreeSize && a.engine == b.engine &&
          a.hidden == b.hidden && a.recovered == b.recovered);
  }
  CHECK(rc.diagnostics().size() == pc.diagnostics().size());
  const std::size_t errors = t->errors().size();
  CHECK(r.diagnostics.size() == p.diagnostics.size() + errors);
  for (std::size_t i = 0; i < p.diagnostics.size() && i < r.diagnostics.size();
       ++i) {
    cursiv::ParseDiagnostic moved = p.diagnostics[i];
    const TextRange span = t->original({moved.begin, moved.end});
    moved.begin = span.begin;
    moved.end = span.end;
    CHECK(same(r.diagnostics[i], moved));
  }
  for (TextOffset at = 0; at <= text.size(); ++at) {
    const auto inText = t->translated(at);
    const auto got = features(translating, rc, at);
    if (!inText)
      CHECK(got.empty());
    else
      CHECK(same(got, features(*e.plain, pc, *inText)));
  }
  return r;
}

void a_keyword_spelled_with_an_escape(const Engine& e) {
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes,
                                       "cl\\u0061ss A;");
  CHECK(r && names(r) == std::vector<std::string>{"A"});
  CHECK(leaf_at(r.cst(), 0).end == 10);
  CHECK(r.diagnostics.empty());
}

void an_escape_ends_a_line_comment(const Engine& e) {
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes,
                                       "// note\\u000aclass B;");
  CHECK(r && names(r) == std::vector<std::string>{"B"});
  CHECK(leaf_at(r.cst(), 0).end == 7);
  CHECK(leaf_at(r.cst(), 13).end == 18);
}

void an_escape_inside_an_identifier(const Engine& e) {
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes,
                                       "class f\\u006fo; class caf\\uu00e9;");
  CHECK(r && names(r) == std::vector<std::string>{"foo", "caf\xc3\xa9"});
  CHECK(leaf_at(r.cst(), 6).end == 14);
  CHECK(leaf_at(r.cst(), 22).end == 32);
}

void a_malformed_escape_is_reported_where_it_stands(const Engine& e) {
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes,
                                       "class A\\u00zz;");
  CHECK(names(r) == std::vector<std::string>{"Azz"});
  CHECK(r.diagnostics.size() == 1);
  if (r.diagnostics.size() == 1) {
    const auto& d = r.diagnostics.front();
    CHECK(d.kind == cursiv::DiagnosticKind::conversion_error);
    CHECK(d.begin == 7 && d.end == 11 && d.message == "malformed escape");
  }
}

void a_repair_after_an_escape_is_reported_in_the_document(const Engine& e) {
  const std::string text = "class \\u0041\\u0042 class C;";
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes, text);
  CHECK(!r.diagnostics.empty());
  for (const auto& d : r.diagnostics) CHECK(d.begin >= 18 && d.end >= 18);
}

void completion_refuses_a_caret_inside_an_escape(const Engine& e) {
  const std::string text = "cl\\u0061ss A; ";
  const auto r = check_against_decoded(e, *e.unicode, unicode_escapes, text);
  CHECK(!features(*e.unicode, r.cst(), 2).empty());
  CHECK(features(*e.unicode, r.cst(), 3).empty());
  CHECK(!features(*e.unicode, r.cst(), 8).empty());
  CHECK(!features(*e.unicode, r.cst(), TextOffset(text.size())).empty());
}

void a_name_span_maps_back_to_the_document(const Engine& e) {
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///t", "t", 0, "class f\\u006fo; class bar;"));
  document.parseResult = e.unicode->parse(document.textDocument().getText());
  const auto* unit = document.parseResult.value_as<ttrans::Unit>();
  CHECK(unit != nullptr && unit->decls.size() == 2);
  if (unit == nullptr || unit->decls.size() != 2) return;
  const auto discard = [](cursiv::Diagnostic&&) {};
  const cursiv::ValidationAcceptor accept(discard, document, "t");
  const ttrans::Decl& escaped = *unit->decls[0];
  const TextRange a = accept.spanOfName(escaped.name, escaped);
  CHECK(a.begin == 6 && a.end == 14);
  const ttrans::Decl& plain = *unit->decls[1];
  const TextRange b = accept.spanOfName(plain.name, plain);
  CHECK(b.begin == 22 && b.end == 25);
}

void line_splices_join_tokens_and_comments(const Engine& e) {
  const auto keyword = check_against_decoded(e, *e.splices, line_splices,
                                             "cla\\\nss A;");
  CHECK(keyword && names(keyword) == std::vector<std::string>{"A"});
  CHECK(leaf_at(keyword.cst(), 0).end == 7);
  const auto name = check_against_decoded(e, *e.splices, line_splices,
                                          "class B\\\nC;");
  CHECK(name && names(name) == std::vector<std::string>{"BC"});
  CHECK(leaf_at(name.cst(), 6).end == 10);
  const auto comment = check_against_decoded(
      e, *e.splices, line_splices, "// x \\\nclass C;\nclass D;");
  CHECK(comment && names(comment) == std::vector<std::string>{"D"});
}

// Untouched text takes the plain parser's path: no translation, and the very
// result the plain parser gives.
void untouched_text_is_parsed_as_it_is(const Engine& e) {
  const std::string text = "class A; // plain\nclass B";
  const auto r = e.unicode->parse(text);
  const auto p = e.plain->parse(text);
  CHECK(r.cst().translation() == nullptr);
  CHECK(names(r) == names(p) && r.accepted == p.accepted);
  CHECK(r.cst().size() == p.cst().size());
  for (cursiv::NodeId id = 0; id < r.cst().size() && id < p.cst().size(); ++id)
    CHECK(std::memcmp(&r.cst()[id], &p.cst()[id], sizeof(cursiv::CstNode)) ==
          0);
  CHECK(r.diagnostics.size() == p.diagnostics.size());
  for (std::size_t i = 0; i < r.diagnostics.size() && i < p.diagnostics.size();
       ++i)
    CHECK(same(r.diagnostics[i], p.diagnostics[i]));
  for (TextOffset at = 0; at <= text.size(); ++at)
    CHECK(same(features(*e.unicode, r.cst(), at),
               features(*e.plain, p.cst(), at)));
}

// A parser that hands every question on: a translation behind it is out of
// sight until a parse.
struct Relay final : cursiv::Parser {
  explicit Relay(std::unique_ptr<const cursiv::Parser> parser)
      : inner(std::move(parser)) {}
  const cursiv::Grammar& getGrammar() const noexcept override {
    return inner->getGrammar();
  }
  cursiv::ParseResult parse(
      std::string_view text,
      const cursiv::CancellationToken& cancel) const override {
    return inner->parse(text, cancel);
  }
  std::unique_ptr<const cursiv::Parser> inner;
};

template <class F>
bool refused(F f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

// One translator does every phase: a translating parser refuses another, when
// wrapped over it, or at a parse both translate where a parser in between
// hides it.
template <class Make>
void translations_do_not_nest(Make make) {
  CHECK(refused([&] {
    (void)cursiv::translating_parser(
        cursiv::translating_parser(make(), unicode_escapes), line_splices);
  }));
  const auto hidden = cursiv::translating_parser(
      std::make_unique<Relay>(
          cursiv::translating_parser(make(), unicode_escapes)),
      line_splices);
  CHECK(refused([&] { (void)hidden->parse("cl\\u0061ss A\\\nB;"); }));
  CHECK(names(hidden->parse("cl\\u0061ss A;")) ==
        std::vector<std::string>{"A"});
  CHECK(names(hidden->parse("class A\\\nB;")) ==
        std::vector<std::string>{"AB"});
}

void run(const Engine& e) {
  a_keyword_spelled_with_an_escape(e);
  an_escape_ends_a_line_comment(e);
  an_escape_inside_an_identifier(e);
  a_malformed_escape_is_reported_where_it_stands(e);
  a_repair_after_an_escape_is_reported_in_the_document(e);
  completion_refuses_a_caret_inside_an_escape(e);
  a_name_span_maps_back_to_the_document(e);
  line_splices_join_tokens_and_comments(e);
  untouched_text_is_parsed_as_it_is(e);
}

}  // namespace

int main() {
  run(engine(&ttrans::generated::Lr::create_parser));
  run(engine(&ttrans::generated::Ll::create_parser));
  translations_do_not_nest(&ttrans::generated::Lr::create_parser);
  translations_do_not_nest(&ttrans::generated::Ll::create_parser);
  return cursiv::test::report("source_translation");
}
