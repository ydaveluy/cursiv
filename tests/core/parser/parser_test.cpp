#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "core/parser/TranslationGrammar.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/Check.hpp"
#include "support/Throws.hpp"
#include "support/World.hpp"
#include "translatelr/generated/Base/Module.hpp"
#include "workspace/generated/Workspace/Module.hpp"

// The type-erased parser: a service reaches the grammar without knowing its
// type, and gets its AST.
namespace parser_service {

namespace {

const cursiv::Parser& service() {
  return support::parser_of<&ws::generated::Workspace::createParser>();
}

// The type-erased grammar answers about records and tokens, all a service
// that does not know the grammar's type can ask.
void the_grammar_is_reachable_without_the_template() {
  const cursiv::Grammar& g = service().getGrammar();
  const cursiv::ParseResult r = service().parse("datatype String\n");
  CHECK(static_cast<bool>(r));
  const cursiv::Cst& cst = r.cst();
  // The root converts nothing; the name's leaf converts its terminal.
  CHECK(g.recordRule(cst[cst.entryRoot()]).empty());
  bool named = false;
  for (cursiv::NodeId id = 0; id < cst.size(); ++id)
    named = named || g.recordRule(cst[id]) == "ID";
  CHECK(named);
  // A terminal is spelled or it is named, never both and never neither.
  CHECK(!g.terminalSpellings(1).empty() || !g.terminalName(1).empty());
}

void a_parse_through_the_interface_builds_the_tree() {
  const std::string src = "datatype String\nentity A { x : String }\n";
  const cursiv::ParseResult through = service().parse(src);
  CHECK(static_cast<bool>(through));
  CHECK(through.value != nullptr);
  CHECK(through.diagnostics.empty());

  // The document declares one reference, and the result gives its handle.
  CHECK(through.references != nullptr);
  CHECK(through.references != nullptr && through.references->handles.size() == 1);
  CHECK(through.error == cursiv::AstBuildError::None && through.accepted);
}

// Full parses and repairs must stop at the view's end and borrow its bytes.
void a_parse_reads_only_the_borrowed_view() {
  const std::string cases[] = {
      "datatype String\nentity A { x : String }\n",
      "datatype String\nentity A { x  String }\n",
      "datatype String\nentity \xE2\x82",
      std::string("datatype String\n// a") + '\0' + "b\n"};
  for (const std::string& text : cases) {
    const cursiv::ParseResult reference = service().parse(text);
    const auto check = [&](std::string_view view) {
      const cursiv::ParseResult actual = service().parse(view);
      CHECK(actual.accepted == reference.accepted);
      CHECK(actual.cst().text().data() == view.data());
      CHECK(actual.cst().text() == view);
      CHECK(actual.cst().size() == reference.cst().size());
      CHECK(actual.diagnostics.size() == reference.diagnostics.size());
      CHECK(actual.nodes().size() == reference.nodes().size());
      for (cursiv::NodeId i = 0;
           i < actual.cst().size() && i < reference.cst().size(); ++i)
        CHECK(std::memcmp(&actual.cst()[i], &reference.cst()[i],
                          sizeof(cursiv::CstNode)) == 0);
    };
    const std::string followed = text + "unexpected suffix";
    check(std::string_view(followed.data(), text.size()));
    auto exact = std::make_unique<char[]>(text.size());
    std::memcpy(exact.get(), text.data(), text.size());
    check(std::string_view(exact.get(), text.size()));
#if defined(__unix__) || defined(__APPLE__)
    // End the view immediately before an unreadable page: even a one-byte
    // overread must fail.
    const long pageSize = sysconf(_SC_PAGESIZE);
    CHECK(pageSize > 0 && text.size() < static_cast<std::size_t>(pageSize));
    if (pageSize <= 0 || text.size() >= static_cast<std::size_t>(pageSize))
      continue;
    const auto page = static_cast<std::size_t>(pageSize);
    void* region = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    if (region == MAP_FAILED) continue;
    char* const end = static_cast<char*>(region) + page;
    const bool protectedEnd = mprotect(end, page, PROT_NONE) == 0;
    CHECK(protectedEnd);
    if (protectedEnd) {
      std::memcpy(end - text.size(), text.data(), text.size());
      check(std::string_view(end - text.size(), text.size()));
    }
    CHECK(munmap(region, 2 * page) == 0);
#endif
  }
  const cursiv::ParseResult empty = service().parse({});
  CHECK(empty.cst().text().empty() && empty.cst().text().data() != nullptr);
}

// A repair reports through the interface exactly as it does under it.
void a_broken_text_still_answers() {
  const std::string src = "datatype String\nentity A { x  String }\n";
  const cursiv::ParseResult r = service().parse(src);
  CHECK(!r.diagnostics.empty());
  CHECK(r.value != nullptr);
  // The result's list is the tree's repairs, not a second bookkeeping.
  CHECK(r.diagnostics.size() == r.cst().diagnostics().size());
  // A repair keeps its whole record through the type-erased interface: a kind
  // and a place, not just a count.
  const cursiv::ParseDiagnostic& d = r.diagnostics.front();
  CHECK(d.kind != cursiv::DiagnosticKind::ConversionError);
  CHECK(d.end >= d.begin);
  CHECK(d.element != cursiv::kNoElement);
}

// Cancellation is observed before any work starts.
void a_cancelled_token_stops_the_parse() {
  cursiv::CancellationTokenSource source;
  source.request_stop();
  CHECK(support::throws<cursiv::OperationCancelled>([&] {
    (void)service().parse("datatype String\n", source.get_token());
  }));
}

// Ordinary reads keep their hot loop free of cancellation polling.
void a_long_reading_runs_whole_and_the_service_still_cancels() {
  std::string src = "datatype String\n";
  for (int i = 0; i < 20000; ++i)
    src += "entity E" + std::to_string(i) + " { x : String }\n";

  const cursiv::ParseResult whole = service().parse(src);
  CHECK(whole.accepted && whole.diagnostics.empty());

  cursiv::CancellationTokenSource source;
  source.request_stop();
  CHECK(support::throws<cursiv::OperationCancelled>([&] {
    (void)service().parse(src, source.get_token());
  }));
}

}  // namespace

void run() {
  the_grammar_is_reachable_without_the_template();
  a_parse_through_the_interface_builds_the_tree();
  a_parse_reads_only_the_borrowed_view();
  a_broken_text_still_answers();
  a_cancelled_token_stops_the_parse();
  a_long_reading_runs_whole_and_the_service_still_cancels();
}

}  // namespace parser_service

