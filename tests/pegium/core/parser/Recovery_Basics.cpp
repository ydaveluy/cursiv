#include "RecoveryTestSupport.hpp"

using namespace pegium::parser;
using namespace pegium::test::recovery;

TEST(RecoveryTest,
     WordLiteralCanRecoverByGenericDeletePrefixBeforeLowConfidenceReplace) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  const std::string input = "xxxxxxxxxxxxxxxxxservice";
  const auto skipper = SkipperBuilder().build();

  ParseOptions constrainedOptions;
  constrainedOptions.maxRecoveryEditCost = 64;
  const auto constrainedResult =
      parseDataType(rule, input, skipper, constrainedOptions);
  ASSERT_TRUE(constrainedResult.value);
  ASSERT_FALSE(constrainedResult.parseDiagnostics.empty());
  EXPECT_EQ(constrainedResult.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Deleted);

  ParseOptions tunedOptions;
  tunedOptions.maxRecoveryEditCost = 128;
  const auto tunedResult = parseDataType(rule, input, skipper, tunedOptions);
  EXPECT_TRUE(tunedResult.value);
  EXPECT_FALSE(tunedResult.parseDiagnostics.empty());
}

TEST(RecoveryTest, ContiguousDeleteRunCanRecoverBeyondDefaultEditCountBudget) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  const std::string input = "xxxxxxxxxservice";
  const auto skipper = SkipperBuilder().build();

  const auto result = parseDataType(rule, input, skipper);

  ASSERT_TRUE(result.value);
  ASSERT_FALSE(result.parseDiagnostics.empty());
  ASSERT_EQ(result.parseDiagnostics.size(), 1u);
  EXPECT_EQ(result.parseDiagnostics.front().kind, ParseDiagnosticKind::Deleted);
  EXPECT_EQ(result.parseDiagnostics.front().beginOffset, 0u);
  EXPECT_EQ(result.parseDiagnostics.front().endOffset, 9u);
}

TEST(RecoveryTest,
     NullableRepetitionCanYieldToStrictSuffixAfterFalseIterationStart) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTransitionBlockNode> rule{"Root", many(id + "=>"_kw + id) +
                                                           "end"_kw};
  const auto skipper = SkipperBuilder().build();

  const auto result = parseRule(rule, "end", skipper);

  EXPECT_TRUE(result.fullMatch);
  EXPECT_EQ(result.parsedLength, 3u);
  EXPECT_TRUE(result.parseDiagnostics.empty())
      << dump_parse_diagnostics(result.parseDiagnostics);
}

TEST(RecoveryTest,
     BoundedNullableRepetitionCanYieldToStrictSuffixAfterFalseIterationStart) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTransitionBlockNode> rule{
      "Root", repeat<0, 3>(id + "=>"_kw + id) + "end"_kw};
  const auto skipper = SkipperBuilder().build();

  const auto result = parseRule(rule, "end", skipper);

  EXPECT_TRUE(result.fullMatch);
  EXPECT_EQ(result.parsedLength, 3u);
  EXPECT_TRUE(result.parseDiagnostics.empty())
      << dump_parse_diagnostics(result.parseDiagnostics);
}

TEST(RecoveryTest, DiagnosticsTrackDeleteAndInsertEdits) {
  const auto skipper = SkipperBuilder().build();

  {
    DataTypeRule<std::string> rule{"Rule", "service"_kw};
    const std::string input = "oopsservice";
    const auto result = parseDataType(rule, input, skipper);

    ASSERT_TRUE(result.value);
    ASSERT_FALSE(result.parseDiagnostics.empty());
    EXPECT_TRUE(std::ranges::all_of(
        result.parseDiagnostics, [](const ParseDiagnostic &d) {
          return d.kind == ParseDiagnosticKind::Deleted ||
                 d.kind == ParseDiagnosticKind::Replaced;
        }));
    EXPECT_EQ(result.parseDiagnostics.front().offset, 0u);
  }

  {
    DataTypeRule<std::string> rule{"Rule", "service"_kw + "{"_kw + "}"_kw};
    const std::string input = "service{";
    const auto result = parseDataType(rule, input, skipper);

    ASSERT_TRUE(result.value);
    ASSERT_FALSE(result.parseDiagnostics.empty());
    EXPECT_TRUE(std::ranges::any_of(
        result.parseDiagnostics, [](const ParseDiagnostic &d) {
          return d.kind == ParseDiagnosticKind::Inserted;
        }));
    const auto inserted = std::ranges::find_if(
        result.parseDiagnostics, [](const ParseDiagnostic &d) {
          return d.kind == ParseDiagnosticKind::Inserted;
        });
    ASSERT_NE(inserted, result.parseDiagnostics.end());
    EXPECT_EQ(inserted->offset, 8u);
  }
}

