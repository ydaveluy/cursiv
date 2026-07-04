#include <gtest/gtest.h>

#include <memory>

#include <statemachine/core/CoreModule.hpp>
#include <statemachine/lsp/LspModule.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>

namespace statemachine::tests {
namespace {

TEST(StatemachineLanguageTest, ParsesSimpleStateMachine) {
  auto parser = createStatemachineParser();
  auto document = pegium::test::parse_document(
      *parser,
      "statemachine Light\n"
      "events Start\n"
      "initialState Idle\n"
      "state Idle end\n",
      pegium::test::make_file_uri("machine.statemachine"), "statemachine");

  ASSERT_TRUE(document->parseSucceeded());
  auto *model =
      dynamic_cast<ast::Statemachine *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->states.size(), 1u);
}

TEST(StatemachineLanguageTest, LinksInitialStateTransitionAndActionReferences) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(statemachine::registerStatemachineCoreServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("linked.statemachine"),
      "statemachine",
      "statemachine Light\n"
      "events Start Stop\n"
      "commands Open Close\n"
      "initialState Idle\n"
      "state Idle actions { Open Close }\n"
      "Start => Running\n"
      "end\n"
      "state Running end\n");

  ASSERT_NE(document, nullptr);
  auto *model =
      dynamic_cast<ast::Statemachine *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_TRUE(model->init);
  EXPECT_EQ(model->init->name, "Idle");
  ASSERT_EQ(model->states.size(), 2u);

  auto *idle = model->states.front();
  ASSERT_NE(idle, nullptr);
  ASSERT_EQ(idle->actions.size(), 2u);
  ASSERT_TRUE(idle->actions[0]);
  ASSERT_TRUE(idle->actions[1]);
  EXPECT_EQ(idle->actions[0]->name, "Open");
  EXPECT_EQ(idle->actions[1]->name, "Close");

  ASSERT_EQ(idle->transitions.size(), 1u);
  auto *transition = idle->transitions.front();
  ASSERT_NE(transition, nullptr);
  ASSERT_TRUE(transition->event);
  ASSERT_TRUE(transition->state);
  EXPECT_EQ(transition->event->name, "Start");
  EXPECT_EQ(transition->state->name, "Running");
}

} // namespace
} // namespace statemachine::tests
