#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "workspace/core/ast.hpp"

namespace ws {

namespace x = cursiv;

// Prose on the word that carries it: what a hover shows when the caret is on
// a keyword rather than on a name.
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

  // terminal ID: [A-Za-z_] [0-9A-Za-z_]*;
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // QualifiedName returns string: ID ('.' ID)*;
  struct QualifiedName : rule<some(ref<ID>, lit<".">)> {};

  struct AbstractElementRule;
  struct TypeRule;
  struct DataTypeRule;
  struct EntityRule;
  struct FeatureRule;
  struct PackageDeclarationRule;

  // Feature returns Feature: many?='many'i? name=ID ':' type=QualifiedName;
  struct FeatureRule
      : rule<option(flag<&ast::Feature::many>(ikw<"many">)) +
             assign<&ast::Feature::name>(ref<ID>) + lit<":"> +
             assign<&ast::Feature::type>(ref<QualifiedName>)> {};

  // DataType returns DataType: 'datatype'i name=ID;
  struct DataTypeRule : rule<create<ast::DataType> + datatypeKw +
                             assign<&ast::DataType::name>(ref<ID>)> {};

  // Entity returns Entity:
  // 'entity'i name=ID ('extends'i superType=QualifiedName)?
  // '{' features+=Feature* '}';
  struct EntityRule
      : rule<entityKw + assign<&ast::Entity::name>(ref<ID>) +
             option(ikw<"extends"> +
                    assign<&ast::Entity::superType>(ref<QualifiedName>)) +
             lit<"{"> + many(append<&ast::Entity::features>(ref<FeatureRule>)) +
             lit<"}">> {};

  // Type returns Type: DataType | Entity;
  struct TypeRule : rule<ref<DataTypeRule> | ref<EntityRule>> {};

  // PackageDeclaration returns PackageDeclaration:
  // 'package'i name=QualifiedName '{' elements+=AbstractElement* '}';
  struct PackageDeclarationRule
      : rule<packageKw +
             assign<&ast::PackageDeclaration::name>(ref<QualifiedName>) +
             lit<"{"> +
             many(append<&ast::PackageDeclaration::elements>(
                 ref<AbstractElementRule>)) +
             lit<"}">> {};

  // AbstractElement returns AbstractElement: PackageDeclaration | Type;
  struct AbstractElementRule
      : rule<ref<PackageDeclarationRule> | ref<TypeRule>> {};

  // Domainmodel returns Workspace: elements+=AbstractElement+;
  struct Domainmodel : rule<some(append<&ast::Workspace::elements>(
                           ref<AbstractElementRule>))> {};

  using entry = Domainmodel;
  using ignore = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace ws
