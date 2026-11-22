// Which rules can start one another before any token is consumed.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cursiv/engine/grammar/StrongComponents.hpp"
#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

template <class S, class Reached>
struct LeadGraph {
  template <class... Rs>
  static auto nodes(TypeList<Rs...>) -> detail::tl_cat_t<detail::cond_t<
      kIsTerminalRule<typename S::template rule<Rs>>, TypeList<>, TypeList<Rs>>...>;
  using Rules = decltype(nodes(typename Reached::type{}));
  template <class R>
  static constexpr std::size_t at = detail::index_of<detail::declared_of_t<R>, Rules>::value;
  static constexpr auto count = detail::tl_size<Rules>::value;

  // A reading yields its nullability and records calls reachable at its opening.
  struct Scan {
    std::array<bool, count> nullable{};
    std::vector<std::uint32_t>* calls = nullptr;
    bool assumeNullable = false;

    template <class R>
    constexpr bool read(detail::Ref<R>) {
      if constexpr (at<R> >= count) return false;
      if (calls) calls->push_back(at<R>);
      return assumeNullable || nullable[at<R>];
    }
    template <class... Es>
    constexpr bool read(detail::Seq<Es...>) { return (read(Es{}) && ...); }
    template <class... Es>
    constexpr bool read(detail::Choice<Es...>) {
      bool empty = false;
      ((empty |= read(Es{})), ...);
      return empty;
    }
    template <class... Es>
    constexpr bool read(detail::Unordered<Es...>) {
      bool empty = true;
      ((empty &= read(Es{})), ...);
      return empty;
    }
    template <std::size_t Min, std::size_t Max, class E>
    constexpr bool read(detail::Repeat<Min, Max, E>) { return read(E{}) || Min == 0; }
    template <class T, class C>
    constexpr bool read(detail::Implied<T, C>) { return true; }
    template <class E>
    constexpr bool read(E) {
      if constexpr (detail::is_condition<E> || std::is_same_v<E, detail::Epsilon>) return true;
      else {
        using Children = typename detail::ExprShape<E>::children;
        if constexpr (detail::tl_size<Children>::value == 1)
          return read(detail::tl_at_t<0, Children>{});
        else
          return false;
      }
    }
  };

  template <class... Rs>
  static consteval auto build(TypeList<Rs...>) {
    constexpr std::array<bool (*)(Scan&), count> read{
        [](Scan& scan) { return scan.read(typename S::template body<Rs>{}); }...};
    Scan scan;
    std::vector<std::uint32_t> calls, work;
    std::vector<std::vector<std::uint32_t>> users(count);
    std::array<bool, count> queued{};
    scan.calls = &calls;
    scan.assumeNullable = true;
    for (std::uint32_t i = 0; i < count; ++i) {
      calls.clear();
      if (!read[i](scan)) continue;
      for (auto r : calls)
        if (users[r].empty() || users[r].back() != i) users[r].push_back(i);
      work.push_back(i);
      queued[i] = true;
    }
    scan.calls = nullptr;
    scan.assumeNullable = false;
    while (!work.empty()) {
      const auto i = work.back();
      work.pop_back();
      queued[i] = false;
      if (!read[i](scan)) continue;
      scan.nullable[i] = true;
      for (auto user : users[i])
        if (!scan.nullable[user] && !queued[user]) {
          queued[user] = true;
          work.push_back(user);
        }
    }
    calls.clear();
    scan.calls = &calls;
    std::array<std::uint32_t, count + 1> starts{};
    for (std::uint32_t i = 0; i < count; ++i) {
      read[i](scan);
      starts[i + 1] = static_cast<std::uint32_t>(calls.size());
    }
    return StrongComponents<count>::build(starts, calls);
  }

  static constexpr auto strong = build(Rules{});
  template <class A>
  static constexpr bool cyclic = at<A> < count && strong.cyclic[at<A>];
};

}  // namespace cursiv::engine
