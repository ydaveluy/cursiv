#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/DataTextGrammar.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace ll_data_text_cases {

void run() {
  auto ll = tllsemantic::generated::Ll::create_parser();
  constexpr std::array cases{
      std::pair{"symbol `weird name`;", "weird name"},
      std::pair{"symbol plain;", "plain"},
      std::pair{"forward `weird name`;", "weird name"},
      std::pair{"path `first` . `last`;", "first.last"},
      std::pair{"converted `name`;", "#name"},
      std::pair{"converted plain;", "#plain"},
      std::pair{"literal literal;", "#literal"},
      std::pair{"convertedforward `name`;", "#name"},
      std::pair{"convertedpath `first` . `last`;", "#first.#last"},
      std::pair{"checked !ab.c;", "!ab.c"},
      std::pair{"keyed key.name;", "key.name"},
  };
  for (const auto& [text, expected] : cases) {
    for (const auto* parser : {ll.get()}) {
      tlldatatext::untickCalls = 0;
      const std::string input = "data:" + std::string(text);
      auto parsed = parser->parse(input);
      const std::string_view source{text};
      CHECK(tlldatatext::untickCalls ==
            static_cast<unsigned>(std::count(source.begin(), source.end(), '`') /
                                  2));
      CHECK(parsed.accepted && parsed.diagnostics.empty());
      CHECK(parsed.value != nullptr);
      if (!parsed.value) continue;
      const auto& document = *static_cast<tlldatatext::Document*>(parsed.value);
      CHECK(document.values.size() == 1);
      if (document.values.size() == 1) {
        CHECK(document.values[0] != nullptr);
        if (document.values[0]) {
          const auto actual = document.values[0]->text;
          if (actual != expected)
            std::fprintf(stderr, "%s: %s produced '%.*s', expected '%s'\n",
                         "LL", text,
                         static_cast<int>(actual.size()), actual.data(),
                         expected);
          CHECK(actual == expected);
        }
      }
    }
  }
  // A repaired keyword keeps its source text; a refused conversion leaves
  // the member at its default.
  constexpr std::array repaired{
      std::tuple{"literal litera;", "#litera", cursiv::DiagnosticKind::replaced},
      std::tuple{"keyed ky.name;", "ky.name", cursiv::DiagnosticKind::replaced},
      std::tuple{"checked !az.b;", "", cursiv::DiagnosticKind::conversion_error},
  };
  for (const auto& [text, expected, kind] : repaired) {
    const std::string input = "data:" + std::string(text);
    const auto parsed = ll->parse(input);
    CHECK(parsed.value != nullptr);
    if (!parsed.value) continue;
    const auto& values =
        static_cast<const tlldatatext::Document*>(parsed.value)->values;
    CHECK(values.size() == 1 && values[0] != nullptr);
    if (values.size() == 1 && values[0]) CHECK(values[0]->text == expected);
    CHECK(parsed.diagnostics.size() == 1);
    if (parsed.diagnostics.size() == 1) CHECK(parsed.diagnostics[0].kind == kind);
  }
}

}  // namespace ll_data_text_cases
