if(NOT CMAKE_CROSSCOMPILING)

  set(_refusal_libraries cursivCore cursivGen cursivLrGen cursivLlGen)
  if(TARGET cursivAllocator)
    list(APPEND _refusal_libraries cursivAllocator)
  endif()
  add_custom_target(refusal_dependencies ALL DEPENDS ${_refusal_libraries})
  export(TARGETS ${_refusal_libraries}
      FILE ${CMAKE_CURRENT_BINARY_DIR}/refusals/engines.cmake)

  get_directory_property(_refusal_options COMPILE_OPTIONS)
  get_directory_property(_refusal_definitions COMPILE_DEFINITIONS)
  string(CONCAT _refusal_settings
      "set(CURSIV_REFUSAL_ROOT [==[${CURSIV_SOURCE_ROOT}]==])\n"
      "set(CURSIV_REFUSAL_COMPILE_OPTIONS [==[${_refusal_options}]==])\n"
      "set(CURSIV_REFUSAL_COMPILE_DEFINITIONS [==[${_refusal_definitions}]==])\n")
  set(_refusal_variables
      CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT
      CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ARG1
      CMAKE_CXX_COMPILER_TARGET CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
      CMAKE_CXX_COMPILER_LAUNCHER CMAKE_CXX_LINKER_LAUNCHER
      CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_CXX_EXTENSIONS
      CMAKE_COMPILE_WARNING_AS_ERROR
      CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS
      CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET)
  foreach(_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel
                          ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${_config}" _config)
    list(APPEND _refusal_variables CMAKE_CXX_FLAGS_${_config}
                                  CMAKE_EXE_LINKER_FLAGS_${_config})
  endforeach()
  list(REMOVE_DUPLICATES _refusal_variables)
  foreach(_variable IN LISTS _refusal_variables)
    if(DEFINED ${_variable})
      string(APPEND _refusal_settings
          "set(${_variable} [==[${${_variable}}]==])\n")
    endif()
  endforeach()
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/refusals/settings.cmake
      CONTENT "${_refusal_settings}" @ONLY)

  function(cursiv_refusal name expected)
    set(engines ${ARGN})
    # Grammar type checks and lexical validation are shared by both backends.
    # Exercise them once; backend lowering contracts explicitly name LR LL.
    if(NOT engines)
      set(engines LR)
    endif()
    foreach(engine IN LISTS engines)
      set(lane ${name})
      if(engine STREQUAL "LL")
        set(lane ${name}_ll)
      endif()
      add_test(NAME refusal_${lane} COMMAND ${CMAKE_COMMAND}
          "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/refusals"
          "-DPARENT=${CMAKE_CURRENT_BINARY_DIR}"
          "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/refusals/${lane}"
          "-DCASE=${name}"
          "-DLANE=${lane}"
          "-DENGINE=${engine}"
          "-DCONFIG=$<CONFIG>"
          "-DBUILD_GENERATOR=${CMAKE_GENERATOR}"
          "-DEXPECTED=${expected}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/refusals/run.cmake)
    endforeach()
  endfunction()

  cursiv_refusal(empty_terminal
      "terminal .MaybeNum. matches the empty string" LR LL)
  cursiv_refusal(recognized_empty_terminal
      "terminal .NOISE. matches the empty string")
  cursiv_refusal(nullable_repetition
      "refused .nullableRepetition..*rule .Root. repeats an item that may match nothing" LR)
  # The same refusal from the other side: an item that holds no position at all
  # matches nothing whatever the text says, and names no symbol to repeat.
  cursiv_refusal(zero_width_repetition
      "refused .nullableRepetition..*rule .Root. repeats an item that may match nothing" LR LL)
  # The one lane that pins both forms: the sentence names the rule, then what
  # its author wrote, then the right-hand sides it became, then the cycle.
  cursiv_refusal(unit_cycle
      "refused .unitProductionCycle..*rule .A. stands on a cycle of reductions that read no text.*cycle: .A. -> .B. -> .A." LR)
  cursiv_refusal(lexical_tie
      "candidates mode=[0-9]+ .(STARS|BOLD). -> .(STARS|BOLD). on .[*][*]." LR)
  cursiv_refusal(indent_newline_rule
      "`indent_newline` names the terminal that ends a line" LR)
  cursiv_refusal(indent_open_rule
      "`indent_open` and `indent_close` name a terminal a rule reads" LR)
  cursiv_refusal(indent_twice
      "refused: terminal .OPEN. declares two indent terminals, or two dedent terminals")
  foreach(_case IN ITEMS unordered_heads unordered_head_nest)
    cursiv_refusal(${_case}
        "this alternative can create twice")
  endforeach()
  foreach(_case IN ITEMS optional_adoption
                         optional_adoption_branch)
    cursiv_refusal(${_case}
        "this alternative hands over a node the text may leave out")
  endforeach()
  cursiv_refusal(unordered_receiver
      "this directive writes a member of a class the node in hand may not be")
  cursiv_refusal(unordered_write_order
      "[ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Node::name")
  cursiv_refusal(unordered_one_item
      "an unordered group needs at least two items: .unordered.a, b.")
  cursiv_refusal(levels_of_an_operator
      "levels.[.][.][.]. lists the levels of a table, tightest-binding first")
  cursiv_refusal(nullable_repetition_inner
      "refused .nullableRepetition..*rule .X. repeats an item that may match nothing.*X returns Pair" LR LL)
  cursiv_refusal(hidden_left_recursion
      "refused .hiddenLeftRecursion..*rule .Type. reaches itself at its left edge behind an item that may match nothing" LR)
  cursiv_refusal(hidden_left_recursion_gated
      "refused .hiddenLeftRecursion..*rule .Type. reaches itself at its left edge behind more than 7 optional items" LR)
  cursiv_refusal(hidden_left_recursion_behind
      "refused .hiddenLeftRecursion..*rule .Type. reaches itself at its left edge behind more than 7 optional items" LR)
  cursiv_refusal(hidden_left_recursion_edges
      "refused .hiddenLeftRecursion..*rule .Type. reaches itself at its left edge behind more than 7 optional items" LR)
  cursiv_refusal(hidden_left_recursion_condition
      "refused .hiddenLeftRecursion..*rule .X. reaches itself at its left edge behind an item that may match nothing" LR)
  cursiv_refusal(rule_without_production
      "refused .ruleWithoutProduction..*rule .Tail. derives nothing" LR LL)
  cursiv_refusal(unproductive_rule
      "refused .ruleWithoutProduction..*rule .Loop. derives nothing" LR)
  cursiv_refusal(unproductive_tail
      "refused .ruleWithoutProduction..*rule .Loop. derives nothing" LR LL)
  # A branch that reads past `eof` beside one that stands: a warning, pinned
  # the same way.
  cursiv_refusal(dead_branch
      "warning: rule .Para.: branch 2 of 2 .BAR !. SEMI. is never read")
  # The grammars one engine refuses and the other generates: a lane pins the
  # refusal, and the sentence of the generator that goes through.
  cursiv_refusal(boundary_disagrees
      "refused: terminal .T. reaches one match through two refusals of different classes" LR)
  cursiv_refusal(boundary_disagrees
      "refusal_boundary_disagrees_ll: terms=" LL)
  # A cycle through a rule called from outside: both engines read it.
  cursiv_refusal(left_recursion_indirect
      "refusal_left_recursion_indirect_ll: terms=" LL)
  cursiv_refusal(left_recursion_indirect
      "refusal_left_recursion_indirect: states=" LR)
  # A call inside a repetition: LR reads it, the descent refuses it.
  cursiv_refusal(left_recursion_repeated
      "refusal_left_recursion_repeated: states=" LR)
  cursiv_refusal(left_recursion_repeated
      "refused .leftRecursion..*rule .Path. reaches itself before matching anything inside a repetition" LL)
  # ...and the grammars both refuse, each in its own words.
  cursiv_refusal(unit_cycle
      "refused .leftRecursion..*rule .A. reaches itself before matching anything.*cycle: .A. -> .B. -> .A." LL)
  cursiv_refusal(hidden_left_recursion
      "refused .leftRecursion..*rule .Type. reaches itself before matching anything" LL)
  cursiv_refusal(hidden_left_recursion_gated
      "refused .leftRecursion..*rule .Type. reaches itself before matching anything inside a repetition or behind optional items.*at most 7 optional items" LL)
  cursiv_refusal(hidden_left_recursion_behind
      "refused .leftRecursion..*rule .Type. reaches itself before matching anything inside a repetition or behind optional items.*at most 7 optional items" LL)
  cursiv_refusal(hidden_left_recursion_edges
      "refused .leftRecursion..*rule .Type. reaches itself before matching anything" LL)
  cursiv_refusal(hidden_left_recursion_condition
      "refused .leftRecursion..*rule .X. reaches itself before matching anything" LL)
  cursiv_refusal(skip_configurations
      "refused: the grammar reads under more than 255 skip configurations" LR LL)
  # The widest groups both engines spell, and the next one wider.
  foreach(_case IN ITEMS unordered_eleven unordered_nests_six)
    cursiv_refusal(${_case}
        "refusal_${_case}: states=" LR)
    cursiv_refusal(${_case}
        "refusal_${_case}_ll: terms=" LL)
  endforeach()
  foreach(_case IN ITEMS unordered_twelve unordered_nests)
    cursiv_refusal(${_case}
        "refused: rule .Root. holds an unordered group of too many items" LR LL)
  endforeach()
  # An item that may read nothing and is no optional item, refused by both
  # engines in one sentence that names it...
  cursiv_refusal(unordered_empty_item
      "refused .nullableUnorderedItem..*rule .Root. holds an unordered item that may match nothing, .\"final\"[?] \"static\"[?].: an unordered item must read input" LR LL)
  # ...and an optional item in a group that holds a nest.
  cursiv_refusal(unordered_optional_nest
      "refused .nullableUnorderedItem..*rule .Root. holds an optional unordered item, .*Wrap.inner=.*, in a group whose items hold a .nest." LR LL)
  cursiv_refusal(repeat_too_many
      "refused: the grammar spells a right-hand side of more than 65 535 positions" LR)
  cursiv_refusal(repeat_too_many
      "refused: rule .Root. lowers to more than 65 535 positions" LL)
  # A counted tail of nest turns, one block deeper each in the LL source.
  cursiv_refusal(nest_turns_too_many
      "refused: rule .Root. repeats a .nest. more than 64 times in a row" LL)
  # The graph counts a state per subset of the conditions read under a token.
  cursiv_refusal(conditions_state_index
      "refused by the 16-bit index of an LR state, counted before minimisation" LR)
  cursiv_refusal(conditions_per_token
      "refused by the conditions one token is read under" LR)
  cursiv_refusal(skipped_terminal
      "refused .skippedTerminal..*rule .Root. names a terminal its reading skips as trivia" LR LL)
  cursiv_refusal(skipped_terminal_shared
      "refused .skippedTerminal..*rule .Pair. names a terminal its reading skips as trivia" LR)
  cursiv_refusal(skipped_terminal_operator
      "refused .skippedTerminal..*rule .Plus. names a terminal its reading skips as trivia" LR)
  cursiv_refusal(skipped_terminal_operator
      "refused .skippedTerminal..*rule .Sum. names a terminal its reading skips as trivia" LL)
  cursiv_refusal(trivia_tied
      "refused: terminal .(WS|NOTE). is trivia that reads the same text as another trivia terminal")
  cursiv_refusal(trivia_tied_guarded
      "refused: terminal .NOTE. is trivia that reads the same text as another trivia terminal")
  cursiv_refusal(trivia_tied_nested
      "refused: terminal .NOTE. is trivia that reads the same text as another trivia terminal")
  # Two trivia the check cannot follow past a capture: a warning from both
  # generators, and the grammar stands.
  cursiv_refusal(trivia_tied_unsure
      "warning: terminals .LONG. and .TAG. are trivia, and the check that they never read the same text at the same length and rank could not be completed" LR LL)
  # Nor past an assertion it does not read, whether the trivia tie where it
  # holds or never do, nor past too many assertions at one position.
  cursiv_refusal(trivia_tied_asserted
      "warning: terminals .(NOTE|SKIP). and .(NOTE|SKIP). are trivia, and the check that they never read the same text at the same length and rank could not be completed")
  cursiv_refusal(trivia_tied_apart
      "warning: terminals .(MARK|NOTE). and .(MARK|NOTE). are trivia, and the check that they never read the same text at the same length and rank could not be completed")
  cursiv_refusal(trivia_tied_many_assertions
      "warning: terminals .(WS|NOTE). and .(WS|NOTE). are trivia, and the check that they never read the same text at the same length and rank could not be completed")
  cursiv_refusal(doc_conflict
      "refused .docConflict..*rule .Root. documents one word twice with different prose")
  # An alternative, or an optional item, that reads what an earlier one reads:
  # a warning from both generators, and the twin is dropped.
  cursiv_refusal(twin_branch
      "warning: rule .Root.: branch 2 of 2 .b=ID. reads the same tokens as branch 1, so it is never chosen and is dropped" LR LL)
  cursiv_refusal(twin_item_gated
      "warning: rule .Root.: ..b=ID... reads the same tokens as the earlier ..a=ID..., so reading it in place of that item is never chosen and is dropped" LR LL)
  cursiv_refusal(optional_condition
      "refused .optionalCondition..*rule .Root. makes a condition optional" LR LL)
  cursiv_refusal(optional_end_mark
      "refused .optionalEndMark..*rule .Root. makes an end-of-input assertion optional" LR LL)
  cursiv_refusal(two_end_marks
      "refused .twoEndMarks..*rule .Root. asserts end of input twice" LR LL)
  cursiv_refusal(gap_shape
      "refused: the grammar asks a condition of an expression the matcher does not read" LR LL)
  cursiv_refusal(gap_too_many
      "states more than 255 gap conditions, or more implied tokens than a reading holds a bit for" LR)
  cursiv_refusal(region_boundary_shared
      "refused: the grammar reaches one point through a .* region and outside it at once" LR)
  # The same where a called rule that may read nothing ends the region.
  cursiv_refusal(region_called_tail
      "refused: the grammar reaches one point through a .* region and outside it at once" LR LL)
  # ...and where that rule ends on a condition, which reads past the region.
  cursiv_refusal(region_called_tail_cond
      "refused: the grammar reaches one point through a .* region and outside it at once" LR LL)
  # A region whose last part may go on or leave, the end of input behind it.
  cursiv_refusal(region_counted_tail
      "refused: the grammar reaches one point through a .* region and outside it at once" LR LL)
  # The same point where the region names trivia of its own, a terminal its
  # caller reads as a token: the one limit a widened region has.
  cursiv_refusal(region_trivia_shared
      "refused: the grammar reaches one point through a .* region and outside it at once" LR LL)
  # A grammar named bare, a section a point reads inside and outside at once,
  # one closed by a token its grammar reads, and an indented grammar embedded.
  cursiv_refusal(section_bare
      "`ref<G>` names a grammar: read it with `section<G>")
  cursiv_refusal(section_in_condition
      "`section<G>.open, close.` stands in a rule, not in a condition")
  cursiv_refusal(section_in_terminal
      "`section<G>.open, close.` stands in a rule, not in a terminal")
  cursiv_refusal(section_point_shared
      "refused: the grammar reaches one point inside a .section<>. and outside it at once" LR LL)
  cursiv_refusal(section_end_shared
      "refused: the grammar closes a .section<>. on a token the embedded grammar reads" LR LL)
  # A closing token the check meets only where an assertion holds, or beside
  # a terminal that calls itself, whether or not they meet: a warning, and the
  # grammar stands.
  cursiv_refusal(section_end_asserted
      "warning: terminal ... closes a .section<>., and the check that ...., a terminal its grammar reads, neither reads it nor begins with it could not be completed" LR LL)
  cursiv_refusal(section_end_recursive
      "warning: terminal .END. closes a .section<>., and the check that .Inner::REGION., a terminal its grammar reads, neither reads it nor begins with it could not be completed" LR LL)
  cursiv_refusal(section_end_recognized
      "warning: terminal .a!. closes a .section<>., and the check that .Inner::RUN., a terminal its grammar reads, neither reads it nor begins with it could not be completed")
  cursiv_refusal(section_indent
      "a grammar embedded with .section<>. reads no indentation" LR)
  cursiv_refusal(rescue_scalars
      "a character past ASCII rescues that word" LR)
  cursiv_refusal(section_rescue_scalars
      "a character past ASCII rescues that word")
  cursiv_refusal(section_indent_terminal
      "a grammar embedded with .section<>. reads no indentation" LR)
  cursiv_refusal(section_indent_brackets
      "a grammar embedded with .section<>. reads no indentation" LR)
  cursiv_refusal(section_indent_inline
      "refused: the grammar reads its lines by indentation and embeds a grammar with .section<>." LR)
  cursiv_refusal(section_indent_caller
      "refused: the grammar reads its lines by indentation and embeds a grammar with .section<>." LR LL)
  cursiv_refusal(precedence_clash
      "refused .precedenceClash..*rule .Sum. names the same operator text on two levels" LR LL)
  cursiv_refusal(precedence_clash_opening
      "refused .precedenceClash..*rule .Sum. names the same operator text on two levels" LR LL)
  cursiv_refusal(precedence_clash_sequence
      "refused .precedenceClash..*rule .Sum. names the same operator text on two levels" LR LL)
  cursiv_refusal(prefix_precedence_clash
      "refused .precedenceClash..*rule .Sum. names the same operator text on two levels")
  cursiv_refusal(postfix_binary_clash
      "refused .postfixBinaryClash..*rule .Sum. reads .!. as a postfix operator on level 1 and as a binary operator on level 3" LR LL)
  # A text the check cannot list may be shared: refused alike.
  cursiv_refusal(postfix_binary_clash_unlisted
      "refused .postfixBinaryClash..*rule .Sum. cannot compare the text of the postfix operator on level 1 with the binary operator on level 2" LR LL)
  # An infix body carries no directive, so nothing else can refuse an
  # operand the climb never fills.
  cursiv_refusal(infix_operand_mismatch
      "this member holds a node of its own type")
  cursiv_refusal(infix_member_count
      "an infix rule names zero or three members")
  cursiv_refusal(infix_operator_not_text
      "refused .infixOperatorNotText..*rule .Sum. names as an operator a rule that builds a node" LR LL)
  cursiv_refusal(infix_operator_sequence_node
      "refused .infixOperatorNotText..*rule .Sum. names as an operator a rule that builds a node" LR LL)
  cursiv_refusal(infix_operator_scoped_condition
      "refused .infixOperatorNotText..*rule .Sum. names as an operator a rule that builds a node" LR LL)
  cursiv_refusal(infix_operator_not_enumerator
      "[ '‘]EnumLiteral.*refuse::ast::Bin::op.*[+].*refuse::Op::add.*refuse::Op::mul")
  cursiv_refusal(infix_operator_sequence_not_enumerator
      "[ '‘]EnumLiteral.*refuse::ast::Bin::op.*NOTIN.*refuse::Op::add.*refuse::Op::mul")
  cursiv_refusal(enum_outside_probe_window
      "this literal can never convert.*CURSIV_ENUM_PROBE_MAX")
  cursiv_refusal(bool_literal
      "[ '‘]BoolLiteral.*refuse::ast::Flag::enabled.*!")
  cursiv_refusal(rescued_refusal_past_match
      "refused: terminal .W. ends a match exactly where the word it refuses ends, past its head, and ._. rescues that word")
  cursiv_refusal(rescue_reads_past
      "refused: terminal .NAME. reads on past a word it refuses over a character its word boundary does not hold")
  cursiv_refusal(boundary_misses
      "refused: terminal .NAME. refuses .\"in\". at its head, and reads .\\\\. where a word of its own begins")
  cursiv_refusal(word_past_match
      "refused: terminal .HASH. may end a match short of .'abc'., a word it refuses past its head")
  cursiv_refusal(word_refused_in_loop
      "refused: terminal .STARS. refuses a word inside a repetition that may come back to a refusal without reading a byte")
  foreach(_case IN ITEMS terminal_cycle terminal_nullable_cycle terminal_assertion_cycle)
    cursiv_refusal(${_case}
        "refused:.*calls itself without consuming input")
  endforeach()
  cursiv_refusal(terminal_unproductive
      "refused:.*has a recursive call that cannot return a finite match")
  cursiv_refusal(terminal_recursive_empty
      "terminal .EMPTY. matches the empty string")
  cursiv_refusal(terminal_nullable_repetition
      "refused: terminal .REST. repeats an item that may match nothing inside a recursive terminal")
  foreach(_case IN ITEMS terminal_reads_rule terminal_reads_data_rule)
    cursiv_refusal(${_case}
        "refused: the grammar reads the rule .(Pair|Dotted).: a rule is read with the skipper between its parts")
  endforeach()
  cursiv_refusal(class_past_match
      "refused: terminal .NUM. may end a match short of .\\\\p{Lo}., a word it refuses past its head")
  cursiv_refusal(refusal_into_relay
      "refused: terminal .CMT. refuses a word that runs into a relay's content")
  cursiv_refusal(echo_unmarked
      "refused: terminal .TAG. echoes a mark<> not set in its scope")
  foreach(_case IN ITEMS echo_mark_one_arm echo_mark_in_loop)
    cursiv_refusal(${_case}
        "refused: terminal .TAG. echoes a mark<> not set in its scope")
  endforeach()
  cursiv_refusal(recursive_echo_unmarked
      "refused: terminal .MARKED. echoes a mark<> not set in its scope")
  cursiv_refusal(recursive_token_echo_unmarked
      "refused: terminal .MARKED. echoes a mark<> not set in its scope")
  foreach(_case IN ITEMS capture_call_scope capture_guard_scope)
    cursiv_refusal(${_case}
        "refused: terminal .OUTER. echoes a mark<> not set in its scope")
  endforeach()
  cursiv_refusal(positive_lookahead_ref
      "A rule that builds a node is parsed, not read: no condition asks one")
  cursiv_refusal(negated_rule_lookahead
      "A rule that builds a node is parsed, not read: no condition asks one")
  cursiv_refusal(condition_rule_recursive
      "a condition reads a data rule that reaches itself")
  cursiv_refusal(commit_terminal
      "a commit stands in a rule, not in a terminal")
  cursiv_refusal(as_nested
      "as<X>.as<Y>.e.. is refused: compose the two conversions in one function")
  cursiv_refusal(as_generic_function
      "as<f>.e.: f accepts more than one input")
  cursiv_refusal(as_inside_terminal
      "inside a terminal, as<X>.e. wraps the whole body or each of its alternatives")
  foreach(_case IN ITEMS as_branch_partial as_branch_and_body)
    cursiv_refusal(${_case}
        "inside a terminal, as<X>.e. wraps the whole body or each of its alternatives")
  endforeach()
  cursiv_refusal(as_branch_type
      "inside a terminal, as<V>.e. on an alternative names a constant")
  cursiv_refusal(as_branch_pattern
      "inside a terminal, an alternative with its own as<V>.e. reads words only")
  cursiv_refusal(as_branch_types
      "inside a terminal, the constants of the alternatives. as<V>.e. have different types")
  cursiv_refusal(as_over_directive
      "as<X>.e. converts text, and this e writes the AST")
  cursiv_refusal(as_over_node
      "as<X>.e. gives a value to text, and this e builds an AST node")
  cursiv_refusal(data_rule_mixed_values
      "the alternatives of this data rule pass values of different types")
  foreach(_case IN ITEMS commit_outside commit_choice)
    cursiv_refusal(${_case}
        "commit.prefix, body. stands only as the direct item of option, many, some or repeat")
  endforeach()
  cursiv_refusal(commit_empty_prefix
      "rule .Root. commits on a prefix that may read nothing")
  cursiv_refusal(commit_region_tail
      "refused: the grammar reaches one point through a .* region and outside it at once" LR LL)
  # The token graph refuses these for both engines alike.
  cursiv_refusal(through_reads_rule
      "a rule is read with the skipper between")
  cursiv_refusal(through_directive
      "sits inside a through")
  cursiv_refusal(through_echo_unmarked
      "echoes a mark<> not set in its scope")
  cursiv_refusal(nonterminal_ignore
      ".ignored., .hidden. and .skip... list terminals only")
  cursiv_refusal(nonterminal_hidden
      ".ignored., .hidden. and .skip... list terminals only")
  cursiv_refusal(trivia_in_both_lists
      "belongs to at most one of .ignored. and .hidden.")
  cursiv_refusal(class_not_scalar
      "names a value that is no scalar" LR)
  cursiv_refusal(class_unknown_escape
      "holds an escape std::regex does not read in a class" LR)
  cursiv_refusal(class_range_of_class
      "puts a class escape .* at an end of a range" LR)
  cursiv_refusal(class_lone_backslash
      "ends on a lone backslash" LR)
  cursiv_refusal(byte_complement
      "byte. is one raw byte, not a class, so it has no complement" LR)
  cursiv_refusal(byte_joins_class
      "byte. is one raw byte, not a class, so no class joins it" LR)
  cursiv_refusal(any_complement
      "this complement holds no character" LR)
  foreach(_case IN ITEMS implied_token_terminal implied_token_guarded)
    cursiv_refusal(${_case}
        "implied.tok, cond.. lays a token of no width, so .tok. is an unguarded literal" LR LL)
  endforeach()
  cursiv_refusal(word_boundary_two_parents
      "two parents of this grammar declare different" LR)
  cursiv_refusal(override_other_family
      "this override hands back a class outside the type of the rule it stands over" LR)
  cursiv_refusal(override_node_to_data
      "an override restates the rule's genre and this one changes it" LR)
  # The ones the type system carries: a member's own shape, which no walk of the
  # grammar can hold as a value -- the generator does not compile.
  cursiv_refusal(member_owns_text
      "an AST member never owns its text" LR)
  cursiv_refusal(node_list_by_value
      "an AST list member holds nodes by pointer" LR)
  # ...and the other side of the same law: an item that takes no position at
  # all has no record for the write to ride.
  cursiv_refusal(directive_on_no_position
      "refused .directiveOnNoPosition..*writes a member from `eof` alone, or from nothing" LR LL)
  cursiv_refusal(directive_over_no_payload
      "stands over a shape that produces no value")
  cursiv_refusal(directive_over_no_payload_alternative
      "stands over a shape that produces no value")
  cursiv_refusal(directive_node_into_value
      "stands over a shape that builds a node, and the member takes a value")
  cursiv_refusal(directive_node_quantifier_into_value
      "stands over a shape that builds a node, and the member takes a value")
  cursiv_refusal(directive_sequence_not_enumerator
      "[ '‘]EnumLiteral.*refuse::ast::Bin::op.*NOTIN.*refuse::Op::add.*refuse::Op::mul")
  cursiv_refusal(directive_over_sequence_values
      "only a node travels through one")
  cursiv_refusal(directive_over_several_nodes
      "stands over a repetition that can hold SEVERAL nodes")
  cursiv_refusal(directive_over_two_nodes
      "stands over a sequence that can hold TWO nodes")
  cursiv_refusal(directive_over_no_node
      "stands over a shape that can match with NO node")
  cursiv_refusal(flag_over_nest
      "this directive writes a member of a class the node in hand may not be")
  cursiv_refusal(flag_over_call
      "this alternative can create twice")
  cursiv_refusal(flag_reads_nothing
      "warning: rule .Root.: .*reads an operand that may match nothing")
  # A run in a run, written in place or reached through a rule: warnings,
  # pinned the same way.
  cursiv_refusal(nested_run
      "warning: rule .Root.: .*repeats a run, so a run of items reads as one group")
  # A word-like literal with no guard, from both generators.
  cursiv_refusal(word_literal
      "warning: rule .Root.: .'begin'. is a literal with no guard that ends in a word character, so it also matches the start of a longer word.*write .kw<.begin.>." LR LL)
  # The CLI diagnostic; wrapped and conditional shapes are covered by
  # NonConsuming_test's self-bounded-arm checks.
  cursiv_refusal(self_bounded_plain
      "refused .selfBoundedArm..*rule .Sum.: the arm .* begins and ends with .Sum., which says nothing of how its operands nest.*rule .Pair.: the arm .* begins and ends with .Pair.*rule .Cond.: the arm .* begins and ends with .Cond." LR LL)
  # A call adopts its node while the current node is empty, and once.
  foreach(_case IN ITEMS call_after_write call_after_flag flag_call_after_write)
    cursiv_refusal(${_case}
        "calls a rule that builds a node after the node in hand was written, created or nested")
  endforeach()
  foreach(_case IN ITEMS call_inside_create call_before_create
                         call_twice call_twice_chain call_in_nest)
    cursiv_refusal(${_case}
        "this alternative can create twice")
  endforeach()
  cursiv_refusal(write_over_adopted
      "[ '‘]WrittenOverAdopted<.*refuse::G::Root.*refuse::ast::Item::name")
  # A rule answers one class: the most derived base of what it can return.
  cursiv_refusal(writes_unrelated
      "write unrelated classes -- state create<T>")
  # A nest needs a head: a create, a call or a write on every path.
  foreach(_case IN ITEMS nest_after_text nest_after_optional_write)
    cursiv_refusal(${_case}
        "has a nest<> with nothing in front of it")
  endforeach()
  cursiv_refusal(operand_repeated
      "[ '‘]WrittenEachTurn<.*refuse::ast::Part::n")
  cursiv_refusal(nest_member_other_family
      "this nest buries the node in hand in a member that cannot hold it" LR)
  # The same law inside a directive's payload, a region of its own.
  cursiv_refusal(nest_in_payload_other_family
      "this nest buries the node in hand in a member that cannot hold it")
  cursiv_refusal(write_on_adopted_base
      "this directive writes a member of a class the node in hand may not be" LR)
  cursiv_refusal(default_converter_result_only
      "no conversion from this payload to the member type.*default_converter" LR)
  # A rule hands back the common base of what its alternatives end on, so a
  # member of a narrower class cannot hold it.
  cursiv_refusal(root_create_other_family
      "this member holds a node of its own type: the payload must build a node whose type derives")
  # A repetition of a head is one head per turn, and a rule keeps only the
  # first.
  cursiv_refusal(repeated_head
      "this alternative can create twice.*each turn of a repetition")
  # `=` overwrites: a single-valued member takes one write per path.
  cursiv_refusal(member_written_twice
      "[ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Holder::first")
  cursiv_refusal(member_written_each_turn
      "[ '‘]WrittenEachTurn<.*refuse::G::Root.*refuse::ast::Holder::first")
  # A node created in place, as a directive's operand, is held to it too.
  foreach(_case IN ITEMS member_written_twice_in_place
                         member_written_twice_appended)
    cursiv_refusal(${_case}
        "[ '‘]WrittenAgain<.*refuse::G::Root.*refuse::ast::Node::name")
  endforeach()
  # One directive per storage, and `flag` on a bool.
  cursiv_refusal(assign_on_vector
      "names the ONE value a member takes, and this member is a .vector<T>. list")
  cursiv_refusal(class_without_extends
      "without .Extends")
  cursiv_refusal(append_on_scalar
      "accumulates, so it names a member the grammar can accumulate INTO")
  cursiv_refusal(flag_not_bool
      "flag<&T::m>: the member must be bool" LR)
  cursiv_refusal(root_create_then_create
      "this member holds a node of its own type: the payload must build a node whose type derives" LR)
  cursiv_refusal(root_create_nest_other_family
      "this member holds a node of its own type: the payload must build a node whose type derives")

  # The descent's lanes for refusals pinned on the table alone, and the codes
  # no other lane pins.
  cursiv_refusal(left_recursion
      "refused .leftRecursion..*rule .Path. reaches itself before matching anything" LL)
  cursiv_refusal(left_recursion_eof
      "refused .leftRecursion..*rule .Tail. reaches itself before matching anything" LL)
  cursiv_refusal(left_recursion_eof
      "refused .unitProductionCycle..*rule .Tail. stands on a cycle of reductions" LR)
  cursiv_refusal(too_many_precedences
      "refused .tooManyPrecedences..*rule .Sum. declares more than 255 precedence levels" LR LL)
endif()