TEST(RecoveryTest, IncompleteDiagnosticAtEofUsesParseOffset) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  ParseOptions options;
  options.recoveryEnabled = false;

  const auto result =
      parseDataType(rule, "", SkipperBuilder().build(), options);

  ASSERT_EQ(result.parseDiagnostics.size(), 1u);
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Incomplete);
  EXPECT_EQ(result.parseDiagnostics.front().offset, 0u);
}

TEST(RecoveryTest, GenericLiteralFuzzyRecoveryRepairsSingleEditOsaShapes) {
  DataTypeRule<std::string> rule{"Rule", "service"_kw};
  const auto skipper = SkipperBuilder().build();

  {
    const std::string input = "servixe";
    const auto result = parseDataType(rule, input, skipper);
    EXPECT_TRUE(result.value);
    EXPECT_TRUE(std::ranges::any_of(
        result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
          return diagnostic.kind == ParseDiagnosticKind::Replaced;
        }));
  }

  {
    const std::string input = "serivce";
    const auto result = parseDataType(rule, input, skipper);
    EXPECT_TRUE(result.value);
    EXPECT_TRUE(std::ranges::any_of(
        result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
          return diagnostic.kind == ParseDiagnosticKind::Replaced;
        }));
  }

  {
    const std::string input = "sxrivxe";
    const auto result = parseDataType(rule, input, skipper);
    EXPECT_TRUE(result.value);
    EXPECT_TRUE(std::ranges::any_of(
        result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
          return diagnostic.kind == ParseDiagnosticKind::Replaced;
        }));
  }
}

TEST(RecoveryTest, NonAsciiKeywordFuzzyRecoveryRepairsSingleCodepointTypo) {
  // End-to-end codepoint-granular fuzzy repair: an accented keyword "modèle"
  // (6 codepoints / 7 bytes) typed "modele" is a single codepoint substitution
  // (è -> e). The codepoint DP scores it as one Replace edit and the recovered
  // span covers the whole 6-byte input (one diagnostic), not a byte-split
  // sub+insert pair.
  const auto skipper = SkipperBuilder().build();
  DataTypeRule<std::string> rule{"Rule", "mod\xC3\xA8le"_kw};
  const auto result = parseDataType(rule, "modele", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_EQ(result.parsedLength, 6u);
  ASSERT_EQ(result.parseDiagnostics.size(), 1u)
      << dump_parse_diagnostics(result.parseDiagnostics);
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Replaced);
  EXPECT_EQ(result.parseDiagnostics.front().beginOffset, 0u);
  EXPECT_EQ(result.parseDiagnostics.front().endOffset, 6u);
}

TEST(RecoveryTest, AnyCharacterRecoveryDoesNotInventMissingCharacter) {
  DataTypeRule<std::string_view> rule{"Rule", dot};
  const auto result = parseDataType(rule, "", SkipperBuilder().build());

  EXPECT_FALSE(result.value);
  EXPECT_TRUE(std::ranges::none_of(
      result.parseDiagnostics, [](const ParseDiagnostic &d) {
        return d.kind == ParseDiagnosticKind::Inserted;
      }));
}

TEST(RecoveryTest, AndPredicateRecoveryDoesNotUseEdits) {
  DataTypeRule<std::string> rule{"Rule", &"a"_kw + "a"_kw};
  const std::string input = "xa";
  const auto result = parseDataType(rule, input, SkipperBuilder().build());
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);
  EXPECT_FALSE(result.value) << parseDump;
}

TEST(RecoveryTest, NotPredicateRecoveryDoesNotUsePrefixDeleteRetry) {
  DataTypeRule<std::string> rule{"Rule", !"x"_kw + "a"_kw};
  const std::string input = "xa";
  const auto result = parseDataType(rule, input, SkipperBuilder().build());
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  EXPECT_FALSE(result.value) << parseDump;
}

