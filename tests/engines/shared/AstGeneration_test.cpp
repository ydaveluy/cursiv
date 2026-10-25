#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/ll/gen/ll_ast_registry.hpp"
#include "cursiv/engines/lr/gen/lr_ast_registry.hpp"
#include "cursiv/engines/shared/gen/AstEmitter.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/TypeSpelling.hpp"
#include "cursiv/engines/shared/gen/write_source.hpp"
#include "cursiv/test/Check.hpp"
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace astgen {

template <auto Expression>
struct Operator {};

void nested_template_arguments_remain_qualified() {
  // An already qualified spelling stays so, independently of the compiler's
  // pretty form; GCC's class value also needs expression parentheses.
  CHECK(cursiv::gen_qualified(
            "astgen::Operator<cursiv::Lit<cursiv::str<3>{\"is\"}, false, "
            "cursiv::detail::WordBound>()>") ==
        "::astgen::Operator<(cursiv::Lit<cursiv::str<3>{\"is\"}, false, "
        "cursiv::detail::WordBound>())>");
  const auto spelling = cursiv::gen_qualified(
      cursiv::detail::raw_type_name<Operator<cursiv::kw<"is">>>());
  CHECK(spelling.starts_with("::astgen::Operator<"));
  CHECK(spelling.find("detail::WordBound") != std::string::npos);
  CHECK(spelling.find("false, WordBound") == std::string::npos);
  using Repeated = Operator<cursiv::many(cursiv::lit<"x">)>;
  const auto repeated =
      cursiv::gen_qualified(cursiv::detail::raw_type_name<Repeated>());
  CHECK(repeated.find("kUnbounded") != std::string::npos);
  const auto nested = cursiv::gen_qualified(
      cursiv::detail::raw_type_name<
          Operator<cursiv::many(cursiv::lit<"x">) + cursiv::lit<"y">>>());
  CHECK(nested.find("kUnbounded") != std::string::npos);

  const auto maximum = std::to_string(cursiv::kUnbounded);
  const auto maximum_literal =
      maximum + (std::numeric_limits<std::size_t>::digits >
                         std::numeric_limits<long long>::digits
                     ? "ull"
                     : "");
  CHECK(repeated.find(maximum) == std::string::npos);
  CHECK(nested.find(maximum) == std::string::npos);
  CHECK(cursiv::gen_qualified("cursiv::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::cursiv::Repeat<0,::cursiv::kUnbounded,astgen::Token>");
  CHECK(cursiv::gen_qualified("cursiv::Repeat<" + maximum +
                              ",7,astgen::Token>") ==
        "::cursiv::Repeat<" + maximum_literal + ",7,astgen::Token>");
  CHECK(cursiv::gen_qualified("cursiv::Repeat<2,7,astgen::Token>") ==
        "::cursiv::Repeat<2,7,astgen::Token>");
  CHECK(cursiv::gen_qualified("astgen::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::astgen::Repeat<0," + maximum_literal + ",astgen::Token>");
  CHECK(cursiv::gen_qualified("astgen::cursiv::Repeat<0," + maximum +
                              ",astgen::Token>") ==
        "::astgen::cursiv::Repeat<0," + maximum_literal + ",astgen::Token>");
  const std::string quoted =
      "astgen::Literal<\"cursiv::Repeat<0," + maximum + ",Token>\">";
  CHECK(cursiv::gen_qualified(quoted) == "::" + quoted);

  CHECK(cursiv::gen_qualified(
            cursiv::detail::raw_type_name<
                Operator<std::numeric_limits<long long>::min()>>()) ==
        "::astgen::Operator<(-9223372036854775807ll - 1)>");
  CHECK(cursiv::gen_qualified("astgen::Operator<-9223372036854775808L>") ==
        "::astgen::Operator<(-9223372036854775807l - 1)>");
  CHECK(cursiv::gen_qualified("astgen::Operator<18446744073709551615>") ==
        "::astgen::Operator<18446744073709551615ull>");
  CHECK(cursiv::gen_widen("\"-9223372036854775808 18446744073709551615\"") ==
        "\"-9223372036854775808 18446744073709551615\"");
}

struct Base : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct RefOnly : cursiv::Extends<cursiv::AstNode> {};
struct Derived : cursiv::Extends<Base> {
  reference<RefOnly> target;
};
struct Wrapper : cursiv::Extends<Derived> {
  Derived* child = nullptr;
};

struct G : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root
      : rule<create<Derived>(assign<&Base::name>(ref<ID>) +
                             assign<&Derived::target>(ref<ID>) +
                             many(nest<Wrapper, &Wrapper::child>(lit<"!">)))> {
  };
  using entry = Root;
};

template <class T>
std::size_t type_of(const cursiv::AstRegistry& registry) {
  const auto name = cursiv::detail::raw_type_name<T>();
  for (std::size_t i = 0; i < registry.types.size(); ++i)
    if (registry.types[i].name == name) return i;
  return registry.types.size();
}

template <auto P>
std::size_t field_of(const cursiv::AstRegistry& registry) {
  const auto name = cursiv::detail::raw_member_name<P>();
  for (std::size_t i = 0; i < registry.fields.size(); ++i)
    if (registry.fields[i].ptr == name) return i;
  return registry.fields.size();
}

void reflection_only_types_need_no_factory(
    const cursiv::AstRegistry& registry) {
  const cursiv::AstEntryTable table;
  cursiv::AstEmitter emitter(registry, table, "astgen",
                             "AstGeneration_test.cpp");
  const auto base = type_of<Base>(registry);
  const auto reference = type_of<RefOnly>(registry);
  const auto derived = type_of<Derived>(registry);
  const auto wrapper = type_of<Wrapper>(registry);
  CHECK(!registry.types[base].built && !registry.types[reference].built);
  CHECK(registry.types[derived].built && registry.types[wrapper].built);
  CHECK(emitter.applier_name(base) == "a_none");
  CHECK(emitter.applier_name(reference) == "a_none");
  CHECK(emitter.applier_name(derived) != "a_none");
  CHECK(emitter.applier_name(wrapper) != "a_none");
  emitter.emit_types();
  const auto factory = [&registry]<class T>() {
    return "make_node<" + registry.types[type_of<T>(registry)].source + ">";
  };
  CHECK(emitter.out.find(factory.template operator()<Base>()) ==
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<RefOnly>()) ==
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<Derived>()) !=
        std::string::npos);
  CHECK(emitter.out.find(factory.template operator()<Wrapper>()) !=
        std::string::npos);
  emitter.emit_writers();
  const auto name = field_of<&Base::name>(registry);
  CHECK(emitter.out.find("case " + std::to_string(name) + "u: w" +
                         std::to_string(name)) != std::string::npos);
}

}  // namespace astgen

