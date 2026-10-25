// Lists of thousands under a template depth of 100: a fold whose depth grows
// with the list does not compile.
#include "cursiv/core/utils/typelist.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/test/Check.hpp"

namespace {

using cursiv::TypeList;
using cursiv::detail::dedupe_t;
using cursiv::detail::index_of;
using cursiv::detail::tl_at_t;
using cursiv::detail::tl_cat_t;
using cursiv::detail::tl_has;

template <std::size_t I>
using N = std::integral_constant<std::size_t, I>;

template <std::size_t... Is>
[[maybe_unused]] TypeList<N<Is>...> counted(std::index_sequence<Is...>);
template <std::size_t... Is>
[[maybe_unused]] TypeList<N<(Is * 7) % 500>...> scrambled(
    std::index_sequence<Is...>);

template <class S>
struct Singletons;
template <std::size_t... Is>
struct Singletons<std::index_sequence<Is...>> {
  using type = tl_cat_t<TypeList<N<Is>>...>;
};

template <std::size_t Z>
constexpr bool cat_ok() {
  using seq = std::make_index_sequence<Z>;
  return std::is_same_v<typename Singletons<seq>::type,
                        decltype(counted(seq{}))>;
}

// 500 distinct values in a scrambled order, repeated: the first lap is the
// answer.
template <std::size_t Z>
constexpr bool dedupe_ok() {
  constexpr auto lap = Z < 500 ? Z : 500;
  return std::is_same_v<
      dedupe_t<decltype(scrambled(std::make_index_sequence<Z>{}))>,
      decltype(scrambled(std::make_index_sequence<lap>{}))>;
}

template <std::size_t... Zs>
constexpr bool sweep(std::index_sequence<Zs...>) {
  return ((cat_ok<Zs * 149>() && dedupe_ok<Zs * 149>()) && ...);
}

static_assert(sweep(std::make_index_sequence<8>{}));
static_assert(cat_ok<1>() && cat_ok<2>() && cat_ok<16>() && cat_ok<17>() &&
              cat_ok<257>() && cat_ok<4097>());
static_assert(dedupe_ok<1>() && dedupe_ok<63>() && dedupe_ok<64>() &&
              dedupe_ok<65>() && dedupe_ok<129>() && dedupe_ok<2049>());
static_assert(
    std::is_same_v<dedupe_t<TypeList<N<2>, N<1>, N<2>, N<3>, N<1>, N<0>>>,
                   TypeList<N<2>, N<1>, N<3>, N<0>>>);

using wide = decltype(counted(std::make_index_sequence<3000>{}));
static_assert(index_of<N<2999>, wide>::value == 2999);
static_assert(index_of<N<3000>, wide>::value == 3000);
static_assert(std::is_same_v<tl_at_t<2999, wide>, N<2999>>);
// A fold over more than 2048 arguments exceeds clang's expression nesting
// limit.
using narrow = decltype(counted(std::make_index_sequence<2000>{}));
static_assert(tl_has<N<1500>, narrow>::value &&
              !tl_has<N<2000>, narrow>::value);

}  // namespace

int main() { return cursiv::test::report("TypeList_test"); }
