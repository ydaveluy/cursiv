#pragma once

#include "cursiv/lsp/formatting/AbstractFormatter.hpp"
#include "requirements/core/ast.hpp"

namespace req {

class RequirementsFormatter : public cursiv::AbstractFormatter {
 public:
  explicit RequirementsFormatter(const cursiv::CoreServices& services);

 protected:
  virtual void formatContact(cursiv::FormattingBuilder& builder,
                             const ast::Contact* contact) const;
  virtual void formatRequirementModel(cursiv::FormattingBuilder& builder,
                                      const ast::RequirementModel* model) const;
  virtual void formatEnvironment(cursiv::FormattingBuilder& builder,
                                 const ast::Environment* environment) const;
  virtual void formatRequirement(cursiv::FormattingBuilder& builder,
                                 const ast::Requirement* requirement) const;
};

class TestsFormatter : public cursiv::AbstractFormatter {
 public:
  explicit TestsFormatter(const cursiv::CoreServices& services);

 protected:
  virtual void formatContact(cursiv::FormattingBuilder& builder,
                             const ast::Contact* contact) const;
  virtual void formatTestModel(cursiv::FormattingBuilder& builder,
                               const ast::TestModel* model) const;
  virtual void formatTest(cursiv::FormattingBuilder& builder,
                          const ast::Test* test) const;
};

}  // namespace req
