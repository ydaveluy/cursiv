#include "cursiv/engines/shared/gen/gen_report.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "cursiv/engines/shared/gen/DeadBranch.hpp"
#include "cursiv/engines/shared/gen/NestedRunWarning.hpp"
#include "cursiv/engines/shared/gen/SelfBoundedArm.hpp"
#include "cursiv/engines/shared/gen/TwinBranch.hpp"
#include "cursiv/engines/shared/gen/WarningFound.hpp"
#include "cursiv/engines/shared/gen/WordLitWarning.hpp"

namespace cursiv {

namespace {

template <class T, class Text>
void warn_each(std::string_view grammar, const std::vector<T>& found,
               Text text) {
  for (const T& w : found)
    std::fprintf(stderr, "%.*s: warning: %s\n",
                 static_cast<int>(grammar.size()), grammar.data(),
                 text(w).c_str());
}

// A flag records that its operand matched; one that may read nothing always
// sets the member.
struct FlagWarning {
  std::string rule;
  // As the dump spells it.
  std::string flag;

  bool operator==(const FlagWarning&) const = default;
};

std::vector<FlagWarning> flag_warnings(WarningFound& found) {
  return walk_warnings<FlagWarning>(found, [](WarningFound& w, ImageId id) {
    const ImageNode& n = w.image().nodes[id];
    return n.kind == ImageKind::flag &&
           w.reads().nothing(w.image().kids[n.first]);
  });
}

std::string flag_warning_text(const FlagWarning& w) {
  return "rule `" + w.rule + "`: `" + w.flag +
         "` flags an operand that may match nothing, so it always sets the "
         "member -- flag a token that must be there, and make the flag "
         "optional (`option(flag<&T::m>(kw<\"on\">))`)";
}

}  // namespace

void report_grammar_warnings(std::string_view grammar, GrammarImage& image) {
  WarningFound found(image);
  warn_each(grammar, dead_branches(found), dead_branch_text);
  warn_each(grammar, twin_branches(found), twin_branch_text);
  warn_each(grammar, flag_warnings(found), flag_warning_text);
  warn_each(grammar, nested_run_warnings(found), nested_run_warning_text);
  warn_each(grammar, word_lit_warnings(found), word_lit_warning_text);
}

bool refuse_self_bounded_arms(std::string_view grammar, GrammarImage& image) {
  const auto found = self_bounded_arms(image);
  for (const SelfBoundedArm& a : found)
    std::fprintf(stderr, "%.*s: refused (selfBoundedArm)\ncursiv: %s\n",
                 static_cast<int>(grammar.size()), grammar.data(),
                 self_bounded_arm_text(a).c_str());
  return !found.empty();
}

bool refuse_empty_match(std::string_view grammar, std::string_view terminal) {
  if (terminal.empty()) return false;
  const std::string_view text = lower_fail_text(LowerFail::terminalEmpty);
  std::fprintf(stderr, "%.*s: refused: terminal `%.*s` %.*s\n",
               static_cast<int>(grammar.size()), grammar.data(),
               static_cast<int>(terminal.size()), terminal.data(),
               static_cast<int>(text.size()), text.data());
  return true;
}

}  // namespace cursiv
