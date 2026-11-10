#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "workspace/core/ast.hpp"

namespace ws {

namespace x = cursiv;

// Keyword documentation, which a hover on the keyword shows.
inline constexpr auto packageKw =
    x::ikw<"package">.doc<"Opens a package, the scope its members answer in.">();
inline constexpr auto entityKw =
    x::ikw<"entity">.doc<"Declares an entity, closed by `}`.">();
inline constexpr auto datatypeKw =
    x::ikw<"datatype">.doc<"Declares a data type a feature may name.">();

struct Workspace : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  // Beyond ASCII, Latin and Greek letters, `ẞ`, and the Ohm and Kelvin signs,
  // for names that ignore case.
  struct ID
      : terminal<cr<R"(a-zA-Z_\u00C0-\u024F\u0370-\u03FF\u1E9E\u2126\u212A)"> +
                 many(cr<R"(\w\u00C0-\u024F\u0370-\u03FF\u1E9E\u2126\u212A)">)> {};

  struct QualifiedName : rule<some(ref<ID>, lit<".">)> {};

  struct AbstractElementRule;
  struct TypeRule;
  struct DataTypeRule;
  struct EntityRule;
  struct FeatureRule;
  struct PackageDeclarationRule;

  struct FeatureRule
      : rule<option(flag<&ast::Feature::many>(ikw<"many">)) +
             assign<&ast::Feature::name>(ref<ID>) + lit<":"> +
             assign<&ast::Feature::type>(ref<QualifiedName>)> {};

  struct DataTypeRule : rule<create<ast::DataType> + datatypeKw +
                             assign<&ast::DataType::name>(ref<ID>)> {};

  struct EntityRule
      : rule<entityKw + assign<&ast::Entity::name>(ref<ID>) +
             option(ikw<"extends"> +
                    assign<&ast::Entity::superType>(ref<QualifiedName>)) +
             lit<"{"> + many(append<&ast::Entity::features>(ref<FeatureRule>)) +
             lit<"}">> {};

  struct TypeRule : rule<ref<DataTypeRule> | ref<EntityRule>> {};

  struct PackageDeclarationRule
      : rule<packageKw +
             assign<&ast::PackageDeclaration::name>(ref<QualifiedName>) +
             lit<"{"> +
             many(append<&ast::PackageDeclaration::elements>(
                 ref<AbstractElementRule>)) +
             lit<"}">> {};

  struct AbstractElementRule
      : rule<ref<PackageDeclarationRule> | ref<TypeRule>> {};

  struct Domainmodel : rule<some(append<&ast::Workspace::elements>(
                           ref<AbstractElementRule>))> {};

  using entry = Domainmodel;
  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace ws
