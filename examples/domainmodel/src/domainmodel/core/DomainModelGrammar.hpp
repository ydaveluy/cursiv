#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "domainmodel/core/ast.hpp"

namespace dm {

inline constexpr auto packageKw = cursiv::ikw<"package">.doc<
    "Groups types under a qualified name: `package blog.archive { ... }`.">();
inline constexpr auto datatypeKw = cursiv::ikw<"datatype">.doc<
    "Declares a basic type, such as `String`.">();
inline constexpr auto entityKw = cursiv::ikw<"entity">.doc<
    "Declares an entity and its features, between `{` and `}`.">();
inline constexpr auto extendsKw = cursiv::ikw<"extends">.doc<
    "Names the entity this one inherits its features from.">();
inline constexpr auto manyKw = cursiv::ikw<"many">.doc<
    "Marks a feature that holds several values.">();

struct DomainModel : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  // terminal ID: [A-Za-z_] [0-9A-Za-z_]*;
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // QualifiedName returns string: ID ('.' ID)*;
  struct QualifiedName : rule<some(ref<ID>, lit<".">)> {};

  struct AbstractElement;
  struct Type;
  struct DataType;
  struct Entity;
  struct Feature;
  struct PackageDeclaration;

  // Feature returns Feature: many?='many'i? name=ID ':' type=QualifiedName;
  struct Feature : rule<option(flag<&ast::Feature::many>(manyKw))
                        + assign<&ast::Feature::name>(ref<ID>) + lit<":">
                        + assign<&ast::Feature::type>(ref<QualifiedName>)> {};

  // DataType returns DataType: 'datatype'i name=ID;
  struct DataType : rule<create<ast::DataType> + datatypeKw
                         + assign<&ast::DataType::name>(ref<ID>)> {};

  // Entity returns Entity:
  // 'entity'i name=ID ('extends'i superType=QualifiedName)?
  // '{' features+=Feature* '}';
  struct Entity
      : rule<entityKw + assign<&ast::Entity::name>(ref<ID>)
             + option(extendsKw
                      + assign<&ast::Entity::superType>(ref<QualifiedName>))
             + lit<"{"> + many(append<&ast::Entity::features>(ref<Feature>))
             + lit<"}">> {};

  // Type returns Type: DataType | Entity;
  struct Type : rule<ref<DataType> | ref<Entity>> {};

  // PackageDeclaration returns PackageDeclaration:
  // 'package'i name=QualifiedName '{' elements+=AbstractElement* '}';
  struct PackageDeclaration
      : rule<packageKw
             + assign<&ast::PackageDeclaration::name>(ref<QualifiedName>)
             + lit<"{">
             + many(append<&ast::PackageDeclaration::elements>(
                 ref<AbstractElement>))
             + lit<"}">> {};

  // AbstractElement returns AbstractElement: PackageDeclaration | Type;
  struct AbstractElement : rule<ref<PackageDeclaration> | ref<Type>> {};

  // Domainmodel returns DomainModel: elements+=AbstractElement+;
  struct Domainmodel
      : rule<some(append<&ast::DomainModel::elements>(ref<AbstractElement>))> {
  };

  using entry = Domainmodel;
  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace dm
