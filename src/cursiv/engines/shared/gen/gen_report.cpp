#include "cursiv/engines/shared/gen/gen_report.hpp"

#include <cstdio>
#include <vector>

#include "cursiv/engines/shared/gen/DeadBranch.hpp"
#include "cursiv/engines/shared/gen/FlagWarning.hpp"
#include "cursiv/engines/shared/gen/NestedRunWarning.hpp"
#include "cursiv/engines/shared/gen/SelfBoundedArm.hpp"

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

}  // namespace

void report_grammar_warnings(std::string_view grammar, GrammarImage& image) {
  warn_each(grammar, dead_branches(image), dead_branch_text);
  warn_each(grammar, flag_warnings(image), flag_warning_text);
  warn_each(grammar, nested_run_warnings(image), nested_run_warning_text);
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
  std::fprintf(stderr,
               "%.*s: refused, terminal `%.*s` matches the empty string -- "
               "give its body a part it must read\n",
               static_cast<int>(grammar.size()), grammar.data(),
               static_cast<int>(terminal.size()), terminal.data());
  return true;
}

}  // namespace cursiv
