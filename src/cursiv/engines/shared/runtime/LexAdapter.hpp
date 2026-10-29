// What `lex_walk` asks of the adapter an engine names its table through. Two
// capabilities choose the algorithm, so an adapter announces them and the
// walk holds it to the announcement when it starts:
//   `kQualified`  a state's election is answered by `qualified`; where it is
//                 not announced the walk reads the boundary fallback;
//   `kRelays`     the table echoes marks, and the adapter offers `relay` with
//                 the rest of the relay protocol.
// The walk picks its branch by what the adapter has. Without the announcement
// a method lost by accident would compile and select the other branch.
// A small adapter that announces nothing is read as it offers.
#pragma once

#include <concepts>
#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The DFA cursor, whatever a match retains.
template <class Ad>
concept LexCursor = requires(const Ad& ad, std::uint16_t s, unsigned char b) {
  { ad.start(s) } -> std::convertible_to<std::uint16_t>;
  { ad.step(s, b) } -> std::convertible_to<std::uint16_t>;
  { ad.accept(s) } -> std::convertible_to<std::uint16_t>;
  { ad.bounded(s) } -> std::convertible_to<bool>;
};

// The two ways a state's acceptance is read: its own election, or the
// terminal it gives up where its boundary refused the byte behind it.
template <class Ad>
concept LexQualifiedAcceptance =
    requires(const Ad& ad, std::uint16_t s, const char* src, TextOffset i,
             TextOffset len, TextOffset& far) {
      { ad.qualified(s, src, i, len, far) } -> std::convertible_to<SymbolId>;
    };

template <class Ad>
concept LexBoundaryFallback =
    requires(const Ad& ad, std::uint16_t s, const char* src, TextOffset i,
             TextOffset len) {
      { ad.bound_has(s, src, i, len) } -> std::convertible_to<bool>;
      { ad.bound_reach(s, src, i, len) } -> std::convertible_to<TextOffset>;
      { ad.refused(s, src, i, len) } -> std::convertible_to<SymbolId>;
    };

// Whether a reading behind conditions takes the spelling of the walk's
// reading, and the tier it asks that with: one without the other is half a
// question.
template <class Ad>
concept LexTaking =
    requires(const Ad& ad, std::uint16_t s, SymbolId t, unsigned tier) {
      { ad.takes(s, tier, t) } -> std::convertible_to<bool>;
      { ad.taking(t, tier) } -> std::convertible_to<unsigned>;
    };

template <class Ad>
concept LexOffersTakes = requires(const Ad& ad, std::uint16_t s, SymbolId t,
                                  unsigned tier) { ad.takes(s, tier, t); };

// Everything the walk and its rests ask of an adapter that echoes marks.
template <class Ad>
concept LexRelayProtocol =
    requires(const Ad& ad, std::uint16_t s, const char* src, TextOffset i,
             TextOffset len, TextOffset& far, SymbolId t, unsigned tier) {
      { ad.relay(s) } -> std::convertible_to<std::uint16_t>;
      { ad.relay_of(s) } -> std::convertible_to<const LexRelay&>;
      { ad.sets() } -> std::convertible_to<const ByteSet*>;
      { ad.names() } -> std::convertible_to<const char*>;
      { ad.run(s, src, len, i) } -> std::convertible_to<TextOffset>;
      { ad.prefers_relayed(t, tier, t, tier) } -> std::convertible_to<bool>;
      { ad.tier(s, t, src, i, len, far) } -> std::convertible_to<unsigned>;
    } &&
    (!LexOffersTakes<Ad> || LexTaking<Ad>);

template <class Ad>
concept LexOffersRelay =
    requires(const Ad& ad, std::uint16_t s) { ad.relay(s); };

template <class Ad>
concept LexDeclaresQualified = requires { Ad::kQualified; };
template <class Ad>
concept LexAnnouncesQualified = requires { requires Ad::kQualified; };
template <class Ad>
concept LexDeclaresRelays = requires { Ad::kRelays; };
template <class Ad>
concept LexAnnouncesRelays = requires { requires Ad::kRelays; };

// The acceptance the adapter announces is the one it offers: `qualified` where
// `kQualified` is true, the fallback where it is false, either where it
// announces nothing.
template <class Ad>
concept LexAcceptanceAnnounced =
    (LexAnnouncesQualified<Ad> ? LexQualifiedAcceptance<Ad>
     : LexDeclaresQualified<Ad>
         ? LexBoundaryFallback<Ad>
         : (LexQualifiedAcceptance<Ad> || LexBoundaryFallback<Ad>));

// The relays the adapter announces are the ones it offers, and whole.
template <class Ad>
concept LexRelaysAnnounced =
    (!LexDeclaresRelays<Ad> || LexAnnouncesRelays<Ad> == LexOffersRelay<Ad>) &&
    (!LexOffersRelay<Ad> || LexRelayProtocol<Ad>);

}  // namespace cursiv
