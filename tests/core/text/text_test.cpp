// The class syntax of `cr<>`, a std::regex bracket expression without its
// brackets read among scalars, and a text lowered as a client lowers it.
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"
#include "cursiv/core/text/ClassItem.hpp"
#include "cursiv/core/text/lower_case.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/test/Check.hpp"

using namespace cursiv;

namespace {

template <std::uint64_t M, bool N, str Sp>
constexpr std::string_view spelled(CpSet<M, N, Sp>) {
  return Sp.view();
}
template <str S>
constexpr std::string_view key = spelled(cr<S>);
template <str S>
using CR = std::remove_cvref_t<decltype(cr<S>)>;

// The escapes.
static_assert(key<R"(\x41)"> == "A");
static_assert(key<R"(\u0041)"> == "A");
static_assert(key<R"(\u{41})"> == "A" && key<R"(\u{0000041})"> == "A");
static_assert(key<R"(\n)"> == "\\x0a");
static_assert(key<R"(\r)"> == "\\x0d");
static_assert(key<R"(\t)"> == "\\x09");
static_assert(key<R"(\f)"> == "\\x0c");
static_assert(key<R"(\v)"> == "\\x0b");
static_assert(key<R"(\b)"> == "\\x08");
static_assert(key<R"(\0)"> == "\\x00");
static_assert(key<R"(\cJ)"> == "\\x0a" && key<R"(\cj)"> == "\\x0a");
static_assert(key<R"(\d)"> == "0-9");
static_assert(key<R"(\w)"> == "0-9A-Z_a-z");
static_assert(key<R"(\s)"> == "\\x09-\\x0d ");
static_assert(std::is_same_v<CR<R"(\D)">, CpSet<0, true, "0-9">>);
static_assert(std::is_same_v<CR<R"(\W)">, CpSet<0, true, "0-9A-Z_a-z">>);
static_assert(std::is_same_v<CR<R"(\S)">, CpSet<0, true, "\\x09-\\x0d ">>);
static_assert(std::is_same_v<CR<R"(a\D)">, CR<R"(\D)">>);
static_assert(std::is_same_v<CR<R"(^\D)">, CR<"0-9">>);
static_assert(key<R"(\\)"> == "\\x5c");
static_assert(key<R"(\-)"> == "\\x2d");
static_assert(key<R"(\^)"> == "\\x5e");
static_assert(key<R"(\])"> == "]");
static_assert(key<R"(\[)"> == "[");
static_assert(key<R"(\")"> == "\"");
static_assert(key<R"(\')"> == "'");
static_assert(key<R"(\/)"> == "/");
static_assert(key<R"(\.)"> == ".");
static_assert(key<R"(\x80-\xff)"> == "\\x80-\\xff");
static_assert(std::is_same_v<CR<R"(\d)">, CR<"0-9">>);
static_assert(std::is_same_v<CR<R"(\w)">, std::remove_cvref_t<decltype(w)>>);
static_assert(std::is_same_v<CR<R"(\s)">, std::remove_cvref_t<decltype(s)>>);

// A `-` is a range operator between two atoms, itself anywhere else.
static_assert(key<"a-c"> == "a-c");
static_assert(key<"-a"> == "\\x2da");
static_assert(key<"a-"> == "\\x2da");
static_assert(std::is_same_v<CR<"^-a">, CpSet<0, true, "\\x2da">>);
static_assert(key<"a-c-x"> == "\\x2da-cx");
static_assert(key<"a-c--0"> == "\\x2d-0a-c");
static_assert(key<R"(a\-c)"> == "\\x2dac");
static_assert(key<R"(\d-)"> == "\\x2d0-9");
static_assert(key<R"(\x2d-\x2f)"> == "\\x2d-/");
static_assert(key<R"(\n-\r)"> == "\\x0a-\\x0d");
static_assert(key<R"(\u0030-\u0039)"> == "0-9");

// A leading `^` negates; a lone one, or one further on, is itself.
static_assert(key<"^"> == "\\x5e");
static_assert(key<"a^"> == "\\x5ea");
static_assert(std::is_same_v<CR<"^^">, CpSet<0, true, "\\x5e">>);
static_assert(std::is_same_v<CR<R"(^\n)">, CpSet<0, true, "\\x0a">>);

// `\x` is U+00HH, whichever way a scalar is spelled.
static_assert(std::is_same_v<CR<R"(\u00e9)">, CpSet<0, false, "\\xe9">>);
static_assert(std::is_same_v<CR<"é">, CR<R"(\xe9)">>);
static_assert(std::is_same_v<CR<R"(^\u00e9)">, CpSet<0, true, "\\xe9">>);
static_assert(std::is_same_v<decltype(~cr<"é">), CR<"^é">>);
static_assert(std::is_same_v<decltype(~~cr<"a">), CR<"a">>);
static_assert(std::is_same_v<decltype(~~cr<R"(\D)">), CR<R"(\D)">>);

// Joining letters to a complement removes them from the excluded scalars.
static_assert(std::is_same_v<decltype(~cr<"a"> | uc::L), AnyChar>);
static_assert(std::is_same_v<decltype(uc::L | ~cr<"a">), AnyChar>);
static_assert(std::is_same_v<decltype(~cr<"a!"> | uc::L), CpSet<0, true, "!">>);
static_assert(std::is_same_v<decltype(uc::L | ~cr<"a!">), CpSet<0, true, "!">>);

constexpr bool difference_is(std::string_view a, std::string_view b,
                             std::initializer_list<unicode::Range> want) {
  const auto actual = ClassRanges::of(a).minus(ClassRanges::of(b));
  return std::equal(actual.r.begin(), actual.r.end(), want.begin(), want.end());
}

static_assert(difference_is("", "", {}));
static_assert(difference_is("", "a-z", {}));
static_assert(difference_is("a-z", "", {{U'a', U'z'}}));
static_assert(difference_is("a-f", "m-z", {{U'a', U'f'}}));
static_assert(difference_is("a-f", "a-c", {{U'd', U'f'}}));
static_assert(difference_is("a-f", "d-z", {{U'a', U'c'}}));
static_assert(difference_is("a-z", "d-f", {{U'a', U'c'}, {U'g', U'z'}}));
static_assert(difference_is("a-z", "a-z", {}));
static_assert(difference_is("a-cm-px-z", "b-y", {{U'a', U'a'}, {U'z', U'z'}}));
static_assert(difference_is("a-cm-px-z", "b-n",
                            {{U'a', U'a'}, {U'o', U'p'}, {U'x', U'z'}}));
static_assert(difference_is(R"(\u{0}-\u{10ffff})", R"(\u{0}\u{10ffff})",
                            {{1, 0x10fffe}}));
static_assert(difference_is(R"(\u{0}-\u{10ffff})", R"(\u{10ffff})",
                            {{0, 0x10fffe}}));
static_assert(difference_is(R"(\u{0}-\u{10ffff})", R"(\u{0})",
                            {{1, 0x10ffff}}));
static_assert(difference_is(R"(\u{0}\u{10ffff})", R"(\u{0}-\u{10ffff})", {}));

constexpr bool union_is(std::string_view a, std::string_view b,
                        std::initializer_list<unicode::Range> want) {
  const auto actual = ClassRanges::of(a).united(ClassRanges::of(b));
  return std::equal(actual.r.begin(), actual.r.end(), want.begin(), want.end());
}

static_assert(union_is("", "", {}));
static_assert(union_is("a-c", "", {{U'a', U'c'}}));
static_assert(union_is("", "a-c", {{U'a', U'c'}}));
static_assert(union_is("a-c", "d-f", {{U'a', U'f'}}));
static_assert(union_is("m", "a-z", {{U'a', U'z'}}));
static_assert(union_is("a-cx-z", "b-dm",
                       {{U'a', U'd'}, {U'm', U'm'}, {U'x', U'z'}}));
// A spelling read out of order, or with ranges that meet, still gives its
// ranges ascending and joined.
static_assert(union_is("ba", "", {{U'a', U'b'}}));
static_assert(union_is("zab-c", "", {{U'a', U'c'}, {U'z', U'z'}}));
static_assert(union_is("x-zc-fa-d", "", {{U'a', U'f'}, {U'x', U'z'}}));
static_assert(union_is(R"(\u{10ffff}\u{0}\x01)", "",
                       {{0, 1}, {0x10ffff, 0x10ffff}}));

template <str S>
consteval bool holds(char32_t cp) {
  return detail::word_class<CR<S>>::has(cp);
}
static_assert(holds<R"(\u00e8-\u00ea)">(0xE9) &&
              !holds<R"(\u00e8-\u00ea)">(0xEB));
static_assert(holds<R"(\u{e8}-\u{ea})">(0xE9) &&
              !holds<R"(\u{e8}-\u{ea})">(0xEB));
static_assert(holds<R"(\u{1F600})">(0x1F600) &&
              holds<R"(\u{10FFFF})">(0x10FFFF));
static_assert(holds<R"(é\x41)">(U'A') && holds<R"(é\xff)">(0xFF) &&
              !holds<R"(é\xff)">(0x7FF));
static_assert(holds<R"(é\s)">(U' ') && holds<R"(é\s)">(U'\v') &&
              !holds<R"(é\s)">(0xA0) && !holds<R"(é\s)">(0x2028) &&
              !holds<R"(é\s)">(0x3000) && !holds<R"(é\s)">(0xFEFF) &&
              !holds<R"(é\s)">(U'a'));
static_assert(holds<R"(é\S)">(U'a') && holds<R"(é\S)">(0x10FFFF) &&
              holds<R"(é\S)">(0x3000) && !holds<R"(é\S)">(U' '));
static_assert(holds<R"(é\d)">(U'5') && !holds<R"(é\d)">(0x0660));
static_assert(holds<R"(é\W)">(0x4E00) && !holds<R"(é\W)">(U'_'));
static_assert(holds<R"(é\-)">(U'-') && holds<R"(é-)">(U'-'));
static_assert(holds<R"(^é)">(U'a') && !holds<R"(^é)">(0xE9));
static_assert(holds<R"(\é)">(0xE9));

using Space = std::remove_cvref_t<decltype(uc::White_Space)>;
static_assert(detail::word_class<Space>::has(U'\t') &&
              detail::word_class<Space>::has(U'\r') &&
              detail::word_class<Space>::has(U' ') &&
              detail::word_class<Space>::has(0x85) &&
              detail::word_class<Space>::has(0xA0) &&
              detail::word_class<Space>::has(0x1680) &&
              detail::word_class<Space>::has(0x2000) &&
              detail::word_class<Space>::has(0x200A) &&
              detail::word_class<Space>::has(0x2028) &&
              detail::word_class<Space>::has(0x2029) &&
              detail::word_class<Space>::has(0x202F) &&
              detail::word_class<Space>::has(0x205F) &&
              detail::word_class<Space>::has(0x3000));
static_assert(!detail::word_class<Space>::has(0x08) &&
              !detail::word_class<Space>::has(0x0E) &&
              !detail::word_class<Space>::has(0x1C) &&
              !detail::word_class<Space>::has(0x1F) &&
              !detail::word_class<Space>::has(0x21) &&
              !detail::word_class<Space>::has(0x180E) &&
              !detail::word_class<Space>::has(0x200B) &&
              !detail::word_class<Space>::has(0x2060) &&
              !detail::word_class<Space>::has(0x3001) &&
              !detail::word_class<Space>::has(0xFEFF));

using Other = std::remove_cvref_t<decltype(uc::C)>;
static_assert(detail::word_class<Other>::has(0x0378));
static_assert(detail::word_class<Other>::has(0x10FFFF));
static_assert(detail::word_class<Other>::has(0x0000));
static_assert(detail::word_class<Other>::has(0xE000));
static_assert(!detail::word_class<Other>::has(U'A'));
static_assert(!detail::word_class<Other>::has(0xD800));

// What the compile-time refusals of `cr<>` stand on.
constexpr ClassFault fault(std::string_view spec) {
  return for_class_ranges(spec, [](char32_t, char32_t) {});
}
static_assert(fault(R"(a\)") == ClassFault::lone_backslash);
static_assert(fault(R"(\q)") == ClassFault::unknown_escape);
static_assert(fault(R"(\a)") == ClassFault::unknown_escape);
static_assert(fault(R"(\1)") == ClassFault::unknown_escape);
static_assert(fault(R"(\01)") == ClassFault::unknown_escape);
static_assert(fault(R"(\x4)") == ClassFault::unknown_escape);
static_assert(fault(R"(\x4g)") == ClassFault::unknown_escape);
static_assert(fault(R"(\u004)") == ClassFault::unknown_escape);
static_assert(fault(R"(\u{})") == ClassFault::unknown_escape);
static_assert(fault(R"(\u{41)") == ClassFault::unknown_escape);
static_assert(fault(R"(\u{4g})") == ClassFault::unknown_escape);
static_assert(fault(R"(\c1)") == ClassFault::unknown_escape);
static_assert(fault(R"(a-\d)") == ClassFault::class_endpoint);
static_assert(fault(R"(\w-z)") == ClassFault::class_endpoint);
static_assert(fault("z-a") == ClassFault::reversed_range);
static_assert(fault(R"(\u00e9)") == ClassFault::none);
static_assert(fault(R"(\u{e9})") == ClassFault::none);
static_assert(fault(R"(a-\u{e9})") == ClassFault::none);
static_assert(fault("é") == ClassFault::none);
static_assert(fault("a-é") == ClassFault::none);
static_assert(fault(R"(\u{41}\x80-\xff)") == ClassFault::none);
static_assert(fault(R"(\u{80}-\u{10FFFF})") == ClassFault::none);
static_assert(fault(R"(\u{0}-\u{10FFFF})") == ClassFault::none);
static_assert(fault(R"(\u{D800})") == ClassFault::not_scalar);
static_assert(fault(R"(\uDFFF)") == ClassFault::not_scalar);
static_assert(fault(R"(\u{110000})") == ClassFault::not_scalar);
static_assert(fault(R"(\u{FFFFFFFFFF})") == ClassFault::not_scalar);
static_assert(fault(R"(\u{10FFFF}\u{000000041})") == ClassFault::none);
static_assert(fault("\xff") == ClassFault::malformed);
static_assert(fault("\xc3") == ClassFault::malformed);
static_assert(fault(R"(\d\-x-)") == ClassFault::none);

bool same_ranges(const LexCodePoints& c,
          std::initializer_list<unicode::Range> want) {
  if (c.ranges.size() != want.size()) return false;
  std::size_t i = 0;
  for (const unicode::Range& r : want) {
    if (c.ranges[i].lo != r.lo || c.ranges[i].hi != r.hi) return false;
    ++i;
  }
  return true;
}

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
  CHECK(same_ranges(LexCodePoints::of(0, false, R"(\s)"),
                    {{0x09, 0x0D}, {0x20, 0x20}}));
  CHECK(same_ranges(LexCodePoints::of(0, false, R"(\S)"),
                    {{0, 0x08}, {0x0E, 0x1F}, {0x21, 0x10FFFF}}));
  CHECK(same_ranges(LexCodePoints::of(0, false, R"(\D)"),
             {{0, 0x2F}, {0x3A, 0x10FFFF}}));
  CHECK(same_ranges(LexCodePoints::of(0, false, R"(\u00e9-\u00ff\cA)"),
             {{0x01, 0x01}, {0xE9, 0xFF}}));
  CHECK(same_ranges(LexCodePoints::of(0, false, R"(\u{e9}-\u{ff}\x41)"),
                    {{0x41, 0x41}, {0xE9, 0xFF}}));
  CHECK(same_ranges(LexCodePoints::of(0, true, R"(\w)"),
                    {{0, 0x2F},
                     {0x3A, 0x40},
                     {0x5B, 0x5E},
                     {0x60, 0x60},
                     {0x7B, 0x10FFFF},
                     {LexCodePoints::kStray, LexCodePoints::kStray}}));
  CHECK(!LexCodePoints::of(0, false, R"(\w)").strays());
  CHECK(LexCodePoints::of(0, true, R"(\w)").strays());
  CHECK(!LexCodePoints::of(0, true, R"(\u{0}-\u{10FFFF})").ranges.empty());
  CHECK(same_ranges(
      LexCodePoints::of(detail::cp_bit(unicode::Property::White_Space), false,
                        ""),
      {{0x09, 0x0D},
       {0x20, 0x20},
       {0x85, 0x85},
       {0xA0, 0xA0},
       {0x1680, 0x1680},
       {0x2000, 0x200A},
       {0x2028, 0x2029},
       {0x202F, 0x202F},
       {0x205F, 0x205F},
       {0x3000, 0x3000}}));
  for (const char* refused_spec :
       {R"(é\d-z)", R"(\u{D800})", R"(\u{110000})"}) {
    bool refused = false;
    try {
      (void)LexCodePoints::of(0, false, refused_spec);
    } catch (const std::invalid_argument&) {
      refused = true;
    }
    CHECK(refused);
  }
  a_text_lowers_as_a_client_lowers_it();
  return test::report("text_test");
}