// A translating parser against the same parser reading the decoded text:
// every record, diagnostic and completion is the decoded parse's, at the
// document's offsets.
namespace source_translation {

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
      cursiv::appendScalar(spelling, value);
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

// A sigil that spells a keyword and the blank after it, one piece and two
// tokens; another spells a declaration missing its name, which a repair
// inserts inside the piece.
void class_sigils(std::string_view source, cursiv::SourceTranslation& out) {
  for (auto at = source.find_first_of("$%"); at != std::string_view::npos;
       at = source.find_first_of("$%", at + 1))
    out.replace(at, at + 1, source[at] == '$' ? "class " : "class ;");
}

// A declaration after another may leave out its keyword, which is inserted
// after the semicolon.
void implied_keywords(std::string_view source, cursiv::SourceTranslation& out) {
  for (auto at = source.find("; "); at != std::string_view::npos;
       at = source.find("; ", at + 1))
    out.replace(at + 1, at + 1, "class");
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
  std::unique_ptr<const cursiv::Parser> sigils;
  std::unique_ptr<const cursiv::Parser> implied;
};

template <class Make>
Engine engine(Make make) {
  return {make(), cursiv::translatingParser(make(), unicode_escapes),
          cursiv::translatingParser(make(), line_splices),
          cursiv::translatingParser(make(), class_sigils),
          cursiv::translatingParser(make(), implied_keywords)};
}

std::vector<std::string> names(const cursiv::ParseResult& r) {
  std::vector<std::string> out;
  if (const auto* unit = r.valueAs<ttrans::Unit>())
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
  cst.visitLeaves([&](const cursiv::CstLeaf& l) {
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
  CHECK(rc.entryRoot() == pc.entryRoot());
  for (cursiv::NodeId id = 0; id < rc.size() && id < pc.size(); ++id) {
    const cursiv::CstNode& a = rc[id];
    const cursiv::CstNode& b = pc[id];
    const TextRange moved = t->original({b.begin, b.end});
    CHECK(a.begin == moved.begin && a.end == moved.end);
    CHECK(rc.spelling(id) == pc.text(id));
    CHECK(a.subtreeSize == b.subtreeSize && a.isHidden() == b.isHidden() &&
          a.isRecovered() == b.isRecovered());
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
    CHECK(d.kind == cursiv::DiagnosticKind::ConversionError);
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

// A stopped completion throws wherever the caret stands, inside an escape too.
void a_stopped_completion_throws(const Engine& e) {
  const auto r = e.unicode->parse("cl\\u0061ss A; ");
  cursiv::CancellationTokenSource source;
  source.request_stop();
  for (const TextOffset at : {TextOffset(2), TextOffset(3)}) {
    std::vector<cursiv::NextFeature> out;
    CHECK(support::throws<cursiv::OperationCancelled>([&] {
      e.unicode->nextFeaturesAt(r.cst(), at, at, out, source.get_token());
    }));
  }
}

void a_name_span_maps_back_to_the_document(const Engine& e) {
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///t", "t", 0, "class f\\u006fo; class bar;"));
  document.parseResult = e.unicode->parse(document.textDocument().getText());
  const auto* unit = document.parseResult.valueAs<ttrans::Unit>();
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

// A keyword is found by the text the parser read: written with an escape, or
// inside a piece that spells several tokens.
void a_keyword_anchor_reads_the_parsed_spelling(const Engine& e) {
  const auto anchor = [](const cursiv::Parser& parser, std::string text) {
    cursiv::Document document(
        cursiv::TextDocument::create("file:///t", "t", 0, std::move(text)));
    document.parseResult = parser.parse(document.textDocument().getText());
    const auto* unit = document.parseResult.valueAs<ttrans::Unit>();
    CHECK(unit != nullptr && unit->decls.size() == 1);
    std::optional<TextRange> found;
    if (unit == nullptr || unit->decls.size() != 1) return found;
    if (const auto keyword = cursiv::findNodeForKeyword(
            *unit->decls[0], "class", 0, &document)) {
      CHECK(keyword.cst->spelling(keyword.id) == "class");
      found = TextRange{keyword.begin(), keyword.end()};
    }
    return found;
  };
  const auto plain = anchor(*e.plain, "class A;");
  CHECK(plain && plain->begin == 0 && plain->end == 5);
  const auto escaped = anchor(*e.unicode, "cl\\u0061ss A;");
  CHECK(escaped && escaped->begin == 0 && escaped->end == 10);
  const auto sigil = anchor(*e.sigils, "$A;");
  CHECK(sigil && sigil->begin == 0 && sigil->end == 1);
}

// A caret behind a token stands in a word when the parsed spelling ends in
// one: an escape that spells `;` is punctuation, one that spells `o` a letter.
void a_word_is_read_in_the_parsed_spelling(const Engine& e) {
  const std::string punctuation = "class A\\u003b";
  const auto p = e.unicode->parse(punctuation);
  CHECK(!cursiv::detail::word_at(p.cst(), TextOffset(punctuation.size())));
  const std::string letter = "class f\\u006f";
  const auto l = e.unicode->parse(letter);
  CHECK(cursiv::detail::word_at(l.cst(), TextOffset(letter.size())));
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

// A piece whose spelling holds several tokens widens each of them to the
// whole piece in the document; completion still reads the spans the engine
// wrote, so it answers as the decoded text does.
void a_piece_spelling_several_tokens_completes_as_decoded(const Engine& e) {
  const std::string text = "$A; $B; ";
  const auto r = check_against_decoded(e, *e.sigils, class_sigils, text);
  CHECK(r && names(r) == std::vector<std::string>{"A", "B"});
  CHECK(leaf_at(r.cst(), 0).end == 1);
  CHECK(!features(*e.sigils, r.cst(), TextOffset(text.size())).empty());
  const auto repaired =
      check_against_decoded(e, *e.sigils, class_sigils, "% $B; ");
  CHECK(repaired.diagnostics.size() == 1);
  if (repaired.diagnostics.size() == 1)
    CHECK(repaired.diagnostics[0].begin == 0 &&
          repaired.diagnostics[0].end == 1);
}

// An inserted keyword shares a document offset with the token before it:
// each still spells what the parser read.
void an_inserted_keyword_keeps_the_spans_around_it(const Engine& e) {
  const std::string text = "class A; B; C";
  const auto r = check_against_decoded(e, *e.implied, implied_keywords, text);
  CHECK(names(r) == std::vector<std::string>{"A", "B", "C"});
  CHECK(leaf_at(r.cst(), 7).end == 8);
  CHECK(!features(*e.implied, r.cst(), TextOffset(text.size())).empty());
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

template <class F>
bool refused(F f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

// A translating parser refuses another one wrapped inside it, at construction,
// or at the first parse when a parser in between hides it.
template <class Make>
void translations_do_not_nest(Make make) {
  CHECK(refused([&] {
    (void)cursiv::translatingParser(
        cursiv::translatingParser(make(), unicode_escapes), line_splices);
  }));
  const auto hidden = cursiv::translatingParser(
      std::make_unique<support::CountingParser>(
          cursiv::translatingParser(make(), unicode_escapes)),
      line_splices);
  CHECK(refused([&] { (void)hidden->parse("cl\\u0061ss A\\\nB;"); }));
  CHECK(names(hidden->parse("cl\\u0061ss A;")) ==
        std::vector<std::string>{"A"});
  CHECK(names(hidden->parse("class A\\\nB;")) ==
        std::vector<std::string>{"AB"});
}

void run_engine(const Engine& e) {
  a_keyword_spelled_with_an_escape(e);
  an_escape_ends_a_line_comment(e);
  an_escape_inside_an_identifier(e);
  a_malformed_escape_is_reported_where_it_stands(e);
  a_repair_after_an_escape_is_reported_in_the_document(e);
  completion_refuses_a_caret_inside_an_escape(e);
  a_stopped_completion_throws(e);
  a_name_span_maps_back_to_the_document(e);
  a_keyword_anchor_reads_the_parsed_spelling(e);
  a_word_is_read_in_the_parsed_spelling(e);
  line_splices_join_tokens_and_comments(e);
  a_piece_spelling_several_tokens_completes_as_decoded(e);
  an_inserted_keyword_keeps_the_spans_around_it(e);
  untouched_text_is_parsed_as_it_is(e);
}

}  // namespace

void run() {
  run_engine(engine(&ttrans::generated::Base::createParser));
  translations_do_not_nest(&ttrans::generated::Base::createParser);
}

}  // namespace source_translation

int main() {
  parser_service::run();
  source_translation::run();
  return cursiv::test::report("parser_test");
}
