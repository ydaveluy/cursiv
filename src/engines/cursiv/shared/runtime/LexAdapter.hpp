// What `lex_walk` asks of an engine's table adapter. An adapter declares
// `kQualified` (`qualified` answers a state's election) and `kRelays` (it
// offers `relay`), and the walk checks what it declares as it starts.
#pragma once

#include <concepts>
#include <cstdint>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

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
// reading, and the priority it asks it at: an adapter offers both or neither.
template <class Ad>
concept LexTaking =
    requires(const Ad& ad, std::uint16_t s, SymbolId t, unsigned priority) {
      { ad.takes(s, priority, t) } -> std::convertible_to<bool>;
      { ad.taking(t, priority) } -> std::convertible_to<unsigned>;
    };

template <class Ad>
concept LexOffersTakes =
    requires(const Ad& ad, std::uint16_t s, SymbolId t, unsigned priority) {
      ad.takes(s, priority, t);
    };

// Everything the walk and its rests ask of an adapter that echoes marks.
template <class Ad>
concept LexRelayProtocol =
    requires(const Ad& ad, std::uint16_t s, const char* src, TextOffset i,
             TextOffset len, TextOffset& far, SymbolId t, unsigned priority) {
      { ad.relay(s) } -> std::convertible_to<std::uint16_t>;
      { ad.relay_of(s) } -> std::convertible_to<const LexRelay&>;
      { ad.sets() } -> std::convertible_to<const ByteSet*>;
      { ad.names() } -> std::convertible_to<const char*>;
      { ad.run(s, src, len, i) } -> std::convertible_to<TextOffset>;
      {
        ad.prefers_relayed(t, priority, t, priority)
      } -> std::convertible_to<bool>;
      { ad.priority(s, t, src, i, len, far) } -> std::convertible_to<unsigned>;
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

// The tape `Ad` names `Skipping`, which re-reads a position without the
// terminals a refused word took off it, held in `skip` (`lex_unrefused`).
template <class Ad>
concept LexSkipping = std::same_as<Ad, typename Ad::Skipping>;
template <class Ad>
concept LexRereads = requires {
  typename Ad::Skipping;
  requires Ad::kRefuses && !LexSkipping<Ad>;
};

// The acceptance the adapter declares is the one it offers: `qualified` where
// `kQualified` is true, the fallback where it is false, either where it
// declares nothing.
template <class Ad>
concept LexAcceptanceDeclared =
    (LexAnnouncesQualified<Ad> ? LexQualifiedAcceptance<Ad>
     : LexDeclaresQualified<Ad>
         ? LexBoundaryFallback<Ad>
         : (LexQualifiedAcceptance<Ad> || LexBoundaryFallback<Ad>));

// The relays the adapter declares are the ones it offers, in full.
template <class Ad>
concept LexRelaysDeclared =
    (!LexDeclaresRelays<Ad> || LexAnnouncesRelays<Ad> == LexOffersRelay<Ad>) &&
    (!LexOffersRelay<Ad> || LexRelayProtocol<Ad>);

}  // namespace cursiv::engine
