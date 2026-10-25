// The AST facet of a grammar, written as C++ source: the registries as data,
// and one flat function per entry naming the author's classes, members and
// converters. An engine hands its record table over as `AstEntryTable`.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/gen/AstEntryTable.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"

namespace cursiv {

struct AstEmitter : EmitOutput {
  using Run = std::vector<std::uint16_t>;

  // A statement's piece: a constant, when `param` names one of the body's
  // parameters, or the text around them.
  struct Piece {
    std::string text;
    int param = -1;
    Piece(const char* t) : text(t) {}
    Piece(std::string t, int p = -1) : text(std::move(t)), param(p) {}
  };

  // `text` writes the constants in; `form` names them `C<k>`, so bodies that
  // differ only in their constants share one form.
  struct Body {
    std::string text;
    std::string form;
    std::vector<std::string> args;
    unsigned made = 0;
    unsigned held = 0;
    Piece param(std::string value) {
      args.push_back(value);
      return {std::move(value), static_cast<int>(args.size()) - 1};
    }
    void say(std::initializer_list<Piece> pieces) {
      text += "  ";
      form += "  ";
      for (const Piece& p : pieces) {
        text += p.text;
        form += p.param < 0 ? p.text : "C" + std::to_string(p.param);
      }
      text += "\n";
      form += "\n";
    }
    void say(const std::string& s) { say({Piece(s)}); }
    // One guard where nothing stands between two.
    void guard() {
      const std::string g = "  if (b.failed()) return;\n";
      if (!text.ends_with(g)) say("if (b.failed()) return;");
    }
  };

  const AstRegistry& reg;
  const AstEntryTable& a;
  std::string ns;
  std::string header;

  std::vector<std::string> names;
  // Whether any entry states something other than its bytes.
  bool anyText = false;
  // Whether a grammar tells a call that read nothing from a token with no
  // width.
  bool anyCalls = false;

  // The members a node of each class takes off a pending, and each member's
  // tag.
  std::vector<std::vector<std::uint16_t>> accepted;
  std::vector<std::string> tags;

  AstEmitter(const AstRegistry& registry, const AstEntryTable& table,
             std::string n, std::string h);

  static std::string u(unsigned long long v);
  static std::string q(std::string_view name);

  bool derives(std::uint16_t base, std::uint16_t sub) const;
  // A write whose declaring class the created node satisfies fills that node;
  // the other direction holds for a close alone, which may adopt a subclass.
  bool fills(std::uint16_t cr, std::uint16_t r) const;
  // The row that makes the run's node: a close fused onto the creation it
  // would adopt is that creation.
  int made_row(const Run& l) const;
  // What follows the node's maker and names another class.
  Run spare(std::uint16_t cr, const Run& l) const;

  static const char* act_name(ActKind k);
  void emit_owe(Body& b, std::uint16_t r, int m) const;
  bool can_close_held(const Run& held, int cr, const Run& over,
                      const Run& added) const;
  // A base run holds one creation at most, and every write before it belongs
  // to its maker: fill_run and spare rely on it.
  void check_base_chains() const;
  std::string write_pending(Body& b, std::uint16_t r) const;
  // Two passes: a fill takes its node payload off the region before the
  // creation consumes what is left.
  unsigned fill_run(Body& b, std::uint16_t cr, const Run& held) const;
  void run_held(Body& b, const Run& held, const Run& over, int cr,
                int heldMade = -1) const;
  Body entry_body(std::uint32_t e) const;
  // One function per distinct program. Programs that differ only in their
  // constants expand one form where it spells less: a macro, since GCC
  // compiles even a forced inline call differently; a type argument with a
  // comma goes through an alias.
  void emit_entries();

  void emit_types();
  void emit_fields();
  // The slot's address, or none.
  static std::string conv_addr(std::uint16_t slot);
  const char* variant_flag(std::uint16_t f) const;
  // The class's writer, or the shared refusal where it declares no member a
  // pending can name.
  std::string applier_name(std::uint16_t t) const;
  // One writer per member a pending can name, switching on the conversion,
  // then one per class switching on the member.
  void emit_writers();
  // A conversion is named by the first row that asked for it.
  void emit_convs();
  // The cell of one record: the text its first `count` conversions make.
  std::string text_cell(const std::vector<const AstRowGen*>& chain,
                        std::size_t count);
  void emit_texts();
  // A table of flags, named only where one is set: a grammar without a turn's
  // twin rows names no table.
  bool emit_flags(std::string_view name,
                  const std::vector<std::uint8_t>& cells);

  std::string run();
};

std::string emit_ast(const AstRegistry& registry, const AstEntryTable& table,
                     std::string_view ns, std::string_view header);

std::string emit_ast_decl(std::string_view ns);

}  // namespace cursiv
