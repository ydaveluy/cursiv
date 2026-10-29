#include "statemachine/cli/Generator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sm {
namespace {

using namespace sm::ast;

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

void write_state_base(std::ostringstream& out, const Statemachine& model) {
  out << "class " << model.name << ";\n\n";
  out << "class State {\n";
  out << "protected:\n";
  out << "    " << model.name << " *statemachine;\n\n";
  out << "public:\n";
  out << "    virtual ~State() {}\n\n";
  out << "    void set_context(" << model.name << " *statemachine) {\n";
  out << "        this->statemachine = statemachine;\n";
  out << "    }\n\n";
  out << "    virtual std::string get_name() {\n";
  out << "        return \"Unknown\";\n";
  out << "    }\n";
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out << "\n";
    out << "    virtual void " << event->name << "() {\n";
    out << "        std::cout << \"Impossible event for the current state.\" "
           "<< std::endl;\n";
    out << "    }\n";
  }
  out << "};\n";
}

void write_machine_class(std::ostringstream& out, const Statemachine& model) {
  out << "class " << model.name << " {\n";
  out << "private:\n";
  out << "    State* state = nullptr;\n\n";
  out << "public:\n";
  out << "    " << model.name << "(State* initial_state) {\n";
  out << "        initial_state->set_context(this);\n";
  out << "        state = initial_state;\n";
  out << "        std::cout << \"[\" << state->get_name() << \"]\" << "
         "std::endl;\n";
  out << "    }\n\n";
  out << "    ~" << model.name << "() {\n";
  out << "        if (state != nullptr) {\n";
  out << "            delete state;\n";
  out << "        }\n";
  out << "    }\n\n";
  out << "    void transition_to(State *new_state) {\n";
  out << "        std::cout << state->get_name() << \" ===> \" << "
         "new_state->get_name() << std::endl;\n";
  out << "        if (state != nullptr) {\n";
  out << "            delete state;\n";
  out << "        }\n";
  out << "        new_state->set_context(this);\n";
  out << "        state = new_state;\n";
  out << "    }\n";
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out << "\n";
    out << "    void " << event->name << "() {\n";
    out << "        state->" << event->name << "();\n";
    out << "    }\n";
  }
  out << "};\n";
}

void write_state_declarations(std::ostringstream& out,
                              const Statemachine& model) {
  bool first = true;
  for (const auto& state : model.states) {
    if (!state) {
      continue;
    }
    if (!first) {
      out << "\n";
    }
    first = false;
    out << "class " << state->name << " : public State {\n";
    out << "public:\n";
    out << "    std::string get_name() override { return \"" << state->name
        << "\"; }\n";
    for (const auto* transition : distinct_event_transitions(*state)) {
      out << "    void " << transition->event->name << "() override;\n";
    }
    out << "};\n";
  }
}

void write_state_definitions(std::ostringstream& out,
                             const Statemachine& model) {
  for (const auto& state : model.states) {
    if (!state) {
      continue;
    }
    out << "\n";
    out << "// " << state->name << "\n";
    const auto transitions = distinct_event_transitions(*state);
    for (std::size_t index = 0; index < transitions.size(); ++index) {
      const auto* transition = transitions[index];
      out << "void " << state->name << "::" << transition->event->name
          << "() {\n";
      out << "    statemachine->transition_to(new " << transition->state->name
          << ");\n";
      out << "}\n";
      out << "\n";
      if (index + 1 < transitions.size()) {
        out << "\n";
      }
    }
  }
}

void write_main(std::ostringstream& out, const Statemachine& model) {
  if (!model.init) {
    throw std::runtime_error("Cannot generate C++ without an initial state.");
  }

  out << "typedef void (" << model.name << "::*Event)();\n\n";
  out << "int main() {\n";
  out << "    " << model.name << " *statemachine = new " << model.name
      << "(new " << model.init->name << ");\n\n";
  out << "    static std::map<std::string, Event> event_by_name;\n";
  for (const auto& event : model.events) {
    if (!event) {
      continue;
    }
    out << "    event_by_name[\"" << event->name << "\"] = &" << model.name
        << "::" << event->name << ";\n";
  }
  out << "\n";
  out << "    for (std::string input; std::getline(std::cin, input);) {\n";
  out << "        std::map<std::string, Event>::const_iterator "
         "event_by_name_it = event_by_name.find(input);\n";
  out << "        if (event_by_name_it == event_by_name.end()) {\n";
  out << "            std::cout << \"There is no event <\" << input << \"> in "
         "the "
      << model.name << " statemachine.\" << std::endl;\n";
  out << "            continue;\n";
  out << "        }\n";
  out << "        Event event_invoker = event_by_name_it->second;\n";
  out << "        (statemachine->*event_invoker)();\n";
  out << "    }\n\n";
  out << "    delete statemachine;\n";
  out << "    return 0;\n";
  out << "}\n";
}

}  // namespace

std::string generate_cpp_content(const ast::Statemachine& model) {
  std::ostringstream out;
  out << "#include <iostream>\n";
  out << "#include <map>\n";
  out << "#include <string>\n\n";
  write_state_base(out, model);
  out << "\n\n";
  write_machine_class(out, model);
  out << "\n\n";
  write_state_declarations(out, model);
  write_state_definitions(out, model);
  out << "\n";
  write_main(out, model);
  return out.str();
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
