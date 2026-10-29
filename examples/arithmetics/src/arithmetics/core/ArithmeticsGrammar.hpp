#pragma once

#include "arithmetics/core/ast.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"

namespace arith {

inline constexpr auto moduleKw = cursiv::ikw<"module">.doc<
    "Starts the document and names the module.">();
inline constexpr auto defKw = cursiv::ikw<"def">.doc<
    "Defines a constant, `def pi: 3.14;`, or a function, "
    "`def area(r): pi * r ^ 2;`.">();

struct Arith : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  // terminal ID: [A-Za-z_] [0-9A-Za-z_]*;
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // terminal NUMBER: [0-9]+ ('.' [0-9]*)?;
  struct NUMBER : terminal<some(d) + option(lit<"."> + many(d))> {};

  // One token per operation, each denoting its own constant: the precedence
  // table below names them, and `op` holds what they mean.
  struct MODULO : terminal<as<ast::Operator::modulo>(lit<"%">)> {};
  struct EXPONENT : terminal<as<ast::Operator::exponent>(lit<"^">)> {};
  struct MULTIPLY : terminal<as<ast::Operator::multiply>(lit<"*">)> {};
  struct DIVIDE : terminal<as<ast::Operator::divide>(lit<"/">)> {};
  struct ADD : terminal<as<ast::Operator::add>(lit<"+">)> {};
  struct SUBTRACT : terminal<as<ast::Operator::subtract>(lit<"-">)> {};

  struct Statement;
  struct Definition;
  struct DeclaredParameter;
  struct Evaluation;
  struct Expression;
  struct PrimaryExpression;

  // Module returns Module: 'module'i name=ID statements+=Statement*;
  struct Module
      : rule<moduleKw + assign<&ast::Module::name>(ref<ID>)
             + many(append<&ast::Module::statements>(ref<Statement>))> {};

  // Statement returns AstNode: Definition | Evaluation;
  struct Statement : rule<ref<Definition> | ref<Evaluation>> {};

  // Definition returns Definition:
  // 'def'i name=ID ('(' args+=DeclaredParameter (','
  // args+=DeclaredParameter)* ')')?
  // ':' expr=Expression ';';
  struct Definition
      : rule<defKw + assign<&ast::Definition::name>(ref<ID>)
             + option(
                 lit<"(">
                 + some(append<&ast::Definition::args>(ref<DeclaredParameter>),
                        lit<",">)
                 + lit<")">)
             + lit<":">
             + assign<&ast::Definition::expr>(ref<Expression>) + lit<";">> {};

  // DeclaredParameter returns DeclaredParameter: name=ID;
  struct DeclaredParameter
      : rule<create<ast::DeclaredParameter>
             + assign<&ast::DeclaredParameter::name>(ref<ID>)> {};

  // Evaluation returns Evaluation: expression=Expression ';';
  struct Evaluation
      : rule<assign<&ast::Evaluation::expression>(ref<Expression>) + lit<";">> {
  };

  // Expression returns Expression: BinaryExpression;
  // infix BinaryExpression returns BinaryExpression on PrimaryExpression:
  // left assoc '%' > left assoc '^' > left assoc ('*' | '/')
  // > left assoc ('+' | '-');
  struct Expression : infix<PrimaryExpression, ast::BinaryExpression,
                            levels(left(ref<MODULO>),                  //
                                   left(ref<EXPONENT>),                //
                                   left(ref<MULTIPLY> | ref<DIVIDE>),  //
                                   left(ref<ADD> | ref<SUBTRACT>))> {};

  // PrimaryExpression returns Expression:
  // ({GroupedExpression} '(' expression=Expression ')')
  // | ({NumberLiteral} value=NUMBER)
  // | ({FunctionCall} func=[AbstractDefinition] ('(' args+=Expression (','
  // args+=Expression)* ')')?);
  struct PrimaryExpression
      : rule<create<ast::GroupedExpression>(
                 lit<"(">
                 + assign<&ast::GroupedExpression::expression>(ref<Expression>)
                 + lit<")">)
             | create<ast::NumberLiteral>(
                 assign<&ast::NumberLiteral::value>(ref<NUMBER>))
             | create<ast::FunctionCall>(
                 assign<&ast::FunctionCall::func>(ref<ID>)
                 + option(
                     lit<"(">
                     + some(append<&ast::FunctionCall::args>(ref<Expression>),
                            lit<",">)
                     + lit<")">))> {};

  using entry = Module;
  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace arith
