#include "cursiv/engines/shared/gen/CommonArtifacts.hpp"

#include "cursiv/engines/shared/gen/AstEmitter.hpp"
#include "cursiv/engines/shared/gen/EmitGrammarSource.hpp"
#include "cursiv/engines/shared/gen/EmitModule.hpp"
#include "cursiv/engines/shared/gen/EmitRecords.hpp"
#include "cursiv/engines/shared/gen/write_source.hpp"

namespace cursiv {

GenCode write_common_artifacts(const std::string& dir,
                               const CommonArtifacts& what) {
  const auto put = [&](const char* file, const std::string& text) {
    return write_source(dir + "/" + file, text);
  };
  if (!put("GrammarSource.hpp", emit_grammar_source(what.ns, what.dump)) ||
      !put("GRAMMAR.txt", dump_wrapped(what.dump.text)))
    return GenCode::grammarText;
  if (!put("AstReflection.hpp", emit_ast_decl(what.ns)) ||
      !put("AstReflection.cpp",
           emit_ast(what.registry, what.entries, what.ns, what.header)))
    return GenCode::ast;
  if (!put("Grammar.hpp", emit_records_decl(what.ns)) ||
      !put("Grammar.cpp",
           emit_records(what.ns, what.header, what.terminals, what.records, what.registry)))
    return GenCode::records;
  if (!put("Module.hpp", emit_module_decl(what.ns, what.lang)) ||
      !put("Module.cpp", emit_module(what.ns, what.lang, what.parserHeader,
                                     what.parserClass)))
    return GenCode::module;
  return GenCode::ok;
}

}  // namespace cursiv
