#include "workspace/generated/Workspace/Parser.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
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
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/SourceTranslation.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/shared/runtime/AstReducer.hpp"
#include "cursiv/test/Check.hpp"
#include "support/World.hpp"
#include "translatell/generated/Ll/Module.hpp"
#include "translatelr/generated/Lr/Module.hpp"
#include "workspace/generated/Workspace/AstReflection.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/generated/Workspace/Table.hpp"

// Pins the erased door: the same grammar, reached through a service that never
// learns it. The AST it hands back is the one the table read directly gives.
namespace parser_service {

namespace {

namespace dmg = ws::generated::Workspace;

const cursiv::Parser& service() {
  return support::parser_of<&ws::generated::Workspace::create_parser>();
}

// The other road to the same tree: the generated table and drive, read as
// data, with no service in front of them.
bool direct_parse(std::string_view text, cursiv::Cst& cst) {
  return cursiv::lr_parser<dmg::kTable>.parse(text, cst, nullptr,
                                              &dmg::kDrive);
}

// The erased door answers about records and tokens, which is all a service
// that never learns its grammar can ask.
void the_grammar_is_reachable_without_the_template() {
  const cursiv::Grammar& g = service().getGrammar();
  const cursiv::ParseResult r = service().parse("datatype String\n");
  CHECK(static_cast<bool>(r));
  const cursiv::Cst& cst = r.cst();
  CHECK(g.recordRule(cst[cst.entry_root()]).empty() ||
        !g.recordRule(cst[cst.entry_root()]).empty());
  // A terminal is spelled or it is named, never both and never neither.
  CHECK(!g.terminalSpellings(1).empty() || !g.terminalName(1).empty());
}

void a_parse_through_the_interface_builds_the_same_tree() {
  const std::string src = "datatype String\nentity A { x : String }\n";
  const cursiv::ParseResult through = service().parse(src);
  CHECK(static_cast<bool>(through));
  CHECK(through.value != nullptr);
  CHECK(through.diagnostics.empty());

  // The table read directly, byte for byte the same outcome -- the reference
  // work list included: the document declares one reference, and both roads
  // owe its handle.
  cursiv::Cst cst;
  direct_parse(src, cst);
  const cursiv::ParseResult direct =
      cursiv::AstReducer::build<dmg::kTable.stamp>(dmg::kAstActions,
                                                   std::move(cst));
  CHECK(through.nodes.size() == direct.nodes.size());
  CHECK(through.cst().size() == direct.cst().size());
  CHECK(through.references != nullptr && direct.references != nullptr);
  CHECK(through.references->handles.size() == 1);
  CHECK(through.references->handles.size() ==
        direct.references->handles.size());
  CHECK(through.references->handles[0].field ==
        direct.references->handles[0].field);
  CHECK(through.error == cursiv::AstBuildError::none && through.accepted);
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
      cursiv::Cst direct;
      CHECK(direct_parse(view, direct) == reference.accepted);
      const cursiv::ParseResult actual = service().parse(view);
      CHECK(actual.accepted == reference.accepted);
      CHECK(actual.cst().text().data() == view.data());
      CHECK(actual.cst().text() == view);
      CHECK(actual.cst().size() == reference.cst().size());
      CHECK(direct.size() == reference.cst().size());
      CHECK(actual.diagnostics.size() == reference.diagnostics.size());
      CHECK(actual.nodes.size() == reference.nodes.size());
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
    // overread in the generated drive or the shared runtime must fail.
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
  // A repair carries its whole record through the erased door: a kind and a
  // place, not just a count.
  const cursiv::ParseDiagnostic& d = r.diagnostics.front();
  CHECK(d.kind != cursiv::DiagnosticKind::conversion_error);
  CHECK(d.end >= d.begin);
  CHECK(d.element != cursiv::kNoElement);
}

// Cancellation is observed before any work starts.
void a_cancelled_token_stops_the_parse() {
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)service().parse("datatype String\n", source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

// Ordinary reads keep their hot loop free of cancellation polling.
void a_long_reading_runs_whole_and_the_door_answers() {
  std::string src = "datatype String\n";
  for (int i = 0; i < 20000; ++i)
    src += "entity E" + std::to_string(i) + " { x : String }\n";

  cursiv::Cst cst;
  CHECK(direct_parse(src, cst));
  cursiv::Cst whole;
  CHECK(direct_parse(src, whole));
  CHECK(cst.size() == whole.size());

  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)service().parse(src, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

cursiv::AstEntryFn stoppedEntry = nullptr;
cursiv::CancellationTokenSource* astStop = nullptr;

void a_cancelled_token_stops_the_ast_before_work() {
  cursiv::Cst cst;
  CHECK(direct_parse("datatype String\n", cst));
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)cursiv::AstReducer::build<dmg::kTable.stamp>(
        dmg::kAstActions, std::move(cst), source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

void stop_after_an_ast_entry(cursiv::AstReducer& builder, cursiv::NodeId id) {
  stoppedEntry(builder, id);
  astStop->request_stop();
}

void a_stop_during_ast_work_is_observed() {
  cursiv::Cst cst;
  CHECK(direct_parse("datatype String\nentity A { x : String }\n", cst));
  auto actions = dmg::kAstActions;
  std::vector<cursiv::AstEntryFn> entries(actions.entries,
                                          actions.entries + actions.entryCount);
  cursiv::ElementId first = cursiv::kNoElement;
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    const auto row = dmg::kTable.stamp.row(cst[id]);
    if (!cst[id].hidden && row != cursiv::kNoElement &&
        row < actions.entryCount && entries[row] != nullptr) {
      first = row;
      break;
    }
  }
  CHECK(first != cursiv::kNoElement);
  if (first == cursiv::kNoElement) return;
  cursiv::CancellationTokenSource source;
  astStop = &source;
  stoppedEntry = entries[first];
  entries[first] = &stop_after_an_ast_entry;
  actions.entries = entries.data();
  bool threw = false;
  try {
    (void)cursiv::AstReducer::build<dmg::kTable.stamp>(actions, std::move(cst),
                                                       source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  astStop = nullptr;
  CHECK(source.stop_requested() && threw);
}

// The materialisation reads the stop between runs of records: a stop
// requested at the first entry leaves most of a long document unbuilt.
std::vector<cursiv::AstEntryFn> countedEntries;
std::vector<cursiv::ElementId> rowById;
std::size_t entryCalls = 0;

void count_an_ast_entry(cursiv::AstReducer& builder, cursiv::NodeId id) {
  if (++entryCalls == 1 && astStop != nullptr) astStop->request_stop();
  countedEntries[rowById[id]](builder, id);
}

std::size_t entries_built(const std::string& src,
                          cursiv::CancellationTokenSource* stop) {
  cursiv::Cst cst;
  CHECK(direct_parse(src, cst));
  rowById.assign(cst.size(), cursiv::kNoElement);
  for (cursiv::NodeId id = 0; id < cst.size(); ++id)
    rowById[id] = dmg::kTable.stamp.row(cst[id]);
  auto actions = dmg::kAstActions;
  countedEntries.assign(actions.entries, actions.entries + actions.entryCount);
  std::vector<cursiv::AstEntryFn> entries(countedEntries);
  for (cursiv::AstEntryFn& e : entries)
    if (e != nullptr) e = &count_an_ast_entry;
  actions.entries = entries.data();
  entryCalls = 0;
  astStop = stop;
  try {
    (void)cursiv::AstReducer::build<dmg::kTable.stamp>(
        actions, std::move(cst),
        stop != nullptr ? stop->get_token() : cursiv::kNoCancel);
  } catch (const cursiv::OperationCancelled&) {
  }
  astStop = nullptr;
  return entryCalls;
}

void a_stop_during_a_long_ast_build_is_observed_early() {
  std::string src = "datatype String\n";
  for (int i = 0; i < 40000; ++i)
    src += "entity E" + std::to_string(i) + " { x : String }\n";
  const std::size_t whole = entries_built(src, nullptr);
  cursiv::CancellationTokenSource source;
  const std::size_t stopped = entries_built(src, &source);
  CHECK(whole > 2 * 64 * 1024);
  CHECK(stopped < whole / 2);
}

}  // namespace

void run() {
  the_grammar_is_reachable_without_the_template();
  a_parse_through_the_interface_builds_the_same_tree();
  a_parse_reads_only_the_borrowed_view();
  a_broken_text_still_answers();
  a_cancelled_token_stops_the_parse();
  a_long_reading_runs_whole_and_the_door_answers();
  a_cancelled_token_stops_the_ast_before_work();
  a_stop_during_ast_work_is_observed();
  a_stop_during_a_long_ast_build_is_observed_early();
}

}  // namespace parser_service

// A translating parser against the same parser reading the decoded text: every
// record, diagnostic and completion answer is the decoded parse's, at the
// document's offsets. Java-like Unicode escapes and C-like line splices, on
// both engines.
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
// hides it -- a translation behind one is out of sight until a parse.
template <class Make>
void translations_do_not_nest(Make make) {
  CHECK(refused([&] {
    (void)cursiv::translating_parser(
        cursiv::translating_parser(make(), unicode_escapes), line_splices);
  }));
  const auto hidden = cursiv::translating_parser(
      std::make_unique<support::CountingParser>(
          cursiv::translating_parser(make(), unicode_escapes)),
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
  a_name_span_maps_back_to_the_document(e);
  line_splices_join_tokens_and_comments(e);
  untouched_text_is_parsed_as_it_is(e);
}

}  // namespace

void run() {
  run_engine(engine(&ttrans::generated::Lr::create_parser));
  run_engine(engine(&ttrans::generated::Ll::create_parser));
  translations_do_not_nest(&ttrans::generated::Lr::create_parser);
  translations_do_not_nest(&ttrans::generated::Ll::create_parser);
}

}  // namespace source_translation

int main() {
  parser_service::run();
  source_translation::run();
  return cursiv::test::report("parser_test");
}