TEST(RecoveryTest,
     OrderedChoiceRecoveryDoesNotShortCircuitCleanBoundaryWhenLaterBranchWins) {
  const auto skipper = SkipperBuilder().build();
  ParserRule<RecoveryNode> rule{"Rule", "a"_kw | ("ab"_kw + ";"_kw)};

  const auto result = parseRule(rule, "ab", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;
  ASSERT_EQ(result.parseDiagnostics.size(), 1u) << parseDump;
  EXPECT_EQ(result.parseDiagnostics.front().kind, ParseDiagnosticKind::Inserted)
      << parseDump;
}

TEST(RecoveryTest,
     OrderedChoiceDeleteRetryAcrossHiddenTriviaKeepsLaterCleanBranch) {
  const auto skipper = SkipperBuilder().ignore(some(s)).build();
  ParserRule<RecoveryNode> rule{"Rule", "a"_kw | ("ab"_kw + ";"_kw)};

  const auto result = parseRule(rule, "x   ab", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;
  EXPECT_TRUE(result.result.recoveryReport.hasRecovered) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(result.parseDiagnostics,
                                  [](const ParseDiagnostic &diagnostic) {
                                    return diagnostic.kind ==
                                               ParseDiagnosticKind::Deleted &&
                                           diagnostic.beginOffset == 0u;
                                  }))
      << parseDump;
  EXPECT_TRUE(std::ranges::any_of(result.parseDiagnostics,
                                  [](const ParseDiagnostic &diagnostic) {
                                    return diagnostic.kind ==
                                           ParseDiagnosticKind::Inserted;
                                  }))
      << parseDump;
  EXPECT_FALSE(std::ranges::any_of(result.parseDiagnostics,
                                   [](const ParseDiagnostic &diagnostic) {
                                     return diagnostic.kind ==
                                                ParseDiagnosticKind::Deleted &&
                                            diagnostic.beginOffset == 5u;
                                   }))
      << parseDump;
}

TEST(RecoveryTest, RecoveryCanBeDisabledThroughParseOptions) {
  const std::string input = "oopsservice";
  const auto skipper = SkipperBuilder().build();

  ParseOptions options;
  options.recoveryEnabled = false;

  {
    DataTypeRule<std::string> rule{"DataRule", "service"_kw};
    const auto result = parseDataType(rule, input, skipper, options);
    EXPECT_FALSE(result.value);
    ASSERT_EQ(result.parseDiagnostics.size(), 1u);
    EXPECT_EQ(result.parseDiagnostics.front().kind,
              ParseDiagnosticKind::Incomplete);
  }

  {
    TerminalRule<std::string_view> rule{"TerminalRule", "service"_kw};
    const auto result = parseTerminal(rule, input, skipper, options);
    EXPECT_FALSE(result.value);
    ASSERT_EQ(result.parseDiagnostics.size(), 1u);
    EXPECT_EQ(result.parseDiagnostics.front().kind,
              ParseDiagnosticKind::Incomplete);
  }

  {
    ParserRule<RecoveryNode> rule{"ParserRule",
                                  assign<&RecoveryNode::token>("service"_kw)};
    const auto result = parseRule(rule, input, skipper, options);
    EXPECT_FALSE(result.value);
    ASSERT_EQ(result.parseDiagnostics.size(), 1u);
    EXPECT_EQ(result.parseDiagnostics.front().kind,
              ParseDiagnosticKind::Incomplete);
  }
}

TEST(
    RecoveryTest,
    ConsecutiveDeleteRecoveryAcrossTerminalBoundaryIsNotGuaranteedGenerically) {
  DataTypeRule<std::string> rule{"Rule", "aaa"_kw + "{"_kw};
  const std::string input = "aaaXXX{";
  const auto skipper = SkipperBuilder().build();

  ParseOptions options;
  options.maxConsecutiveCodepointDeletes = 8;

  const auto result = parseDataType(rule, input, skipper, options);
  EXPECT_FALSE(result.value);
}

TEST(RecoveryTest,
     MissingRequiredTokenRecoveryStillBuildsRootAndReportsSyntax) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryDefinition> definition{
      "Definition", "def"_kw + assign<&RecoveryDefinition::name>(id) + ":"_kw +
                        assign<&RecoveryDefinition::value>(number) + ";"_kw};
  ParserRule<RecoveryEvaluation> evaluation{
      "Evaluation", assign<&RecoveryEvaluation::name>(id) + ";"_kw};
  ParserRule<pegium::AstNode> statement{"Statement", definition | evaluation};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(statement))};

  const auto result = parseRule(module, "module \n\ndef a:4;", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_FALSE(result.parseDiagnostics.empty());
  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics,
      [](const ParseDiagnostic &diagnostic) { return diagnostic.isSyntax(); }));
}

