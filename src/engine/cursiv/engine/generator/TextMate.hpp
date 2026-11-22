// The grammar's keywords, written into `repository.keywords.match` of an
// editor's TextMate file; the rest of that file is written by hand.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engine/grammar/GrammarWord.hpp"

namespace cursiv::engine::gen {

struct Cfg;
struct Packed;
struct ImageClassDesc;

// The literals the parser reads as tokens. Trivia and the words only a
// condition reads are no keywords.
std::vector<GrammarWord> textmate_keywords(const Cfg& cfg, const Packed& packed);

// Matches keywords containing a Unicode letter as whole words, longest first,
// respecting each keyword's case setting.
std::string textmate_keyword_match(std::vector<GrammarWord> keywords, const ImageClassDesc& bound);

// `json` with its string at `repository.keywords.match` replaced by `match`,
// every other byte unchanged. Nothing where it has no such string or its
// brackets do not match; the rest of the JSON is not checked.
std::optional<std::string> with_keyword_match(std::string_view json,
                                              std::string_view match);

}  // namespace cursiv::engine::gen