void ast_generation_cases_run() {
  astgen::nested_template_arguments_remain_qualified();
  astgen::reflection_only_types_need_no_factory(
      cursiv::lr_ast_registry<astgen::G>());
  astgen::reflection_only_types_need_no_factory(
      cursiv::ll_ast_registry<astgen::G>());

}

namespace write_source_cases {

// A generated file whose text did not move is left as it is, whatever its
// size; one that moved is written, in whole.

namespace {

namespace fs = std::filesystem;

std::string seen(const fs::path& path) {
  return cursiv::test::read_file(path.string());
}

}  // namespace

void run() {
  const fs::path dir =
      fs::temp_directory_path() /
      ("cursiv-write-source-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string path = (dir / "Table.hpp").string();

  CHECK(cursiv::write_source(path, "one"));
  CHECK(seen(path) == "one");

  // Unmoved: the file keeps the time it was written at.
  fs::last_write_time(path, fs::last_write_time(path) - std::chrono::hours(1));
  const auto then = fs::last_write_time(path);
  CHECK(cursiv::write_source(path, "one"));
  CHECK(fs::last_write_time(path) == then);

  // Moved, at the same size and at another.
  CHECK(cursiv::write_source(path, "two"));
  CHECK(seen(path) == "two" && fs::last_write_time(path) != then);
  CHECK(cursiv::write_source(path, "three and more"));
  CHECK(seen(path) == "three and more");
  CHECK(cursiv::write_source(path, "short"));
  CHECK(seen(path) == "short");

  // Empty text, over a file and over none; and bytes a text reader would fold.
  CHECK(cursiv::write_source(path, ""));
  CHECK(fs::file_size(path) == 0);
  CHECK(cursiv::write_source(path, ""));
  const std::string folded("a\r\nb\0c", 6);
  CHECK(cursiv::write_source(path, folded));
  CHECK(cursiv::write_source(path, folded));
  CHECK(seen(path) == folded);
  CHECK(cursiv::write_source((dir / "New.hpp").string(), ""));
  CHECK(fs::exists(dir / "New.hpp"));

  // A path that cannot be written says so.
  CHECK(!cursiv::write_source((dir / "missing" / "Table.hpp").string(), "x"));

  fs::remove_all(dir);
}

}  // namespace write_source_cases

int main() {
  cursiv::test::count_expectations();
  ast_generation_cases_run();
  write_source_cases::run();
  return cursiv::test::report("AstGeneration_test");
}
