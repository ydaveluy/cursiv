#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"

// What an AST member is, and what an element does to it: the columns
// `AstMetaData` describes a field with, the effect a record owes the tree,
// and the decode of a member pointer into them. A closed vocabulary read
// back by services; nothing here reaches a grammar.

namespace cursiv {

enum class ActKind : std::uint8_t {
  none,
  create,
  assign,
  append,
  flag,
  nest
};
// "Names no member", in every column of the registries.
inline constexpr std::uint16_t kNoField = 0xFFFF;

namespace detail {

// The three storage forms a handle factory distinguishes; `RefHandle::index`
// exists for the vector one.
enum class FieldStorage : std::uint8_t { direct, optional, vector };

// What the storage holds.
enum class FieldNature : std::uint8_t {
  value,
  node,
  reference,
  multi_reference,
  variant
};

// Asked, never spelled out at a call site: a third reference nature would
// otherwise leave half the tests behind.
constexpr bool is_ref_nature(FieldNature n) {
  return n == FieldNature::reference || n == FieldNature::multi_reference;
}
// Holds several targets (an overload set).
constexpr bool is_multi_ref_nature(FieldNature n) {
  return n == FieldNature::multi_reference;
}

template <class M>
inline constexpr bool is_reference_v = false;
template <class... Ts>
inline constexpr bool is_reference_v<cursiv::reference<Ts...>> = true;
template <class M>
inline constexpr bool is_multi_reference_v = false;
template <class... Ts>
inline constexpr bool is_multi_reference_v<cursiv::multi_reference<Ts...>> =
    true;
template <class M>
inline constexpr bool is_variant_v = false;
template <class... Ts>
inline constexpr bool is_variant_v<std::variant<Ts...>> = true;

// A string is not a container here: it has a `value_type` too, and belongs to
// the value vocabulary. Only the vocabulary's own list form is a container.
template <class M>
struct strip_storage {
  using type = M;
  static constexpr FieldStorage storage = FieldStorage::direct;
};
template <class X>
struct strip_storage<std::optional<X>> {
  using type = X;
  static constexpr FieldStorage storage = FieldStorage::optional;
};
template <class X>
struct strip_storage<cursiv::vector<X>> {
  using type = X;
  static constexpr FieldStorage storage = FieldStorage::vector;
};
// Refused by name rather than falling through as an unrecognised value type,
// where the diagnostic would be about a conversion nobody asked for.
template <class X, class A>
struct strip_storage<std::vector<X, A>> {
  static_assert(sizeof(X) == 0,
                "an AST list member is the vocabulary's `vector<T>` "
                "(cursiv::vector), not std::vector: the list is seated in the "
                "tree's own arena, which is what takes the per-node allocation "
                "and the teardown out of the build. Spell it `vector<T>` -- "
                "AstNode brings the alias into scope");
  using type = X;
  static constexpr FieldStorage storage = FieldStorage::vector;
};

template <class X>
inline constexpr FieldNature nature_of =
    is_reference_v<X>         ? FieldNature::reference
    : is_multi_reference_v<X> ? FieldNature::multi_reference
    : is_variant_v<X>         ? FieldNature::variant
    : (std::is_pointer_v<X> && std::is_class_v<std::remove_pointer_t<X>>)
        ? FieldNature::node
        : FieldNature::value;

// One distinct field of the grammar, identified by the member pointer, so two
// rules naming one member share an entry and nothing templated on this tag
// mangles wider than `&Type::field`.
template <auto P>
struct field_tag {
  static constexpr auto ptr = P;
  using owner = typename member_traits<decltype(P)>::class_type;
  using member = typename member_traits<decltype(P)>::type;
  // `vector<X*>` and `optional<X*>` both hold an `X*`.
  using held = typename strip_storage<member>::type;
  static constexpr FieldStorage storage = strip_storage<member>::storage;
  static constexpr FieldNature nature = nature_of<held>;
  static constexpr std::string_view name = member_identifier<P>();
};

}  // namespace detail
}  // namespace cursiv