TEST(RecoveryTest,
     WordBoundaryViolationCanInsertSyntheticGapForKeywordLiteral) {
  const auto skipper = SkipperBuilder().build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id)};

  const auto result = parseRule(module, "modulebasicMath", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      }));
  EXPECT_FALSE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Deleted;
      }));

  const auto inserted = std::ranges::find_if(
      result.parseDiagnostics, [](const ParseDiagnostic &d) {
        return d.kind == ParseDiagnosticKind::Inserted;
      });
  ASSERT_NE(inserted, result.parseDiagnostics.end());
  EXPECT_EQ(inserted->offset, 6u);
  EXPECT_EQ(inserted->element, nullptr);
  EXPECT_EQ(inserted->message, "Expecting separator");
  ASSERT_NE(result.cst, nullptr);
  const auto recovered = detail::first_recovered_node(*result.cst);
  EXPECT_FALSE(recovered.valid());

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  EXPECT_EQ(parsedModule->name, "basicMath");
}

TEST(RecoveryTest, KeywordGluedToShortNameKeepsKeywordAndContinues) {
  // `Moduleb` = keyword `module` glued to a short name `b`. The whole-word
  // fuzzy Replace (`Moduleb` -> `module`, distance 1) is cheap and would swallow
  // the name, then delete the trailing statement. The keep-keyword split-Insert
  // (insert a separator at 6, name `b`) is strictly more faithful and lets the
  // `def a: 5;` statement parse. The forbid-Replace sibling probe must pick it.
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryDefinition> definition{
      "Definition", "def"_kw.i() + assign<&RecoveryDefinition::name>(id) +
                        ":"_kw + assign<&RecoveryDefinition::value>(number) +
                        ";"_kw};
  ParserRule<pegium::AstNode> statement{"Statement", definition};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw.i() + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(statement))};

  const auto result = parseRule(module, "Moduleb\n\ndef a: 5;", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;

  // Exactly one edit: insert the missing separator after the keyword.
  ASSERT_EQ(result.parseDiagnostics.size(), 1u) << parseDump;
  EXPECT_EQ(result.parseDiagnostics[0].kind, ParseDiagnosticKind::Inserted)
      << parseDump;
  EXPECT_EQ(result.parseDiagnostics[0].offset, 6u) << parseDump;
  EXPECT_FALSE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Replaced ||
               diagnostic.kind == ParseDiagnosticKind::Deleted;
      })) << parseDump;

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr) << parseDump;
  EXPECT_EQ(parsedModule->name, "b") << parseDump;
  EXPECT_EQ(parsedModule->statements.size(), 1u) << parseDump;
}

TEST(RecoveryTest, MissingKeywordCodepointCanRecoverLiteralAndContinue) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id)};

  const auto result = parseRule(module, "modle basicMath", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Replaced;
      }));

  const auto replaced = std::ranges::find_if(
      result.parseDiagnostics, [](const ParseDiagnostic &d) {
        return d.kind == ParseDiagnosticKind::Replaced;
      });
  ASSERT_NE(replaced, result.parseDiagnostics.end());
  EXPECT_EQ(replaced->offset, 0u);
  EXPECT_NE(replaced->element, nullptr);

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  EXPECT_EQ(parsedModule->name, "basicMath");
}

TEST(RecoveryTest, KeywordLiteralFuzzyRepairContinues) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id)};

  struct Case {
    const char *name;
    std::string_view input;
  };
  static const Case cases[] = {
      {"MissingKeywordSuffix", "Mod basicMath"},
      {"ExtraKeywordCodepoint", "modulee basicMath"},
      {"TransposedKeywordCodepoints", "modlue basicMath"},
  };

  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);

    const auto result = parseRule(module, c.input, skipper);

    ASSERT_TRUE(result.value);
    EXPECT_TRUE(result.fullMatch);
    EXPECT_TRUE(std::ranges::any_of(
        result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
          return diagnostic.kind == ParseDiagnosticKind::Replaced;
        }));

    auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
    ASSERT_NE(parsedModule, nullptr);
    EXPECT_EQ(parsedModule->name, "basicMath");
  }
}

TEST(RecoveryTest, HiddenGapWordLikeLiteralCanUseIdentifierLikeFuzzyRepair) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "prefix"_kw + "module"_kw + assign<&RecoveryModule::name>(id)};

  const auto result = parseRule(module, "prefix\nmodle basicMath", skipper);

  ASSERT_TRUE(result.value) << dump_parse_diagnostics(result.parseDiagnostics);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Replaced;
      }));

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  EXPECT_EQ(parsedModule->name, "basicMath");
}

TEST(RecoveryTest,
     HiddenGapWordLikeLiteralDoesNotFuzzyReplaceFromPunctuationSource) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryModule> module{
      "Module", "prefix"_kw + "module"_kw + assign<&RecoveryModule::name>(id)};

  const auto result = parseRule(module, "prefix\n:odule basicMath", skipper);

  EXPECT_FALSE(result.value);
  ASSERT_FALSE(result.parseDiagnostics.empty());
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Incomplete);
}

