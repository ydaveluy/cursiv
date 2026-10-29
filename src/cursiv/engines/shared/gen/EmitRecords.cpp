#include "cursiv/engines/shared/gen/EmitRecords.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>

#include "cursiv/engines/shared/gen/CLiteral.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"

namespace cursiv {

RecordGen record_gen(std::span<const AstRowGen> rows,
                     const std::vector<std::uint16_t>& held,
                     const std::vector<std::uint16_t>& over) {
  RecordGen rec;
  bool named = false, converted = false;
  const auto read = [&](const auto& run, auto& features, bool own) {
    for (const std::uint16_t r : run) {
      const auto& row = rows[r];
      if (row.writes() || row.kind == WriteKind::flag) {
        features.push_back(row.feature);
        if (!named) {
          rec.feature = row.feature;
          rec.typeSlot = row.declType;
          rec.narrow = row.narrowSlot;
          named = true;
        }
      }
      rec.made |= own && row.makes();
      if (!converted && row.converts() && !row.rule.empty()) {
        rec.rule = row.rule;
        converted = true;
      }
    }
  };
  read(held, rec.held, true);
  read(over, rec.over, false);
  return rec;
}

namespace {

// Sized: a NUL in a terminal's text is one of its bytes.
std::string record_literal(std::string_view s) {
  return std::format("::std::string_view{{{}, {}}}", c_literal(s), s.size());
}
std::string record_text(std::string_view s) {
  return s.empty() ? std::string("{}") : record_literal(s);
}

}  // namespace

std::string emit_records(std::string_view ns,
                         const std::vector<TerminalGen>& terminals,
                         const std::vector<RecordGen>& records,
                         const AstRegistry& registry) {
  // One function per distinct set of members. The comparison is a set: the
  // same members, in any order and however often listed, give one function.
  std::map<std::string, std::string> bodies;
  std::string funs;
  const auto ask = [&](std::vector<std::string_view> set,
                       const std::string& prefix) {
    std::ranges::sort(set);
    set.erase(std::ranges::unique(set).begin(), set.end());
    const std::string body =
        set.empty()
            ? std::string("bool @(::std::string_view) { return false; }")
            : "bool @(::std::string_view f) {\n  return " +
                  join(set, " || ",
                       [](std::string_view s) {
                         return "f == " + record_literal(s);
                       }) +
                  ";\n}";
    const auto [found, fresh] =
        bodies.try_emplace(body, prefix + std::to_string(bodies.size()));
    if (fresh) {
      std::string text = body;
      text.replace(text.find('@'), 1, found->second);
      funs += text + "\n";
    }
    return found->second;
  };
  // One pool of the names answers give, the empty one first.
  std::map<std::string_view, std::size_t> nameIds{{std::string_view{}, 0}};
  std::vector<std::string_view> names{std::string_view{}};
  const auto name_of = [&](std::string_view text) {
    const auto [at, fresh] = nameIds.try_emplace(text, names.size());
    if (fresh) names.push_back(text);
    return at->second;
  };
  // A record names its member's class by its index in the AST tables.
  const auto type_of = [&](const RecordGen& rec) -> std::string {
    if (rec.typeSlot == detail::kNoIndex) return "::cursiv::detail::kNoIndex";
    if (rec.typeSlot >= registry.types.size())
      throw std::logic_error(std::format(
          "a record writes class {}, past the {} of the AST registry",
          rec.typeSlot, registry.types.size()));
    return std::format("{}u", rec.typeSlot);
  };

  // One answer per distinct row, in the order first met.
  std::map<std::string, std::size_t> answerIds;
  std::vector<std::string> answers;
  std::vector<std::size_t> recordAnswers;
  for (const RecordGen& rec : records) {
    std::vector<std::string_view> writes = rec.made ? rec.over : rec.held;
    if (!rec.made)
      writes.insert(writes.end(), rec.over.begin(), rec.over.end());
    const std::string w = ask(std::move(writes), "w");
    const std::string f =
        ask(rec.made ? rec.held : std::vector<std::string_view>{}, "l");
    const std::size_t g = name_of(rec.feature);
    const std::size_t r = name_of(rec.rule);
    const std::string answer =
        std::format("{{&{}, &{}, {}u, {}u, {}}}", w, f, g, r, type_of(rec));
    const auto [at, fresh] = answerIds.try_emplace(answer, answers.size());
    if (fresh) answers.push_back(answer);
    recordAnswers.push_back(at->second);
  }
  if (answers.size() > 0xFFFFu)
    throw std::runtime_error(
        "this grammar's records answer in more distinct ways than a record's "
        "answer index can name");
  if (names.size() > 0xFFFFu)
    throw std::runtime_error(
        "this grammar's records give more distinct names than an answer's "
        "name index can name");

  std::string out = std::format(R"(// generated by cursiv gen -- do not edit
#include "Grammar.hpp"

#include <cstdint>
#include <string_view>

#include "AstReflection.hpp"
#include "cursiv/core/grammar/GrammarMetaData.hpp"

namespace {0} {{
namespace grammar_detail {{

)",
                                ns);
  out += funs;
  out += '\n';
  out += emit_table("constexpr ::cursiv::GrammarMetaData::Answer kAnswers[]",
                    answers, std::identity{}, "{}");
  out += emit_table(
      "constexpr ::std::uint16_t kRecordAnswers[]", recordAnswers,
      [](std::size_t a) { return std::format("{}u", a); }, "0u");
  out +=
      emit_table("constexpr ::std::string_view kNames[]", names, record_text);

  // A null table reads `kNoNarrow` everywhere.
  const bool anyNarrow = std::ranges::any_of(
      records, [](const RecordGen& rec) { return rec.narrow != kNoNarrow; });
  if (anyNarrow)
    out += emit_table(
        "constexpr ::std::uint8_t kNarrows[]", records,
        [](const RecordGen& rec) { return std::format("{}", rec.narrow); });

  // The words of every terminal in one pool, a list the pool already holds in
  // a row read there again: a grammar that spells none writes no pool, and
  // its rows no spellings.
  std::vector<std::string_view> spellings;
  std::vector<std::size_t> spellingAt;
  for (const TerminalGen& t : terminals) {
    const auto run = std::ranges::search(spellings, t.spellings);
    spellingAt.push_back(
        static_cast<std::size_t>(run.begin() - spellings.begin()));
    if (run.empty())
      spellings.insert(spellings.end(), t.spellings.begin(), t.spellings.end());
  }
  if (!spellings.empty())
    out += emit_table("constexpr ::std::string_view kSpellings[]", spellings,
                      record_literal);

  std::size_t row = 0;
  out += emit_table(
      "constexpr ::cursiv::GrammarTerminal kTerminals[]", terminals,
      [&](const TerminalGen& t) {
        std::string cell = std::format("{{{}, {}, {}", record_text(t.name),
                                       record_text(t.doc), t.folded);
        if (!t.spellings.empty())
          cell += std::format(", {{kSpellings + {}, {}u}}", spellingAt[row],
                              t.spellings.size());
        ++row;
        return cell + '}';
      },
      "{}");
  out += std::format(R"(
}}  // namespace grammar_detail

constexpr ::cursiv::GrammarMetaData kGrammarMetaData{{
    .answers = grammar_detail::kAnswers,
    .recordAnswers = grammar_detail::kRecordAnswers,
    .names = grammar_detail::kNames,
    .ast = &kAstMetaData,
)");
  if (anyNarrow) out += "    .narrows = grammar_detail::kNarrows,\n";
  out += std::format(R"(    .recordCount = {0}u,
    .terminals = grammar_detail::kTerminals,
    .terminalCount = {1}u,
}};

}}  // namespace {2}
)",
                     records.size(), terminals.size(), ns);
  return out;
}

std::string emit_records_decl(std::string_view ns) {
  return std::format(R"(// generated by cursiv gen -- do not edit
#pragma once

#include "cursiv/core/grammar/GrammarMetaData.hpp"

namespace {} {{
extern const ::cursiv::GrammarMetaData kGrammarMetaData;
}}
)",
                     ns);
}

}  // namespace cursiv
