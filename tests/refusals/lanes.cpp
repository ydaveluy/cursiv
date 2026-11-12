// `refusal_lanes <log> <out-dir>` gives each refusal fixture the diagnostics of
// a failed compilation that name it, by its header or its namespace, in
// `<out-dir>/<fixture>.log`: each error with its context and its notes.
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace {

// The fixtures `text` names after each `lead`, where `tail` follows the name.
void named(std::string_view text, std::string_view lead, std::string_view tail,
           std::set<std::string>& out) {
  for (auto at = text.find(lead); at != std::string_view::npos;
       at = text.find(lead, at + 1)) {
    auto end = at + lead.size();
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) != 0 ||
            text[end] == '_'))
      ++end;
    if (text.substr(end).starts_with(tail))
      out.emplace(text.substr(at + lead.size(), end - at - lead.size()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <log> <out-dir>\n";
    return 2;
  }
  std::ifstream log(argv[1]);
  if (!log) {
    std::cerr << argv[0] << ": cannot read " << argv[1] << "\n";
    return 2;
  }
  std::map<std::string, std::string> lanes;
  std::string context, diagnostic;
  const auto close = [&] {
    std::set<std::string> fixtures;
    named(diagnostic, "refusals/", ".hpp", fixtures);
    named(diagnostic, "refusals\\", ".hpp", fixtures);
    named(diagnostic, "refusal_", "::", fixtures);
    for (const std::string& fixture : fixtures) lanes[fixture] += diagnostic;
    diagnostic.clear();
  };
  bool inDiagnostic = false;
  for (std::string line; std::getline(log, line);) {
    line += '\n';
    const bool continued =
        std::isspace(static_cast<unsigned char>(line.front())) != 0;
    const std::string_view text = line;
    const bool opens = !continued &&
                       (text.find("error: ") != std::string_view::npos ||
                        text.find("warning: ") != std::string_view::npos);
    const bool note =
        !continued && !opens && text.find("note: ") != std::string_view::npos;
    if (opens) close();
    if (opens || (note && !diagnostic.empty())) {
      diagnostic += context;
      context.clear();
      inDiagnostic = true;
    } else if (!continued) {
      inDiagnostic = false;
    }
    (inDiagnostic ? diagnostic : context) += line;
  }
  close();
  for (const auto& [fixture, text] : lanes) {
    const std::string path = std::string(argv[2]) + '/' + fixture + ".log";
    std::ofstream out(path);
    out << text;
    if (!out) {
      std::cerr << argv[0] << ": cannot write " << path << "\n";
      return 2;
    }
  }
  return 0;
}