TEST(RecoveryTest,
     WordLikeLiteralFuzzyAdmissibilityUsesSharedLexicalAndTriviaFacts) {
  const auto profile = detail::make_terminal_shape_from_literal("module");
  // ASCII fixture: consumedCodepoints == consumed (one byte per codepoint).
  // The half-length floor reads consumedCodepoints, so set both in lockstep.
  detail::LiteralFuzzyCandidate candidate{
      .consumed = 6u,
      .consumedCodepoints = 6u,
      .distance = 2u,
      .operationCount = 2u,
      .cost = detail::make_recovery_cost(2u, 4u),
      .substitutionCount = 1u,
  };
  const detail::TerminalRecoveryFacts hiddenGapFacts{
      .triviaGap = {.hasHiddenGap = true,
                    .visibleSourceAfterLocalSkip = false},
      .previousElementIsTerminalish = false,
  };

  EXPECT_FALSE(detail::allows_literal_fuzzy_candidate(candidate, profile,
                                                      hiddenGapFacts, false));
  EXPECT_FALSE(detail::allows_literal_fuzzy_candidate(candidate, profile,
                                                      hiddenGapFacts, true));

  candidate.substitutionCount = 0u;
  EXPECT_TRUE(detail::allows_literal_fuzzy_candidate(candidate, profile,
                                                     hiddenGapFacts, true));

  candidate.cost.primaryRankCost = 10u;
  EXPECT_FALSE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, true));
}

TEST(
    RecoveryTest,
    OperatorLikeLiteralFuzzyAdmissibilityAllowsOnlySingleNonSubstitutiveRepair) {
  const auto profile = detail::make_terminal_shape_from_literal("=>");
  detail::LiteralFuzzyCandidate candidate{
      .consumed = 2u,
      .distance = 1u,
      .operationCount = 1u,
      .cost = detail::make_recovery_cost(1u, 1u),
      .substitutionCount = 0u,
  };

  EXPECT_TRUE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, false));

  candidate.substitutionCount = 1u;
  EXPECT_FALSE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, false));

  candidate.substitutionCount = 0u;
  candidate.operationCount = 2u;
  EXPECT_FALSE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, false));

  candidate.operationCount = 1u;
  candidate.distance = 2u;
  EXPECT_FALSE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, false));
}

TEST(RecoveryTest,
     LongLiteralFuzzyAdmissibilityRequiresHalfConsumption) {
  // Pins the half-consumption guard: a low-consumption fuzzy match like
  // `re` -> `environment` survives the cost ceiling (length normalization keeps
  // its primaryRankCost at the limit) but consumes only 2 of 11 codepoints, so
  // it must be rejected; raising consumption to >= len/2 admits the same shape.
  // Only `consumed` differs between the two checks, so this isolates the floor.
  const auto profile = detail::make_terminal_shape_from_literal("environment");
  // 11 codepoints -> rank limit min(5, 11+3) = 5; half-length floor = 5.
  // ASCII fixture: consumedCodepoints == consumed; the floor reads
  // consumedCodepoints, so move them in lockstep across the two checks.
  detail::LiteralFuzzyCandidate candidate{
      .consumed = 2u,
      .consumedCodepoints = 2u,
      .distance = 9u,
      .operationCount = 9u,
      .cost = detail::make_recovery_cost(9u, 5u),
      .substitutionCount = 0u,
  };
  EXPECT_FALSE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, true));

  candidate.consumed = 5u;
  candidate.consumedCodepoints = 5u;
  EXPECT_TRUE(
      detail::allows_literal_fuzzy_candidate(candidate, profile, {}, true));
}

TEST(RecoveryTest,
     OperatorLikeTerminalAllowsNearbyDeleteScanAfterCompactHiddenGap) {
  auto skipper = SkipperBuilder().build();
  const auto matchArrow = [](const char *scanCursor) noexcept {
    return "=>"_kw.terminal(scanCursor);
  };

  auto hiddenGapHarness = pegium::test::makeCstBuilderHarness("xy=>");
  auto &hiddenGapBuilder = hiddenGapHarness.builder;
  detail::FailureHistoryRecorder hiddenGapRecorder(
      hiddenGapBuilder.input_begin());
  RecoveryContext hiddenGapCtx{hiddenGapBuilder, skipper, hiddenGapRecorder};
  hiddenGapCtx.skip();
  const detail::TerminalRecoveryFacts facts{
      .triviaGap = {.hasHiddenGap = true,
                    .visibleSourceAfterLocalSkip = false},
      .previousElementIsTerminalish = false,
  };
  EXPECT_TRUE(detail::probe_nearby_delete_scan_match(
      hiddenGapCtx, matchArrow, facts,
      detail::make_terminal_shape_from_literal("=>")));
}

