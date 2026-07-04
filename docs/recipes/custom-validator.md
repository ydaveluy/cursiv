# Custom Validator

Add semantic rules beyond parsing by writing a validator class. This page covers the basic structure; for rules that need a whole-document graph walk, see [Dependency Loops](validation/dependency-loops.md).

## Pattern

1. implement one method per AST node kind you want to validate
2. register the methods in the validation registry
3. report diagnostics with `ValidationAcceptor`

This is usually a small class plus one registry setup point.

## A concrete example

The `domainmodel` example registers checks like this:

```cpp
inline void registerValidationChecks(pegium::CoreServices &services,
                                     DomainModelValidator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkEntityNameStartsWithCapital>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkDataTypeNameStartsWithCapital>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkEntityDoesNotExtendItself>(validator)});
}
```

The core module owns the validator instance and registers each method against the core service container's `validation.validationRegistry`. LSP services build on the same core container, so the checks run for both plain document builds and language-server requests.

By default a check registered for a type also runs for every subtype: a check on the abstract `Type` base sees both `Entity` and `DataType`. To run a check *only* for nodes whose dynamic type is exactly the target — never its subtypes — register it with the exact variants instead. Both produce the same registration type, so they mix freely in one `registerChecks({...})` call:

```cpp
registry.registerChecks(
    {// runs for every Type — both Entity and DataType
     pegium::validation::ValidationRegistry::makeValidationCheck<
         &DomainModelValidator::checkType>(validator),
     // runs only for a node whose exact type is Entity
     pegium::validation::ValidationRegistry::makeExactValidationCheck<
         &DomainModelValidator::checkEntityOnly>(validator)});
```

The single-check helpers `registerCheck<Node>` (subtypes included) and `registerExactCheck<Node>` (exact type only) mirror this.

A validator method then attaches a diagnostic directly to the relevant property:

```cpp
void DomainModelValidator::checkEntityNameStartsWithCapital(
    const ast::Entity &entity,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (entity.name.empty()) {
    return;
  }
  const auto first = entity.name.front();
  if (std::toupper(static_cast<unsigned char>(first)) == first) {
    return;
  }
  accept.warning(entity, "Type name should start with a capital.")
      .property<&ast::Entity::name>();
}
```

`.property<&Feature>()` narrows the underlined range to a parsed feature. To
underline a keyword literal instead — handy when the message is about a clause
introduced by a keyword rather than an assigned value — use `.keyword("name")`
(optionally `.keyword("name", index)` for a repeated keyword):

```cpp
void DomainModelValidator::checkEntityDoesNotExtendItself(
    const ast::Entity &entity,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (entity.superType.has_value() && entity.superType->get() == &entity) {
    accept.error(entity, "An entity may not extend itself.")
        .keyword("extends");
  }
}
```

Here the diagnostic underlines the `extends` keyword that introduces the offending
clause. When the keyword is absent from the node's syntax tree (for instance an
optional keyword that was not written), the diagnostic keeps the whole node's
range.

## Typical workflow

Start with checks that are local and cheap:

- duplicate names
- empty or missing required values
- inconsistent flags or modifiers

Add semantic or cross-reference aware checks once scoping and linking work.

## Practical advice

- keep checks small and type-specific
- use categories when some checks are expensive
- attach diagnostics to the most precise AST node or property available
- do not duplicate grammar constraints already enforced during parsing

For expected structure, look at the validators in the `arithmetics` and `statemachine` examples.

## Related pages

- [Validation](validation/index.md)
- [Dependency Loops](validation/dependency-loops.md)
