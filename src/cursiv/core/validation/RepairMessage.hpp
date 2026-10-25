#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// What a repair says, from the three facts it carries: the grammar position it
// was owed, what the recovery did, and the text it did it over. No second
// parse, and no service: the recovery corpus renders the same sentence an
// editor gets, so a wording that drifts is a line that moved in a reference.

namespace cursiv {

struct Grammar;
struct ParseDiagnostic;

// `over` is the text the repair spans; `atEnd` says the span reaches the last
// byte of the document, which is the one thing the span itself cannot tell.
[[nodiscard]] std::string repair_message(const Grammar& grammar,
                                         const ParseDiagnostic& diagnostic,
                                         std::string_view over, bool atEnd);

// The spellings of what the repair was owed, the first named first, at most
// `limit` of them: what a message quotes and what a quick fix writes.
[[nodiscard]] std::vector<std::string_view> expected_spellings(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::size_t limit);

}  // namespace cursiv