TEST(RecoveryTest,
     SeparatorTerminalDisallowsNearbyDeleteScanAfterCompactHiddenGap) {
  auto skipper = SkipperBuilder().build();
  const auto matchSemicolon = [](const char *scanCursor) noexcept {
    return ";"_kw.terminal(scanCursor);
  };

  auto hiddenGapHarness = pegium::test::makeCstBuilderHarness("x;");
  auto &hiddenGapBuilder = hiddenGapHarness.builder;
  detail::FailureHistoryRecorder hiddenGapRecorder(
      hiddenGapBuilder.input_begin());
  RecoveryContext hiddenGapCtx{hiddenGapBuilder, skipper, hiddenGapRecorder};
  hiddenGapCtx.skip();
  const detail::TerminalRecoveryFacts facts{
      .triviaGap = {.hasHiddenGap = true,
                    .visibleSourceAfterLocalSkip = false},
      .previousElementIsTerminalish = false,
  };
  EXPECT_FALSE(detail::probe_nearby_delete_scan_match(
      hiddenGapCtx, matchSemicolon, facts,
      detail::make_terminal_shape_from_literal(";")));
}

TEST(RecoveryTest,
     TerminalRecoveryFactsRestrictDeleteScanBetweenAdjacentTerminals) {
  auto skipper = SkipperBuilder().build();
  const auto matchArrow = [](const char *scanCursor) noexcept {
    return "=>"_kw.terminal(scanCursor);
  };
  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("xy=>");
    auto &builder = builderHarness.builder;
    detail::FailureHistoryRecorder recorder(builder.input_begin());
    RecoveryContext ctx{builder, skipper, recorder};
    ctx.skip();
    EXPECT_TRUE(detail::probe_nearby_delete_scan_match(ctx, matchArrow));
  }

  auto restrictedHarness = pegium::test::makeCstBuilderHarness("xy=>");
  auto &restrictedBuilder = restrictedHarness.builder;
  detail::FailureHistoryRecorder restrictedRecorder(
      restrictedBuilder.input_begin());
  RecoveryContext restrictedCtx{restrictedBuilder, skipper, restrictedRecorder};
  restrictedCtx.skip();
  const detail::TerminalRecoveryFacts facts{
      .triviaGap = {.hasHiddenGap = false,
                    .visibleSourceAfterLocalSkip = false},
      .previousElementIsTerminalish = true,
  };
  EXPECT_FALSE(
      detail::probe_nearby_delete_scan_match(restrictedCtx, matchArrow, facts));
}

TEST(RecoveryTest, SymbolicLiteralOnlyAllowsSingleNonSubstitutiveFuzzyRepair) {
  const auto skipper = SkipperBuilder().build();
  DataTypeRule<std::string> arrow{"Arrow", "=>"_kw};

  const auto missingCharacter = parseDataType(arrow, ">", skipper);
  ASSERT_TRUE(missingCharacter.value);
  EXPECT_TRUE(missingCharacter.fullMatch);
  EXPECT_TRUE(std::ranges::any_of(
      missingCharacter.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Replaced;
      }));

  const auto substitutedCharacter = parseDataType(arrow, "->", skipper);
  EXPECT_FALSE(substitutedCharacter.value);
  ASSERT_FALSE(substitutedCharacter.parseDiagnostics.empty());
  EXPECT_EQ(substitutedCharacter.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Incomplete);
}

TEST(RecoveryTest,
     ParserRuleCanRecoverByGenericDeletePrefixBeforeLowConfidenceReplace) {
  const std::string input = "xxxxxxxxxxxxxxxxxservice";
  const auto skipper = SkipperBuilder().build();

  ParserRule<RecoveryNode> rule{"ParserRule",
                                assign<&RecoveryNode::token>("service"_kw)};
  ParseOptions constrainedOptions;
  constrainedOptions.maxRecoveryEditCost = 64;
  const auto constrainedResult =
      parseRule(rule, input, skipper, constrainedOptions);
  ASSERT_TRUE(constrainedResult.value);
  ASSERT_FALSE(constrainedResult.parseDiagnostics.empty());
  EXPECT_EQ(constrainedResult.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Deleted);

  ParseOptions tunedOptions;
  tunedOptions.maxRecoveryEditCost = 128;
  const auto tunedResult = parseRule(rule, input, skipper, tunedOptions);
  EXPECT_TRUE(tunedResult.value);
  EXPECT_FALSE(tunedResult.parseDiagnostics.empty());
  auto *typed = pegium::ast_ptr_cast<RecoveryNode>(tunedResult.value);
  ASSERT_TRUE(typed != nullptr);
  EXPECT_EQ(typed->token, "service");
}

