#include <cstdio>

#include "cursiv/ll/gen/LlLowering.hpp"
#include "cursiv/ll/gen/ll_build.hpp"
#include "cursiv/lr/gen/LrLowering.hpp"
#include "cursiv/lr/gen/lr_build.hpp"
#include "cursiv/lr/table/LrReducePlan.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/RegionBoundaryGrammar.hpp"

namespace {
static_assert(sizeof(cursiv::LrReducePlan) == 12);

using tregionboundary::Document;

// Existing refusal contracts. DifferentArms reaches the same X under two
// trivia settings; the guarded nullable/counted tails document analysis
// limits, rather than a demonstrated ambiguity of the language. The direct
// nullable tail is instead covered by the positive Optional runtime cases.
// GuardedRun and ImpliedTail are the shapes only one engine refuses.
struct RefusalBase : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  using ignored = WS;
};
struct DifferentArms : RefusalBase {
  struct Root : rule<create<Document>(
      tight(lit<"["> + ref<X> + lit<"a">) |
      (lit<"["> + ref<X> + lit<"b">))> {};
  using entry = Root;
};
struct GuardedNullableTail : RefusalBase {
  struct Tail : rule<option(ref<X>) + !lit<"z">> {};
  struct Root : rule<create<Document>(
      tight(lit<"["> + ref<Tail>) + lit<"]">)> {};
  using entry = Root;
};
struct CountedEnd : RefusalBase {
  struct Root : rule<create<Document>(
      tight(lit<"["> + repeat<2, 4>(ref<X>)) + eof)> {};
  using entry = Root;
};
struct GuardedRun : RefusalBase {
  struct Root : rule<create<Document>(
      tight(many(lit<","> + ref<X>) + !lit<",">) + lit<";">)> {};
  using entry = Root;
};
struct ImpliedTail : RefusalBase {
  struct Root : rule<create<Document>(
      lit<"a"> + tight(ref<X> + implied(lit<";">, gap::line | eof)))> {};
  using entry = Root;
};

template <class Entry>
struct Grammar : tregionboundary::Base {
  using entry = Entry;
};

// An exit peek must not swallow a region's item as caller-only trivia.
struct MaskedNullableTail : cursiv::GrammarSpec {
  struct XS : terminal<some(lit<"x">)> {};
  struct Root : rule<tight(lit<"["> + option(lit<"x">)) + lit<"]">> {};
  using entry = Root;
  using ignored = XS;
};

// Caller trivia that consumes beyond an item must not hide that item either.
struct LongerMaskedTail : cursiv::GrammarSpec {
  struct XX : terminal<lit<"xx">> {};
  struct Root : rule<tight(lit<"["> + option(lit<"x">)) + lit<"]">> {};
  using entry = Root;
  using ignored = XX;
};

// The same next token can belong to the optional tail or to its caller.
struct SharedNullableTail : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<tight(lit<"["> + option(lit<"x">)) + lit<"x">> {};
  using entry = Root;
  using ignored = WS;
};

// A fresh nonempty arm does not remove the guarded empty exit of another arm.
struct MixedGuardedTail : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Tail : rule<(option(lit<"x">) + !lit<"z">) | lit<"y">> {};
  struct Root : rule<lit<"p"> + tight(lit<"a"> + ref<Tail>) + lit<"c">> {};
  using entry = Root;
  using ignored = WS;
};

template <class G>
void refuses_shared_exit(bool byLl = true, bool byLr = true) {
  const auto ll = cursiv::ll_build_program(cursiv::LlLowering<G>::lower());
  const auto lr = cursiv::lr_build_automaton(cursiv::LrLowering<G>::lower());
  const auto why = [](bool refused) {
    return refused ? cursiv::LowerFail::regionBoundaryShared
                   : cursiv::LowerFail::none;
  };
  CHECK(ll.cfg.lowerWhy == why(byLl));
  CHECK(lr.cfg.lowerWhy == why(byLr));
}

// Construct each mode independently, without requiring a generated parser.
// A refusal in one mode cannot prevent the others from being diagnosed.
template <class Entry>
void each_mode(const char* name) {
  using G = Grammar<Entry>;
  const auto ll = cursiv::LlLowering<G>::lower();
  const auto lr = cursiv::LrLowering<G>::lower();
  std::printf("region analysis %s: LL lower=%u fault=%u; LR lower=%u fault=%u\n",
              name, unsigned(ll.cfg.lowerWhy), unsigned(ll.fault.code),
              unsigned(lr.cfg.lowerWhy), unsigned(lr.fault.code));
  CHECK(!ll.fault && !ll.cfg.lowerFail());
  CHECK(!lr.fault && !lr.cfg.lowerFail());
  if (!ll.fault && !ll.cfg.lowerFail()) {
    const auto built = cursiv::ll_build_program(ll);
    std::printf("region analysis %s: LL build=%u fault=%u\n", name,
                unsigned(built.cfg.lowerWhy), unsigned(built.fault.code));
    CHECK(!built.cfg.lowerFail() && !built.fault);
  }
  if (!lr.fault && !lr.cfg.lowerFail()) {
    const auto built = cursiv::lr_build_automaton(lr);
    std::printf("region analysis %s: LR build=%u\n", name,
                unsigned(built.cfg.lowerWhy));
    CHECK(!built.cfg.lowerFail());
  }
}
}  // namespace

int main() {
  using G = tregionboundary::Base;
  each_mode<G::Required>("required");
  each_mode<G::Nonempty>("some");
  each_mode<G::AtLeastTwo>("at-least-two");
  each_mode<G::Optional>("optional");
  each_mode<G::CalledOptional>("called-optional");
  each_mode<G::CalledEntry>("called-entry");
  each_mode<G::Fixed>("fixed");
  each_mode<G::ClosingInside>("closing-inside");
  each_mode<G::Called>("called");
  each_mode<G::Same>("same-config");
  each_mode<G::SameHead>("same-head");
  each_mode<G::PatternHead>("pattern-head");
  each_mode<G::ShortItem>("short-item");
  each_mode<G::LongItem>("long-item");
  refuses_shared_exit<MaskedNullableTail>();
  refuses_shared_exit<LongerMaskedTail>();
  refuses_shared_exit<SharedNullableTail>();
  refuses_shared_exit<MixedGuardedTail>();
  refuses_shared_exit<DifferentArms>(false, true);
  refuses_shared_exit<GuardedRun>(false, true);
  refuses_shared_exit<ImpliedTail>(true, false);
  refuses_shared_exit<GuardedNullableTail>();
  refuses_shared_exit<CountedEnd>();
  return cursiv::test::report("RegionBoundaryAnalysis_test");
}
