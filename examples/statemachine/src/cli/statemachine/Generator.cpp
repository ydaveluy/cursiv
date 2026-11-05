#include "statemachine/Generator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/TextBuilder.hpp"

namespace sm {
namespace {

using namespace sm::ast;
using cursiv::TextBuilder;

[[nodiscard]] std::string sanitize_file_stem(std::string_view inputPath) {
  auto stem = std::filesystem::path(inputPath).stem().string();
  const auto [newEnd, end] = std::ranges::remove_if(
      stem, [](char ch) { return ch == '.' || ch == '-'; });
  stem.erase(newEnd, end);
  if (stem.empty()) {
    stem = "generated";
  }
  return stem;
}

[[nodiscard]] std::string resolve_destination(
    std::optional<std::string_view> destination) {
  return destination.has_value() ? std::string(*destination)
                                 : std::string("generated");
}

// One transition per distinct event name (first occurrence): both generator
// passes use this, so two transitions on the same event still yield exactly
// one method -- a redeclared override would not compile.
[[nodiscard]] std::vector<const Transition*> distinct_event_transitions(
    const State& state) {
  std::vector<const Transition*> result;
  std::vector<std::string_view> seenEvents;
  for (const auto& transition : state.transitions) {
    if (!transition || !transition->event || !transition->state) {
      continue;
    }
    const std::string_view eventName = transition->event->name;
    if (std::ranges::find(seenEvents, eventName) != seenEvents.end()) {
      continue;
    }
    seenEvents.push_back(eventName);
    result.push_back(transition);
  }
  return result;
}

void write_state_base(TextBuilder& out, const Statemachine& model) {
  out.line("class ", model.name, ";");
  out.line();
  out.line("class State {");
  out.open("protected:");
  out.line(model.name, " *statemachine;");
  out.close();
  out.line();
  out.open("public:");
  out.line("virtual ~State() {}");
  out.line();
  out.open("void set_context(", model.name, " *statemachine) {");
  out.line("this->statemachine = statemachine;");
  out.close("}");
  out.line();
  out.open("virtual std::string get_name() {");
  out.line(R"(return "Unknown";)");
  out.close("}");
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out.line();
    out.open("virtual void ", event->name, "() {");
    out.line(
        R"(std::cout << "Impossible event for the current state." << std::endl;)");
    out.close("}");
  }
  out.close("};");
}

void write_machine_class(TextBuilder& out, const Statemachine& model) {
  out.line("class ", model.name, " {");
  out.open("private:");
  out.line("State* state = nullptr;");
  out.close();
  out.line();
  out.open("public:");
  out.open(model.name, "(State* initial_state) {");
  out.line("initial_state->set_context(this);");
  out.line("state = initial_state;");
  out.line(R"(std::cout << "[" << state->get_name() << "]" << std::endl;)");
  out.close("}");
  out.line();
  out.open("~", model.name, "() {");
  out.open("if (state != nullptr) {");
  out.line("delete state;");
  out.close("}");
  out.close("}");
  out.line();
  out.open("void transition_to(State *new_state) {");
  out.line(
      R"(std::cout << state->get_name() << " ===> " << new_state->get_name() << std::endl;)");
  out.open("if (state != nullptr) {");
  out.line("delete state;");
  out.close("}");
  out.line("new_state->set_context(this);");
  out.line("state = new_state;");
  out.close("}");
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out.line();
    out.open("void ", event->name, "() {");
    out.line("state->", event->name, "();");
    out.close("}");
  }
  out.close("};");
}

void write_state_declarations(TextBuilder& out, const Statemachine& model) {
  bool first = true;
  for (const auto& state : model.states) {
    if (!state) {
      continue;
    }
    if (!std::exchange(first, false)) {
      out.line();
    }
    out.line("class ", state->name, " : public State {");
    out.open("public:");
    out.line(R"(std::string get_name() override { return ")", state->name,
             R"("; })");
    for (const auto* transition : distinct_event_transitions(*state)) {
      out.line("void ", transition->event->name, "() override;");
    }
    out.close("};");
  }
}

void write_state_definitions(TextBuilder& out, const Statemachine& model) {
  for (const auto& state : model.states) {
    if (!state) {
      continue;
    }
    out.line();
    out.line("// ", state->name);
    const auto transitions = distinct_event_transitions(*state);
    for (std::size_t index = 0; index < transitions.size(); ++index) {
      const auto* transition = transitions[index];
      out.open("void ", state->name, "::", transition->event->name, "() {");
      out.line("statemachine->transition_to(new ", transition->state->name,
               ");");
      out.close("}");
      out.line();
      if (index + 1 < transitions.size()) {
        out.line();
      }
    }
  }
}

void write_main(TextBuilder& out, const Statemachine& model) {
  if (!model.init) {
    throw std::runtime_error("Cannot generate C++ without an initial state.");
  }

  out.line("typedef void (", model.name, "::*Event)();");
  out.line();
  out.open("int main() {");
  out.line(model.name, " *statemachine = new ", model.name, "(new ",
           model.init->name, ");");
  out.line();
  out.line("static std::map<std::string, Event> event_by_name;");
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out.line(R"(event_by_name[")", event->name, R"("] = &)", model.name,
             "::", event->name, ";");
  }
  out.line();
  out.open("for (std::string input; std::getline(std::cin, input);) {");
  out.line(
      "std::map<std::string, Event>::const_iterator event_by_name_it = "
      "event_by_name.find(input);");
  out.open("if (event_by_name_it == event_by_name.end()) {");
  out.line(R"(std::cout << "There is no event <" << input << "> in the )",
           model.name, R"( statemachine." << std::endl;)");
  out.line("continue;");
  out.close("}");
  out.line("Event event_invoker = event_by_name_it->second;");
  out.line("(statemachine->*event_invoker)();");
  out.close("}");
  out.line();
  out.line("delete statemachine;");
  out.line("return 0;");
  out.close("}");
}

}  // namespace

std::string generate_cpp_content(const ast::Statemachine& model) {
  TextBuilder out;
  out.line("#include <iostream>");
  out.line("#include <map>");
  out.line("#include <string>");
  out.line();
  write_state_base(out, model);
  out.line().line();
  write_machine_class(out, model);
  out.line().line();
  write_state_declarations(out, model);
  write_state_definitions(out, model);
  out.line();
  write_main(out, model);
  return out.take();
}

std::string generate_cpp(const ast::Statemachine& model,
                         std::string_view inputPath,
                         std::optional<std::string_view> destination) {
  const auto outputDirectory = resolve_destination(destination);
  std::filesystem::create_directories(outputDirectory);
  const auto outputPath = std::filesystem::path(outputDirectory)
                          / (sanitize_file_stem(inputPath) + ".cpp");
  std::ofstream out(outputPath, std::ios::binary);
  if (!out.is_open()) {
    throw std::runtime_error("Unable to open output file: "
                             + outputPath.string());
  }
  out << generate_cpp_content(model);
  return outputPath.string();
}

}  // namespace sm