TEST(RecoveryTest, MissingRequiredExpressionBeforeDelimiterLeavesHole) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> expression{
      "Expression", create<RecoveryNumberExpression>() +
                            assign<&RecoveryNumberExpression::value>(number) |
                        create<RecoveryReferenceExpression>() +
                            assign<&RecoveryReferenceExpression::name>(id)};
  ParserRule<RecoveryDefinitionWithExpr> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithExpr::name>(id) + ":"_kw +
          assign<&RecoveryDefinitionWithExpr::expr>(expression) + ";"_kw};

  const auto result = parseRule(definition, "def a :;", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  auto *parsedDefinition =
      dynamic_cast<RecoveryDefinitionWithExpr *>(result.value);
  ASSERT_NE(parsedDefinition, nullptr);
  EXPECT_EQ(parsedDefinition->name, "a");
  EXPECT_EQ(parsedDefinition->expr, nullptr);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      }));
}

TEST(RecoveryTest, RepetitionAllowsInsertRetryAfterRewoundProgressAtEof) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();

  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  TerminalRule<int> number{"NUMBER", some(d)};
  ParserRule<RecoveryExpression> primary{
      "Primary", create<RecoveryNumberExpression>() +
                     assign<&RecoveryNumberExpression::value>(number)};
  InfixRule<RecoveryBinaryExpression, &RecoveryBinaryExpression::left,
            &RecoveryBinaryExpression::op, &RecoveryBinaryExpression::right>
      expression{"Expression", primary, LeftAssociation("+"_kw)};
  ParserRule<RecoveryExpression> expressionRule{"Expression", expression};
  ParserRule<RecoveryDefinitionWithExpr> definition{
      "Definition",
      "def"_kw + assign<&RecoveryDefinitionWithExpr::name>(id) + ":"_kw +
          assign<&RecoveryDefinitionWithExpr::expr>(expressionRule) + ";"_kw};
  ParserRule<RecoveryModule> module{
      "Module", "module"_kw + assign<&RecoveryModule::name>(id) +
                    many(append<&RecoveryModule::statements>(definition))};

  const auto result =
      parseRule(module, "module demo\n\ndef a : 3  +5\n", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  ASSERT_FALSE(result.parseDiagnostics.empty());
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      }));

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  ASSERT_EQ(parsedModule->statements.size(), 1u);

  auto *parsedDefinition = dynamic_cast<RecoveryDefinitionWithExpr *>(
      parsedModule->statements.front());
  ASSERT_NE(parsedDefinition, nullptr);
  EXPECT_EQ(parsedDefinition->name, "a");

  auto *binary =
      dynamic_cast<RecoveryBinaryExpression *>(parsedDefinition->expr);
  ASSERT_NE(binary, nullptr);
  EXPECT_EQ(binary->op, "+");
  auto *left = dynamic_cast<RecoveryNumberExpression *>(binary->left);
  auto *right = dynamic_cast<RecoveryNumberExpression *>(binary->right);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(left->value, 3);
  EXPECT_EQ(right->value, 5);
}

TEST(RecoveryTest,
     OptionalBranchDoesNotEmitSpuriousInsertionsWhenSuffixMatches) {
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

  const auto result = parseRule(definition, "def a :;", skipper);

  ASSERT_TRUE(result.value);
  ASSERT_EQ(result.parseDiagnostics.size(), 1u);
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Inserted);
  const auto *expectedElement = result.parseDiagnostics.front().element;
  ASSERT_NE(expectedElement, nullptr);
  if (expectedElement->getKind() == pegium::grammar::ElementKind::Assignment) {
    expectedElement =
        static_cast<const pegium::grammar::Assignment *>(expectedElement)
            ->getElement();
  }
  const auto *expectedRule =
      dynamic_cast<const pegium::grammar::AbstractRule *>(expectedElement);
  ASSERT_NE(expectedRule, nullptr);
  EXPECT_EQ(expectedRule->getName(), "Expression");
}

TEST(RecoveryTest,
     OptionalBranchDoesNotInventMissingPrefixBeforeDelimiterRecovery) {
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
      "Module", some(append<&RecoveryModule::statements>(statement))};

  const auto result = parseRule(module, "def a: 5\ndef b: 3;", skipper);

  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      }));
  EXPECT_FALSE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Deleted;
      }));

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr);
  ASSERT_EQ(parsedModule->statements.size(), 2u);

  auto *firstDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[0]);
  auto *secondDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[1]);
  ASSERT_NE(firstDefinition, nullptr);
  ASSERT_NE(secondDefinition, nullptr);
  EXPECT_EQ(firstDefinition->name, "a");
  EXPECT_EQ(secondDefinition->name, "b");
}

