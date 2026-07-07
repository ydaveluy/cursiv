// Ranking-baseline regression: pins the CHOSEN recovery candidate for a corpus
// of malformed inputs.
//
// This is the quality oracle for recovery-ranking changes and a permanent
// regression asset. Every case below GENUINELY recovers under the current
// ranking (faithfulness-weighted first-edit score, see
// recovery_key_first_edit_score). Each case captures a stable, deterministic
// observable of the chosen recovery candidate:
//
//   struct Outcome { bool fullMatch; uint32_t parsedLength; std::string edits; };
//
// where `edits` is the parseDiagnostics joined as "begin-end:Kind" with " | "
// (Kind in {Inserted,Deleted,Replaced,Other}); the dump format mirrors
// tests/pegium/core/RecoveryCacheNeutralityHarness.hpp.
//
// The goldens were captured on current `main` by running this binary with the
// environment variable PEGIUM_DUMP_RANKING_BASELINE set, which prints each case
// as `name | fullMatch | parsedLength | edits` to stdout instead of asserting.
// To re-capture after an intentional ranking change:
//
//   PEGIUM_DUMP_RANKING_BASELINE=1 \
//     ./build/tests/pegium/core/PegiumCoreUnitTest \
//       --gtest_filter='*RankingBaseline*'
//
// then paste the dumped fields back into kRankingBaselineCases.
//
// The grammars and malformed inputs are mined from the existing recovering
// scenarios in Recovery_Lists.cpp, Recovery_Infix.cpp, Recovery_Calls.cpp and
// Recovery_Basics.cpp; the STRESS cases additionally pad a LONG keyword and/or
// a long whitespace/comment run around the error so that the first-edit BYTE
// offset is large while the TOKEN/leaf offset is small. Those are exactly the
// cases a future leaf-based ranking would be expected to rank differently, so
// they are the most valuable divergence probes.

#include "RecoveryTestSupport.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace pegium::parser;
using namespace pegium::test::recovery;

namespace {

// Stable observable of the chosen recovery candidate.
struct Outcome {
  bool fullMatch = false;
  std::uint32_t parsedLength = 0;
  std::string edits;
};

// Joins parseDiagnostics as "begin-end:Kind" with " | "; Kind is bucketed into
// {Inserted,Deleted,Replaced,Other} exactly like the cache-neutrality dump.
std::string dump_ranking_edits(const std::vector<ParseDiagnostic> &diagnostics) {
  std::string dump;
  for (const auto &diagnostic : diagnostics) {
    if (!dump.empty()) {
      dump += " | ";
    }
    dump += std::to_string(diagnostic.beginOffset);
    dump += "-";
    dump += std::to_string(diagnostic.endOffset);
    dump += ":";
    switch (diagnostic.kind) {
    case ParseDiagnosticKind::Inserted:
      dump += "Inserted";
      break;
    case ParseDiagnosticKind::Deleted:
      dump += "Deleted";
      break;
    case ParseDiagnosticKind::Replaced:
      dump += "Replaced";
      break;
    default:
      dump += "Other";
      break;
    }
  }
  return dump;
}

struct RankingBaselineCase {
  const char *name;
  // Whether this case targets byte-vs-token ranking divergence (a LONG keyword
  // and/or long trivia run around the error inflates the first-edit byte offset
  // while leaving the token/leaf offset small).
  bool stress;
  std::function<Outcome()> run;
  Outcome golden;
};

// ---------------------------------------------------------------------------
// Reusable grammar builders mined from the existing recovery test corpus.
// Each returns the captured Outcome for a specific malformed input.
// ---------------------------------------------------------------------------

// service-keyword DataTypeRule shapes (Recovery_Basics.cpp).
Outcome runServiceKeyword(std::string_view input) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  const auto skipper = SkipperBuilder().build();
  const auto result = parseDataType(rule, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// module/id keyword fuzzy + separator shapes (Recovery_Basics.cpp).
Outcome runModuleName(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id)};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// module/id with NO skipper so the word-boundary path runs (Recovery_Basics).
Outcome runModuleNameNoSkip(std::string_view input) {
  const auto skipper = SkipperBuilder().build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id)};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// ordered-choice keyword vs ("ab" ";") shapes (Recovery_Basics.cpp).
