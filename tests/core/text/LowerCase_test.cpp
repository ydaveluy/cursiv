#include <string>

#include "cursiv/core/text/lower_case.hpp"
#include "cursiv/test/Check.hpp"

namespace {

// As a client's `toLowerCase` lowers: each scalar by its full mapping, and a
// capital sigma by whether it closes a word.
void a_text_lowers_as_a_client_lowers_it() {
  CHECK(cursiv::lower_case("MixedCASE_9") == "mixedcase_9");
  CHECK(cursiv::lower_case("ÉCOLE Ŀ") == "école ŀ");
  CHECK(cursiv::lower_case("İ") == "i̇");
  CHECK(cursiv::lower_case("ΟΔΟΣ ΣΑ") == "οδος σα");
  CHECK(cursiv::lower_case("ΑΣ'") == "ας'");
  CHECK(cursiv::lower_case("Σ") == "σ");
  CHECK(cursiv::lower_case("\U00010400") == "\U00010428");
  CHECK(cursiv::lower_case(std::string("A\xFF"
                                       "B")) == std::string("a\xFF"
                                                            "b"));
}

}  // namespace

int main() {
  a_text_lowers_as_a_client_lowers_it();
  return cursiv::test::report("LowerCase_test");
}
