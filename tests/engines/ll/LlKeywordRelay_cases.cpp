#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexAdapter.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/KeywordRelayGrammar.hpp"

#include "semanticll/generated/Ll/Module.hpp"
#include "semanticll/generated/Ll/Table.hpp"

namespace ll_keyword_relay_cases {

namespace {

// The tape of a table with relays announces them and its qualified acceptance
// in both reads, and the walk refuses one that lost either.
namespace announced {

using Plain = cursiv::LlLexTape<tllsemantic::generated::Ll::kTable>;
using Skipping = cursiv::LlLexTape<tllsemantic::generated::Ll::kTable, true>;

struct LostQualified : Plain {
  cursiv::SymbolId qualified(std::uint16_t, const char*, cursiv::TextOffset,
                             cursiv::TextOffset,
                             cursiv::TextOffset&) const = delete;
};
struct LostRun : Skipping {
  cursiv::TextOffset run(std::uint16_t, const char*, cursiv::TextOffset,
                         cursiv::TextOffset) const = delete;
};

static_assert(Plain::kRelays && Plain::kQualified && Skipping::kQualified);
static_assert(cursiv::LexCursor<Plain> && cursiv::LexCursor<Skipping>);
static_assert(cursiv::LexAcceptanceAnnounced<Plain> &&
              cursiv::LexAcceptanceAnnounced<Skipping>);
static_assert(cursiv::LexRelaysAnnounced<Plain> &&
              cursiv::LexRelaysAnnounced<Skipping>);

static_assert(LostQualified::kQualified &&
              !cursiv::LexAcceptanceAnnounced<LostQualified>);
static_assert(cursiv::LexOffersRelay<LostRun> &&
              !cursiv::LexRelaysAnnounced<LostRun>);

}  // namespace announced

std::string shown(const cursiv::ParseResult& r) {
  std::string out;
  const auto* d = r.value_as<tkwrelay::Doc>();
  if (d == nullptr) return "<none>";
  for (const tkwrelay::Item* i : d->items) {
    out += i->keyword ? std::string("KW") : std::string(i->name);
    if (!i->arg.empty()) out += "(" + std::string(i->arg) + ")";
    out += ' ';
  }
  return out;
}

constexpr std::string_view kWords = "if; x; iff; while; whilex; i;";
constexpr std::string_view kKeywords = "KW x iff KW whilex i ";

struct Pair {
  const char* name;
  const cursiv::Parser* ll;
};

void reads(const Pair& g, const char* engine, const cursiv::Parser& p,
           std::string_view text, std::string_view want) {
  const auto r = p.parse(text);
  const auto got = shown(r);
  if (got != want || !r.diagnostics.empty())
    std::printf("%s %s [%.*s]: want %.*s, got %s, %zu diagnostics\n", g.name,
                engine, static_cast<int>(text.size()), text.data(),
                static_cast<int>(want.size()), want.data(), got.c_str(),
                r.diagnostics.size());
  CHECK(got == want);
  CHECK(r.diagnostics.empty());
}

void both(const Pair& g, std::string_view text, std::string_view want) {
  reads(g, "LL", *g.ll, text, want);
}

void faulty(const Pair& g, std::string_view text) {
  for (const cursiv::Parser* p : {g.ll}) {
    const auto r = p->parse(text);
    if (r.diagnostics.empty())
      std::printf("%s [%.*s]: no diagnostic, got %s\n", g.name,
                  static_cast<int>(text.size()), text.data(), shown(r).c_str());
    CHECK(!r.diagnostics.empty());
  }
}

std::string picked(const cursiv::ParseResult& r) {
  std::string out;
  const auto* d = r.value_as<tkwrelay::Picks>();
  if (d == nullptr) return "<none>";
  for (const tkwrelay::Pick* i : d->items) {
    out += i->first.empty() ? "2:" : "1:";
    out += i->first.empty() ? i->second : i->first;
    out += ' ';
  }
  return out;
}

void arms(const Pair& g, std::string_view text, std::string_view want) {
  for (const auto& [engine, p] :
       {std::pair{"LL", g.ll}}) {
    const auto r = p->parse(text);
    const auto got = picked(r);
    if (got != want || !r.diagnostics.empty())
      std::printf("%s %s [%.*s]: want %.*s, got %s, %zu diagnostics\n", g.name,
                  engine, static_cast<int>(text.size()), text.data(),
                  static_cast<int>(want.size()), want.data(), got.c_str(),
                  r.diagnostics.size());
    CHECK(got == want);
    CHECK(r.diagnostics.empty());
  }
}

}  // namespace

void run(std::unique_ptr<cursiv::Parser> ll,
         std::string_view prefix, bool picks,
         std::initializer_list<std::pair<std::string_view, std::string_view>> cases,
         std::initializer_list<std::string_view> invalid = {}) {
  const Pair pair{"keyword relay", ll.get()};
  for (const auto& [text, want] : cases) {
    const std::string source = "relay:" + std::string(prefix) + std::string(text);
    if (picks) arms(pair, source, want);
    else both(pair, source, want);
  }
  for (const auto text : invalid)
    faulty(pair, "relay:" + std::string(prefix) + std::string(text));
}

void run() {
  namespace g = tllsemantic::generated;
  run(g::Ll::create_parser(), "choice:", false,
      {{kWords, kKeywords}, {"if x; while y;", "if(x) while(y) "},
       {"if(x); while(y);", "if(x) while(y) "}, {"if ;", "if "}});
  run(g::Ll::create_parser(), "rule:", false,
      {{kWords, kKeywords}, {"if ;", "KW "}}, {"if x;"});
  run(g::Ll::create_parser(), "written:", false,
      {{kWords, "KW x iff while whilex i "},
       {"if x; while y;", "if(x) while(y) "}});
  run(g::Ll::create_parser(), "mixedheld:", true,
      {{"if; x; iff; if ; while;", "2:if 1:x 1:iff 1:if 2:while "}});
  run(g::Ll::create_parser(), "narrow:", true,
      {{"if;", "2:if "}, {"if 1;", "2:if "}, {"if1;", "1:if "},
       {"ab;", "1:ab "}, {"ab1;", "1:ab "}});
  run(g::Ll::create_parser(), "sharedtwo:", true,
      {{"if; x; while;", "2:if 1:x 2:while "},
       {"if;x; y;", "1:if 1:x 1:y "}});
  run(g::Ll::create_parser(), "starred:", true,
      {{"*( *(", "1:* 1:* "}});
  run(g::Ll::create_parser(), "called:", true,
      {{"foo( bar(", "1:foo 1:bar "}});
}

}  // namespace ll_keyword_relay_cases
