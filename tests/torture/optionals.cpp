// Surplus optional clauses use auxiliaries while preserving the values and
// order of the distributed body, without moving end assertions and without
// adding conflicts where a clause opens on the keyword that may follow it.

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "optionals/generated/OptionalsG/Module.hpp"
#include "optionals/generated/OptionalsG/Table.hpp"
#include "torture/OptionalsGrammar.hpp"

namespace {

namespace ast = topt::ast;
namespace g = topt::generated::OptionalsG;

std::string_view ast::Opts::* const kFields[] = {
    &ast::Opts::a01, &ast::Opts::a02, &ast::Opts::a03, &ast::Opts::a04,
    &ast::Opts::a05, &ast::Opts::a06, &ast::Opts::a07, &ast::Opts::a08,
    &ast::Opts::a09, &ast::Opts::a10, &ast::Opts::a11, &ast::Opts::a12,
    &ast::Opts::a13, &ast::Opts::a14, &ast::Opts::a15, &ast::Opts::a16,
    &ast::Opts::a17, &ast::Opts::a18, &ast::Opts::a19, &ast::Opts::a20};
constexpr unsigned kClauses = 20;

std::string_view ast::Storey::* const kStoreyFields[] = {
    &ast::Storey::a01, &ast::Storey::a02, &ast::Storey::a03, &ast::Storey::a04};
constexpr unsigned kStoreyClauses = 4;

bool clean(const cursiv::ParseResult& r) {
  return r.cst().accepted() && !r.cst().recovered();
}

// A reading no repair finishes still has to leave a tape a reader can walk,
// and an accepted one a tree.
void walkable(const cursiv::Parser& parser, std::string_view broken) {
  const cursiv::ParseResult r = parser.parse(broken);
  const cursiv::Cst& cst = r.cst();
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    CHECK(cst[i].begin <= cst[i].end && cst[i].end <= broken.size());
  CHECK(!cst.accepted() || r.value != nullptr);
}

// The block, and what every clause is expected to hold after it: an empty
// expectation is a clause the document leaves out.
void one(const cursiv::Parser& parser, const std::string& text,
         const std::string (&want)[kClauses], std::size_t items) {
  const cursiv::ParseResult r = parser.parse(text);
  if (!clean(r)) {
    std::printf("  FAIL optionals: `%s` was not taken cleanly\n", text.c_str());
    ++failures;
    return;
  }
  const ast::Opts* o = r.value_as<ast::Opts>();
  CHECK(o != nullptr);
  if (o == nullptr) return;
  for (unsigned i = 0; i < kClauses; ++i)
    if (o->*kFields[i] != want[i]) {
      std::printf("  FAIL optionals: clause %u holds `%.*s`, wanted `%s`\n",
                  i + 1, static_cast<int>((o->*kFields[i]).size()),
                  (o->*kFields[i]).data(), want[i].c_str());
      ++failures;
    }
  CHECK(o->items.size() == items);
}

void optionals(const cursiv::Parser& parser) {
  const std::string none[kClauses] = {};
  one(parser, "optionals {}", none, 0);

  std::string all = "optionals {";
  std::string wantAll[kClauses];
  for (unsigned i = 0; i < kClauses; ++i) {
    char kw[8], v[8];
    std::snprintf(kw, sizeof kw, " k%02u", i + 1);
    std::snprintf(v, sizeof v, " v%02u", i + 1);
    all += kw;
    all += v;
    wantAll[i] = v + 1;
  }
  for (unsigned i = 1; i <= 6; ++i) {
    all += " r" + std::to_string(i) + " i" + std::to_string(i);
  }
  all += " }";
  one(parser, all, wantAll, 6);

  // A scattered subset with one list turning twice.
  std::string want3[kClauses];
  want3[2] = "x";
  want3[10] = "y";
  want3[19] = "z";
  one(parser, "optionals { k03 x k11 y k20 z r2 p r2 q }", want3, 2);

  // The order is the alternative's, and factoring does not loosen it.
  CHECK(!clean(parser.parse("optionals { k05 a k02 b }")));

  // A repair inside an auxiliary still returns the accepted document's tree.
  for (std::string_view broken :
       {"optionals { k03 }", "optionals { k03 x k11 }",
        "optionals { k03 x k11 y k20 z r2 }", "optionals { k07"})
    walkable(parser, broken);
}

void storey_one(const cursiv::Parser& parser, const std::string& text,
                const std::string (&want)[kStoreyClauses]) {
  const cursiv::ParseResult r = parser.parse(text);
  if (!clean(r)) {
    std::printf("  FAIL storey: `%s` was not taken cleanly\n", text.c_str());
    ++failures;
    return;
  }
  const ast::Storey* o = r.value_as<ast::Storey>();
  CHECK(o != nullptr);
  if (o == nullptr) return;
  for (unsigned i = 0; i < kStoreyClauses; ++i)
    if (o->*kStoreyFields[i] != want[i]) {
      std::printf("  FAIL storey: clause %u holds `%.*s`, wanted `%s`\n", i + 1,
                  static_cast<int>((o->*kStoreyFields[i]).size()),
                  (o->*kStoreyFields[i]).data(), want[i].c_str());
      ++failures;
    }
}

// Optional positions separated by choices holding an end assertion.
void storey(const cursiv::Parser& parser) {
  // Empty, full and scattered subsets preserve each clause's value.
  storey_one(parser, "storey { ; ; ; ; }", {"", "", "", ""});
  storey_one(parser, "storey { k01 a ; k02 b ; k03 c ; k04 d ; }",
             {"a", "b", "c", "d"});
  storey_one(parser, "storey { k01 a ; ; ; ; }", {"a", "", "", ""});
  storey_one(parser, "storey { ; ; ; k04 d ; }", {"", "", "", "d"});
  storey_one(parser, "storey { ; k02 b ; k03 c ; ; }", {"", "b", "c", ""});
  for (const std::string_view broken :
       {"storey { k01", "storey { k01 a", "storey { ; ; }", "storey {"})
    walkable(parser, broken);
}

// A clause opening on the keyword which may follow its own rule.
void entangled(const cursiv::Parser& parser) {
  {
    const cursiv::ParseResult r = parser.parse("entangled rec a rec b");
    CHECK(clean(r));
    CHECK(r.value_as<ast::Recs>() != nullptr);
  }
  {
    const cursiv::ParseResult r =
        parser.parse("entangled rec a k1 x k4 y k8 z rec b");
    CHECK(clean(r));
    const ast::Recs* m = r.value_as<ast::Recs>();
    CHECK(m != nullptr && m->items.size() == 1);
    if (m == nullptr || m->items.empty()) return;
    const ast::Rec& rec = *m->items[0];
    CHECK(rec.name == "a" && rec.a1 == "x" && rec.a4 == "y" && rec.a8 == "z");
    CHECK(rec.nested == "b" && rec.a2.empty() && rec.a7.empty());
  }
}

}  // namespace

void torture_optionals() {
  // The entangled suffix owes every conflict: auxiliaries add none, and the
  // other shapes stay deterministic.
  CHECK(g::kTable.nconflicts <= 22);

  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  optionals(*parser);
  storey(*parser);
  entangled(*parser);

  // The emitter must escape a literal newline in its generated C++ text.
  // It remains a real token here, even though the other cases ignore it.
  const auto newline = parser->parse("newline:sample\n");
  CHECK(clean(newline));
  const auto* doc = newline.value_as<tnl::ast::Doc>();
  CHECK(doc != nullptr && doc->name == "sample");
  CHECK(!clean(parser->parse("newline:sample")));
}