TEST(RecoveryTest,
     OptionalBranchDoesNotBeatMissingDelimiterInsertInFollowingSequenceTail) {
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
      "Module", some(append<&RecoveryModule::statements>(definition))};

  const auto result =
      parseRule(module, "def a 5;\ndef b: 3;\ndef c: b;", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      })) << parseDump;

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr) << parseDump;
  ASSERT_EQ(parsedModule->statements.size(), 3u) << parseDump;

  auto *firstDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[0]);
  ASSERT_NE(firstDefinition, nullptr) << parseDump;
  EXPECT_EQ(firstDefinition->name, "a") << parseDump;
  ASSERT_NE(firstDefinition->expr, nullptr) << parseDump;
  auto *firstNumber =
      dynamic_cast<RecoveryNumberExpression *>(firstDefinition->expr);
  ASSERT_NE(firstNumber, nullptr) << parseDump;
  EXPECT_EQ(firstNumber->value, 5) << parseDump;
}

TEST(RecoveryTest,
     MissingDelimiterInsertStillWinsInsideStatementChoiceSequence) {
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
      "Module", some(append<&RecoveryModule::statements>(statement))};

  const auto result =
      parseRule(module, "def a 5;\ndef b: 3;\nb;", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      result.parseDiagnostics, [](const ParseDiagnostic &diagnostic) {
        return diagnostic.kind == ParseDiagnosticKind::Inserted;
      })) << parseDump;

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr) << parseDump;
  ASSERT_EQ(parsedModule->statements.size(), 3u) << parseDump;

  auto *firstDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[0]);
  auto *secondDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[1]);
  auto *thirdEvaluation = dynamic_cast<RecoveryExpressionEvaluation *>(
      parsedModule->statements[2]);
  ASSERT_NE(firstDefinition, nullptr) << parseDump;
  ASSERT_NE(secondDefinition, nullptr) << parseDump;
  ASSERT_NE(thirdEvaluation, nullptr) << parseDump;
  EXPECT_EQ(firstDefinition->name, "a") << parseDump;
  EXPECT_EQ(secondDefinition->name, "b") << parseDump;
  auto *firstNumber =
      dynamic_cast<RecoveryNumberExpression *>(firstDefinition->expr);
  ASSERT_NE(firstNumber, nullptr) << parseDump;
  EXPECT_EQ(firstNumber->value, 5) << parseDump;
}

TEST(RecoveryTest,
     MissingDelimiterInsertStillWinsInsideModuleWithStatementChoice) {
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

  const auto result =
      parseRule(module, "module m\ndef a 5;\ndef b: 3;\nb;", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr) << parseDump;
  ASSERT_EQ(parsedModule->statements.size(), 3u) << parseDump;

  auto *firstDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[0]);
  ASSERT_NE(firstDefinition, nullptr) << parseDump;
  auto *firstNumber =
      dynamic_cast<RecoveryNumberExpression *>(firstDefinition->expr);
  ASSERT_NE(firstNumber, nullptr) << parseDump;
  EXPECT_EQ(firstNumber->value, 5) << parseDump;
}

TEST(RecoveryTest,
     MissingDelimiterInsertStillWinsInsideModuleWithDefinitionRepetition) {
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

  const auto result =
      parseRule(module, "module m\ndef a 5;\ndef b: 3;\ndef c: b;", skipper);
  const auto parseDump = dump_parse_diagnostics(result.parseDiagnostics);

  ASSERT_TRUE(result.value) << parseDump;
  EXPECT_TRUE(result.fullMatch) << parseDump;

  auto *parsedModule = dynamic_cast<RecoveryModule *>(result.value);
  ASSERT_NE(parsedModule, nullptr) << parseDump;
  ASSERT_EQ(parsedModule->statements.size(), 3u) << parseDump;

  auto *firstDefinition = dynamic_cast<RecoveryDefinitionWithOptionalArgs *>(
      parsedModule->statements[0]);
  ASSERT_NE(firstDefinition, nullptr) << parseDump;
  auto *firstNumber =
      dynamic_cast<RecoveryNumberExpression *>(firstDefinition->expr);
  ASSERT_NE(firstNumber, nullptr) << parseDump;
  EXPECT_EQ(firstNumber->value, 5) << parseDump;
}