Outcome runOrderedChoiceAb(std::string_view input, bool ignoreWhitespace) {
  const auto skipper = ignoreWhitespace
                           ? SkipperBuilder().ignore(some(s)).build()
                           : SkipperBuilder().build();
  ParserRule<RecoveryNode> rule{"Rule", "a"_kw | ("ab"_kw + ";"_kw)};
  const auto result = parseRule(rule, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// service{} insertion shape (Recovery_Basics.cpp).
Outcome runServiceBraces(std::string_view input) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw + "{"_kw + "}"_kw};
  const auto skipper = SkipperBuilder().build();
  const auto result = parseDataType(rule, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Optional tail name-list (Recovery_Lists.cpp): missing comma / extra comma.
Outcome runOptionalTailList(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryNameListNode> model{
      "Model",
      "root"_kw + create<RecoveryNameListNode>() +
          option("tail"_kw + append<&RecoveryNameListNode::names>(id) +
                 many(","_kw + append<&RecoveryNameListNode::names>(id)))};
  const auto result = parseRule(model, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Direct applicable-for list (Recovery_Lists.cpp): missing comma.
Outcome runApplicableForList(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTaggedRequirementListNode> applicable{
      "Applicable",
      "applicable"_kw + "for"_kw + create<RecoveryTaggedRequirementListNode>() +
          append<&RecoveryTaggedRequirementListNode::environments>(id) +
          many(","_kw +
               append<&RecoveryTaggedRequirementListNode::environments>(id))};
  const auto result = parseRule(applicable, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Requirement model with a long garbage prefix (Recovery_Lists.cpp): delete.
Outcome runRequirementGarbagePrefix(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<std::string> text{"TEXT", "\""_kw <=> "\""_kw};
  ParserRule<RecoveryRequirementNode> requirement{
      "Requirement", "req"_kw + assign<&RecoveryRequirementNode::name>(id) +
                         assign<&RecoveryRequirementNode::label>(text)};
  ParserRule<RecoveryRequirementModelNode> model{
      "RequirementModel",
      some(append<&RecoveryRequirementModelNode::requirements>(requirement))};
  const auto result = parseRule(model, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Optional-started header recovers a stray colon (Recovery_Lists.cpp): delete.
Outcome runOptionalHeaderStrayColon(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<std::string> text{"TEXT", "\""_kw <=> "\""_kw};
  ParserRule<RecoveryContact> contact{
      "Contact", "contact"_kw + ":"_kw + assign<&RecoveryContact::name>(text)};
  ParserRule<RecoveryEnvironmentNode> environment{
      "Environment", "environment"_kw +
                         assign<&RecoveryEnvironmentNode::name>(id) + ":"_kw +
                         assign<&RecoveryEnvironmentNode::label>(text)};
  ParserRule<RecoveryRequirementNode> requirement{
      "Requirement", "req"_kw + assign<&RecoveryRequirementNode::name>(id) +
                         assign<&RecoveryRequirementNode::label>(text)};
  ParserRule<RecoveryRequirementModelNode> model{
      "RequirementModel",
      option(assign<&RecoveryRequirementModelNode::contact>(contact)) +
          many(append<&RecoveryRequirementModelNode::environments>(
              environment)) +
          some(append<&RecoveryRequirementModelNode::requirements>(
              requirement))};
  const auto result = parseRule(model, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Optional infix expression keeps recovered prefix before delimiter
// (Recovery_Infix.cpp): trailing-operator delete.
Outcome runOptionalInfixTrailingOperator(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> primary{
      "Primary", create<RecoveryNumberExpression>() +
                     assign<&RecoveryNumberExpression::value>(number)};
  InfixRule<RecoveryBinaryExpression, &RecoveryBinaryExpression::left,
            &RecoveryBinaryExpression::op, &RecoveryBinaryExpression::right>
      expression{"Expression", primary, LeftAssociation("*"_kw | "/"_kw),
                 LeftAssociation("+"_kw | "-"_kw)};
  ParserRule<RecoveryExpression> expressionRule{"Expression", expression};
  ParserRule<RecoveryExpressionEvaluation> evaluation{
      "Evaluation", option(assign<&RecoveryExpressionEvaluation::expression>(
                        expressionRule)) +
                        ";"_kw};
  const auto result = parseRule(evaluation, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Missing-required-expression before a long punctuation run + delimiter
// (Recovery_Infix.cpp): insert + delete of the operator run.
Outcome runMissingExprBeforePunctuationRun(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> primary{
      "Primary", create<RecoveryNumberExpression>() +
                         assign<&RecoveryNumberExpression::value>(number) |
                     create<RecoveryReferenceExpression>() +
                         assign<&RecoveryReferenceExpression::name>(id)};
  ParserRule<RecoveryExpressionEvaluation> evaluation{
      "Evaluation",
      assign<&RecoveryExpressionEvaluation::expression>(primary) + ";"_kw};
  const auto result = parseRule(evaluation, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Unexpected token after infix operator -> generic delete (Recovery_Infix.cpp).
Outcome runUnexpectedTokenAfterOperator(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> primary{
      "Primary", create<RecoveryNumberExpression>() +
                         assign<&RecoveryNumberExpression::value>(number) |
                     create<RecoveryReferenceExpression>() +
                         assign<&RecoveryReferenceExpression::name>(id)};
  InfixRule<RecoveryBinaryExpression, &RecoveryBinaryExpression::left,
            &RecoveryBinaryExpression::op, &RecoveryBinaryExpression::right>
      expression{"Expression", primary, LeftAssociation("*"_kw | "/"_kw),
                 LeftAssociation("+"_kw | "-"_kw)};
  ParserRule<RecoveryExpression> expressionRule{"Expression", expression};
  ParserRule<RecoveryDefinitionWithExpr> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithExpr::name>(id) + ":"_kw +
          assign<&RecoveryDefinitionWithExpr::expr>(expressionRule) + ";"_kw};
  ParserRule<RecoveryExpressionEvaluation> evaluationRule{
      "Evaluation",
      assign<&RecoveryExpressionEvaluation::expression>(expressionRule) +
          ";"_kw};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(definition |
                                                             evaluationRule))};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// star-repetition feature-block "many" mis-parse (Recovery_Infix.cpp).
Outcome runFeatureBlockManyTypo(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  DataTypeRule<std::string> qualifiedName{"QualifiedName", some(id, "."_kw)};
  ParserRule<RecoveryFeatureNode> feature{
      "Feature", option(enable_if<&RecoveryFeatureNode::many>("many"_kw)) +
                     assign<&RecoveryFeatureNode::name>(id) + ":"_kw +
                     assign<&RecoveryFeatureNode::type>(qualifiedName)};
  ParserRule<RecoveryFeatureListNode> featureBlock{
      "FeatureBlock",
      "{"_kw + many(append<&RecoveryFeatureListNode::features>(feature)) +
          "}"_kw};
  const auto result = parseRule(featureBlock, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Nullable repetition deletes garbage before recoverable iteration
// (Recovery_Infix.cpp): state/transition grammar.
Outcome runStateTransitionGarbage(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTransitionNode> transition{
      "Transition", assign<&RecoveryTransitionNode::event>(id) + "=>"_kw +
                        assign<&RecoveryTransitionNode::target>(id)};
  ParserRule<RecoveryStateNode> state{
      "State", "state"_kw + assign<&RecoveryStateNode::name>(id) +
                   many(append<&RecoveryStateNode::transitions>(transition)) +
                   "end"_kw};
  const auto result = parseRule(state, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Top-level ordered choice prefers full keyword repair (Recovery_Infix.cpp).
Outcome runTopLevelChoiceKeywordRepair(std::string_view input) {
  struct RecoveryEntityNode : pegium::AstNode {
    string name;
  };
  struct RecoveryTypeNode : pegium::AstNode {
    string name;
  };
  struct RecoveryDomainModelNode : pegium::AstNode {
    vector<pointer<pegium::AstNode>> elements;
  };
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTypeNode> dataType{
      "DataType", "datatype"_kw + assign<&RecoveryTypeNode::name>(id)};
  ParserRule<RecoveryEntityNode> entity{
      "Entity", "entity"_kw + assign<&RecoveryEntityNode::name>(id) + "{"_kw +
                    "}"_kw};
  ParserRule<pegium::AstNode> type{"Type", dataType | entity};
  ParserRule<RecoveryDomainModelNode> domainModel{
      "DomainModel", some(append<&RecoveryDomainModelNode::elements>(type))};
  const auto result = parseRule(domainModel, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Entity block with a feature list, missing its closing delimiter before the
// next declaration. The next "entity" keyword also lexes as a feature name, so
// a token-theft cascade (insert ":" after it + delete the following "{") pushes
// its first edit one token later than the faithful single close-delimiter
// insert; the ranking must keep the faithful insert.
Outcome runEntityFeaturesMissingCloseBrace(std::string_view input) {
  struct FeatureNode : pegium::AstNode {
    string name;
    string type;
  };
  struct EntityNode : pegium::AstNode {
    string name;
    vector<pointer<FeatureNode>> features;
  };
  struct ModelNode : pegium::AstNode {
    vector<pointer<EntityNode>> entities;
  };
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<FeatureNode> feature{"Feature",
                                  assign<&FeatureNode::name>(id) + ":"_kw +
                                      assign<&FeatureNode::type>(id)};
  ParserRule<EntityNode> entity{
      "Entity", "entity"_kw + assign<&EntityNode::name>(id) + "{"_kw +
                    many(append<&EntityNode::features>(feature)) + "}"_kw};
  ParserRule<ModelNode> model{"Model",
                              some(append<&ModelNode::entities>(entity))};
  const auto result = parseRule(model, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// def/optional-args module with statement choice (Recovery_Calls.cpp):
// missing delimiter / missing colon shapes.
Outcome runDefModuleStatementChoice(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> expression{
      "Expression", create<RecoveryNumberExpression>() +
                            assign<&RecoveryNumberExpression::value>(number) |
                        create<RecoveryReferenceExpression>() +
                            assign<&RecoveryReferenceExpression::name>(id)};
  ParserRule<RecoveryParameter> parameter{"Parameter",
                                          assign<&RecoveryParameter::name>(id)};
  ParserRule<RecoveryDefinitionWithOptionalArgs> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithOptionalArgs::name>(id) +
          option("("_kw +
                 append<&RecoveryDefinitionWithOptionalArgs::args>(parameter) +
                 ")"_kw) +
          ":"_kw +
          assign<&RecoveryDefinitionWithOptionalArgs::expr>(expression) +
          ";"_kw};
  ParserRule<RecoveryExpressionEvaluation> evaluation{
      "Evaluation",
      assign<&RecoveryExpressionEvaluation::expression>(expression) + ";"_kw};
  ParserRule<pegium::AstNode> statement{"Statement", definition | evaluation};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(statement))};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// def-only repetition module (Recovery_Calls.cpp): two missing colons.
Outcome runDefRepetitionModule(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> expression{
      "Expression", create<RecoveryNumberExpression>() +
                            assign<&RecoveryNumberExpression::value>(number) |
                        create<RecoveryReferenceExpression>() +
                            assign<&RecoveryReferenceExpression::name>(id)};
  ParserRule<RecoveryParameter> parameter{"Parameter",
                                          assign<&RecoveryParameter::name>(id)};
  ParserRule<RecoveryDefinitionWithOptionalArgs> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithOptionalArgs::name>(id) +
          option("("_kw +
                 append<&RecoveryDefinitionWithOptionalArgs::args>(parameter) +
                 ")"_kw) +
          ":"_kw +
          assign<&RecoveryDefinitionWithOptionalArgs::expr>(expression) +
          ";"_kw};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(definition))};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Started optional call branch keeps recovered argument prefix
// (Recovery_Calls.cpp): trailing-operator delete inside call args, and missing
// comma between numeric args.
Outcome runCallArgRecovery(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> argument{
      "Argument", create<RecoveryNumberExpression>() +
                      assign<&RecoveryNumberExpression::value>(number)};
  ParserRule<RecoveryExpression> primary{
      "Primary",
      create<RecoveryFunctionCall>() + assign<&RecoveryFunctionCall::name>(id) +
              option(
                  "("_kw + append<&RecoveryFunctionCall::args>(argument) +
                  many(","_kw + append<&RecoveryFunctionCall::args>(argument)) +
                  ")"_kw) |
          create<RecoveryNumberExpression>() +
              assign<&RecoveryNumberExpression::value>(number)};
  ParserRule<RecoveryExpressionEvaluation> evaluation{
      "Evaluation",
      assign<&RecoveryExpressionEvaluation::expression>(primary) + ";"_kw};
  const auto result = parseRule(evaluation, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// ---------------------------------------------------------------------------
// STRESS grammar builders: byte-vs-token ranking divergence probes.
// A LONG keyword and/or a long whitespace/comment run is placed before or
// around the error so the first-edit BYTE offset is large while the token/leaf
// offset is small.
// ---------------------------------------------------------------------------

// Long-keyword fuzzy replace: the keyword "configurationmanagement" is long, so
// the fuzzy-replace edit's byte offset is 0 but the *leaf* index is still the
// first token. The input drops one codepoint of the keyword.
Outcome runStressLongKeywordFuzzy(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "configurationmanagement"_kw +
                    assign<&RecoveryModule::name>(id)};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Long leading whitespace run before the first (only) error. The skipper hides
// the run, so the first VISIBLE leaf is at token 0, but the byte offset of the
// edit is pushed far to the right by the trivia.
Outcome runStressLongWhitespaceBeforeError(std::string_view input) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> expression{
      "Expression", create<RecoveryNumberExpression>() +
                        assign<&RecoveryNumberExpression::value>(number)};
  ParserRule<RecoveryDefinitionWithExpr> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithExpr::name>(id) + ":"_kw +
          assign<&RecoveryDefinitionWithExpr::expr>(expression) + ";"_kw};
  const auto result = parseRule(definition, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Long hidden comment run between two statements, with a missing semicolon in
// the first. The byte gap between the two leaves is enormous (comment trivia)
// while they are adjacent in token/leaf space.
Outcome runStressLongCommentBetweenStatements(std::string_view input) {
  const auto whitespace = some(s);
  TerminalRule<> slComment{"SL_COMMENT", "//"_kw <=> &(eol | eof)};
  const auto skipper =
      SkipperBuilder().ignore(whitespace).hide(slComment).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> expression{
      "Expression", create<RecoveryNumberExpression>() +
                        assign<&RecoveryNumberExpression::value>(number)};
  ParserRule<RecoveryExpressionEvaluation> evaluation{
      "Evaluation",
      create<RecoveryExpressionEvaluation>() +
          assign<&RecoveryExpressionEvaluation::expression>(expression) +
          ";"_kw};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(evaluation))};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Long keyword BEFORE the error: a valid long keyword prefix is consumed, then
// the error (missing separator) lands far into the byte stream while being the
// second leaf.
Outcome runStressLongKeywordBeforeError(std::string_view input) {
  const auto skipper = SkipperBuilder().build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "configurationmanagement"_kw +
                    assign<&RecoveryModule::name>(id)};
  const auto result = parseRule(module, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Long garbage delete prefix before a long keyword: the delete run's byte span
// is large while it collapses to the leading leaf region.
Outcome runStressLongDeletePrefixBeforeKeyword(std::string_view input) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  const auto skipper = SkipperBuilder().build();
  const auto result = parseDataType(rule, input, skipper);
  return Outcome{result.fullMatch, result.parsedLength,
                 dump_ranking_edits(result.parseDiagnostics)};
}

// Build a long operator run as a std::string so it can be reused in inputs.
const std::string kOperatorRun37(37u, '*');

} // namespace

// The corpus. Goldens are captured on current `main` (byte-offset ranking).
static const RankingBaselineCase kRankingBaselineCases[] = {
    // ---- delete-prefix -------------------------------------------------
    {"delete_prefix_short", false,
     [] { return runServiceKeyword("oopsservice"); },
     {true, 11, "0-4:Deleted"}},
    {"delete_prefix_long_run", false,
     [] { return runServiceKeyword("xxxxxxxxxservice"); },
     {true, 16, "0-9:Deleted"}},
    // ---- fuzzy-keyword-replace -----------------------------------------
    {"fuzzy_keyword_missing_codepoint", false,
     [] { return runModuleName("modle basicMath"); },
     {true, 15, "0-5:Replaced"}},
    {"fuzzy_keyword_transposed", false,
     [] { return runModuleName("modlue basicMath"); },
     {true, 16, "0-6:Replaced"}},
    {"fuzzy_keyword_extra_codepoint", false,
     [] { return runModuleName("modulee basicMath"); },
     {true, 17, "0-7:Replaced"}},
    {"fuzzy_keyword_single_substitution", false,
     [] { return runServiceKeyword("servixe"); },
     {true, 7, "0-7:Replaced"}},
    // ---- insert-missing-separator / delimiter --------------------------
    {"insert_word_boundary_separator", false,
     [] { return runModuleNameNoSkip("modulebasicMath"); },
     {true, 15, "6-6:Inserted"}},
    {"insert_missing_closing_brace", false,
     [] { return runServiceBraces("service{"); },
     {true, 8, "8-8:Inserted"}},
    // The faithful repair: one inserted "}" ahead of the next "entity". Two
    // mechanisms cooperate: the parent-owned iteration boundary defers the
    // feature iteration (including the panic-mode resync skip, which would
    // otherwise fabricate delete-into-the-started-token + insert-":"), and
    // the attempt ranking prefers the insert-only script over the token-theft
    // cascade (pinned by
    // GlobalRecoveryRankingInsertOnlyAttemptFoldsPastDestructiveCascade).
    {"insert_missing_close_brace_before_next_declaration", false,
     [] {
       return runEntityFeaturesMissingCloseBrace(
           "entity A {\n  x: T\nentity B {\n  y: U\n}");
     },
     {true, 37, "18-18:Inserted"}},
    // ---- ordered-choice branch pick ------------------------------------
    {"ordered_choice_insert_semicolon", false,
     [] { return runOrderedChoiceAb("ab", false); },
     {true, 2, "2-2:Inserted"}},
    {"ordered_choice_delete_prefix_then_branch", false,
     [] { return runOrderedChoiceAb("x   ab", true); },
     {true, 6, "0-1:Deleted | 6-6:Inserted"}},
    {"ordered_choice_full_keyword_repair", false,
     [] { return runTopLevelChoiceKeywordRepair("entit Blog {}"); },
     {true, 13, "0-5:Replaced"}},
    // ---- missing comma in lists ----------------------------------------
    {"list_missing_comma_between_items", false,
     [] { return runOptionalTailList("root tail alpha beta"); },
     {true, 20, "16-16:Inserted"}},
    {"list_extra_comma_before_item", false,
     [] { return runOptionalTailList("root tail alpha,, beta"); },
     {true, 22, "16-17:Deleted"}},
    {"list_applicable_for_missing_comma", false,
     [] { return runApplicableForList("applicable for prod staging"); },
     {true, 27, "20-20:Inserted"}},
    // ---- infix operator noise ------------------------------------------
    {"infix_trailing_operator_delete", false,
     [] { return runOptionalInfixTrailingOperator("81/;"); },
     {true, 4, "2-3:Deleted"}},
    {"infix_unexpected_token_after_operator", false,
     [] {
       return runUnexpectedTokenAfterOperator("module calc\ndef c: 8;\n2 * +c;\n");
     },
     {true, 30, "26-27:Deleted"}},
    {"infix_missing_expr_before_punctuation_run", false,
     [] { return runMissingExprBeforePunctuationRun(kOperatorRun37 + ";"); },
     {true, 38, "0-37:Deleted | 37-37:Inserted"}},
    // ---- repetition ----------------------------------------------------
    {"repetition_feature_block_many_typo", false,
     [] {
       return runFeatureBlockManyTypo(
           "{\n  many comments Comment\n  title: String\n}\n");
     },
     {true, 44, "18-18:Inserted"}},
    {"repetition_state_garbage_line", false,
     [] {
       return runStateTransitionGarbage(
           "state Idle\n<<<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle\nend\n");
     },
     {true, 54, "11-35:Deleted"}},
    // ---- trailing garbage / delete -------------------------------------
    {"requirement_long_garbage_prefix", false,
     [] {
       return runRequirementGarbagePrefix(
           "<<<<<<<<<<<<<<<<<<<<<<<<\nreq login \"Users can login\"\n");
     },
     {true, 53, "0-24:Deleted"}},
    {"optional_header_stray_colon", false,
     [] {
       return runOptionalHeaderStrayColon(
           "contact:: \"team\"\nenvironment prod: \"Production\"\nreq login "
           "\"Users can login\"\n");
     },
     {true, 76, "8-9:Deleted"}},
    // ---- missing-delimiter inside def modules --------------------------
    {"def_module_missing_semicolon", false,
     [] {
       return runDefModuleStatementChoice(
           "module m\ndef a: 5\ndef b: 3;\ndef c: b;");
     },
     {true, 37, "18-18:Inserted"}},
    {"def_module_missing_delimiter_before_value", false,
     [] {
       return runDefModuleStatementChoice(
           "module m\ndef a 5;\ndef b: 3;\ndef c: b;");
     },
     {true, 37, "15-15:Inserted"}},
    {"def_repetition_two_missing_colons", false,
     [] { return runDefRepetitionModule("module m\ndef a 5;\ndef b 3;"); },
     {true, 26, "15-15:Inserted | 24-24:Inserted"}},
    // ---- call argument recovery ----------------------------------------
    {"call_arg_trailing_operator_delete", false,
     [] { return runCallArgRecovery("sqrt(81/);"); },
     {true, 10, "7-8:Deleted"}},
    {"call_arg_missing_comma_between_numbers", false,
     [] { return runCallArgRecovery("root(64 3);"); },
     {true, 11, "8-8:Inserted"}},

    // ===================================================================
    // STRESS: byte-vs-token ranking divergence probes.
    // ===================================================================
    {"stress_long_keyword_fuzzy_replace", true,
     [] {
       // drop one codepoint of the 23-char keyword.
       return runStressLongKeywordFuzzy("configurationmanagment app");
     },
     {true, 26, "0-22:Replaced"}},
    {"stress_long_keyword_before_error", true,
     // valid long keyword, then no separator before the name -> error byte
     // offset is 23 but it is only the second leaf.
     [] { return runStressLongKeywordBeforeError("configurationmanagementapp"); },
     {true, 26, "0-2:Deleted | 2-2:Inserted"}},
    {"stress_long_whitespace_before_error", true,
     [] {
       // ~40 spaces of hidden trivia before a missing-colon error.
       return runStressLongWhitespaceBeforeError(
           "def                                        a 5;");
     },
     {true, 47, "45-45:Inserted"}},
    {"stress_long_comment_between_statements", true,
     [] {
       return runStressLongCommentBetweenStatements(
           "module m\n"
           "1 // a very long single line comment used purely as hidden trivia\n"
           "2;\n");
     },
     {true, 78, "75-75:Inserted"}},
    {"stress_long_delete_prefix_before_keyword", true,
     [] {
       return runStressLongDeletePrefixBeforeKeyword(
           "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxservice");
     },
     {true, 39, "0-32:Deleted"}},
};

namespace {
bool ranking_baseline_dump_enabled() {
  const char *env = std::getenv("PEGIUM_DUMP_RANKING_BASELINE");
  return env != nullptr && env[0] != '\0';
}
} // namespace

TEST(RecoveryRankingBaselineTest, ChosenCandidateMatchesGolden) {
  const bool dump = ranking_baseline_dump_enabled();

  for (const auto &c : kRankingBaselineCases) {
    SCOPED_TRACE(c.name);
    const Outcome actual = c.run();

    if (dump) {
      std::cout << "GOLDEN " << c.name << " | " << (actual.fullMatch ? 1 : 0)
                << " | " << actual.parsedLength << " | " << actual.edits
                << '\n';
      continue;
    }

    // Corpus non-triviality: each case must GENUINELY recover. A case that
    // both fully matched and produced no edits did not exercise recovery.
    EXPECT_FALSE(actual.fullMatch && actual.edits.empty())
        << "case '" << c.name
        << "' did not recover (fullMatch && empty edits) — it is not a "
           "meaningful ranking probe";

    EXPECT_EQ(actual.fullMatch, c.golden.fullMatch);
    EXPECT_EQ(actual.parsedLength, c.golden.parsedLength);
    EXPECT_EQ(actual.edits, c.golden.edits);
  }
}

TEST(RecoveryRankingBaselineTest, CorpusCountsAreStable) {
  std::size_t total = 0;
  std::size_t stress = 0;
  for (const auto &c : kRankingBaselineCases) {
    ++total;
    if (c.stress) {
      ++stress;
    }
  }
  EXPECT_EQ(total, 32u);
  EXPECT_EQ(stress, 5u);
}

// forbid-Replace global probe: a keyword that exactly prefixes the input plus
// one extra identifier char (`moduleX`, grammar `module = "module"_kw + id`)
// must keep the keyword and insert a boundary so the id consumes the extra char
// (6-6:Inserted, faithfulness 5), NOT fuzzy-Replace the whole `moduleX` into
// `module` and synthesise the id (0-7:Replaced | 7-7:Inserted, faithfulness 13).
// The greedy local terminal choice prefers the cheaper-looking distance-1 fold;
// only a whole-attempt probe (re-descend with the split-insertable fuzzy-Replace
// forbidden, kept iff fullMatch and outranks) can surface the better reading.
TEST(RecoveryRankingBaselineTest, TerminalKeywordExtraCharKeepsKeyword) {
  const Outcome outcome = runModuleNameNoSkip("moduleX");
  EXPECT_TRUE(outcome.fullMatch);
  EXPECT_EQ(outcome.parsedLength, 7u);
  EXPECT_EQ(outcome.edits, "6-6:Inserted");
}
