// A semicolon is inserted exactly where ECMAScript's first rule says: the
// token in hand is refused, and a line break, a closing brace or the end of
// input stands before it. A token the statement can take never splits it.

#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "asi/generated/G/Module.hpp"
#include "asicases/generated/G/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/CstAudit.hpp"
#include "engines/lr/AsiGrammar.hpp"

namespace {

// A grammar, or one case of the combined one behind its opening keyword.
struct Case {
  const cursiv::Parser& p;
  std::string_view head;
  std::string text(std::string_view body) const {
    return std::string(head) + std::string(body);
  }
};

const cursiv::Parser& cases() {
  static const auto p = tasi::cases::generated::G::create_parser();
  return *p;
}

const Case* current = nullptr;

// A statement as a word: `a(b)` a call, `{a b}` a block, `return a`, `;`.
void spell(const tasi::ast::Stmt* s, std::string& out) {
  if (s->empty) {
    out += ';';
    return;
  }
  if (s->go) {
    out += "go";
    for (const auto* const a : s->args) {
      out += '<';
      spell(a, out);
      out += '>';
    }
    return;
  }
  if (s->ret) {
    out += "return";
    for (const auto* const a : s->args) {
      out += '(';
      spell(a, out);
      out += ')';
    }
    return;
  }
  if (s->head.empty()) {
    out += '{';
    for (std::size_t i = 0; i < s->body.size(); ++i) {
      if (i) out += ' ';
      spell(s->body[i], out);
    }
    out += '}';
    return;
  }
  if (s->obj) out += '=';
  out += s->head;
  if (!s->args.empty()) {
    out += '(';
    for (std::size_t i = 0; i < s->args.size(); ++i) {
      if (i) out += ',';
      spell(s->args[i], out);
    }
    out += ')';
  }
  for (const auto* const d : s->divs) {
    out += '/';
    spell(d, out);
  }
  if (s->inc) out += "++";
}

std::string read(std::string_view body) {
  const std::string text = current->text(body);
  const cursiv::ParseResult r = current->p.parse(text);
  if (!r.accepted || !r.diagnostics.empty()) return "<refused>";
  const auto* const doc = r.value_as<tasi::ast::Doc>();
  std::string out;
  for (std::size_t i = 0; i < doc->body.size(); ++i) {
    if (i) out += ' ';
    spell(doc->body[i], out);
  }
  return out;
}

bool same(std::string_view text, std::string_view want) {
  const std::string got = read(text);
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

void spell_op(const tasi::ops::ast::Node* n, std::string& out) {
  using namespace tasi::ops::ast;
  if (cursiv::ast_cast<Bin>(n) != nullptr) {
    out += '(';
    spell_op(n->kids[0], out);
    out += n->op;
    spell_op(n->kids[1], out);
    out += ')';
  } else if (cursiv::ast_cast<Pre>(n) != nullptr) {
    out += "(";
    out += n->op;
    spell_op(n->kids[0], out);
    out += ")";
  } else if (cursiv::ast_cast<Post>(n) != nullptr) {
    out += "(";
    spell_op(n->kids[0], out);
    out += n->op;
    out += ")";
  } else {
    out += n->name;
  }
}

// An expression of the tail grammar as its operands joined by operators.
bool tail(std::string_view text, std::string_view want) {
  const std::string source = "tailops " + std::string(text);
  const cursiv::ParseResult r = cases().parse(source);
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty()) {
    got.clear();
    for (const auto* const item : r.value_as<tasi::tail::ast::Doc>()->items) {
      if (!got.empty()) got += ' ';
      for (const auto* const k : item->kids) {
        if (!k->op.empty()) got += k->op;
        got += k->kids.empty() ? k->name : k->kids[0]->name;
      }
    }
  }
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

void spell_block(const tasi::block::ast::Node* n, std::string& out) {
  using namespace tasi::block::ast;
  const bool block = cursiv::ast_cast<Block>(n) != nullptr;
  if (block || cursiv::ast_cast<Obj>(n) != nullptr) out += block ? "{" : "obj(";
  if (cursiv::ast_cast<Var>(n) != nullptr) out += "var";
  out += n->name;
  for (std::size_t i = 0; i < n->body.size(); ++i) {
    if (block && i != 0) out += ' ';
    spell_block(n->body[i], out);
  }
  if (block || cursiv::ast_cast<Obj>(n) != nullptr) out += block ? "}" : ")";
}

bool block(std::string_view text, std::string_view want) {
  const std::string doc = "block " + std::string(text);
  const cursiv::ParseResult r = cases().parse(doc);
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty()) {
    got.clear();
    for (const auto* const item : r.value_as<tasi::block::ast::Doc>()->items) {
      if (!got.empty()) got += ' ';
      spell_block(item, got);
    }
  }
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

// The names read, or how the parse refused: a repair names what it did, and
// a parse that stopped without one says `<silent>`.
std::string optgap(std::string_view text) {
  const std::string doc = "optgap " + std::string(text);
  const cursiv::ParseResult r = cases().parse(doc);
  if (!r.accepted || !r.diagnostics.empty()) {
    for (const auto& d : r.diagnostics)
      if (d.kind != cursiv::DiagnosticKind::conversion_error)
        return "<repaired>";
    return "<silent>";
  }
  std::string got;
  for (const auto name : r.value_as<tasi::optgap::ast::Doc>()->names) {
    if (!got.empty()) got += ' ';
    got += name;
  }
  return got;
}

bool ops(std::string_view text, std::string_view want) {
  const std::string doc = "ops " + std::string(text);
  const cursiv::ParseResult r = cases().parse(doc);
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty()) {
    got.clear();
    for (const auto* const item : r.value_as<tasi::ops::ast::Doc>()->items) {
      if (!got.empty()) got += ' ';
      spell_op(item, got);
    }
  }
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

// Whether the caret at `|` offers `literal`.
bool offers_at(const Case& c, std::string_view body,
               std::string_view literal) {
  const cursiv::Parser& parser = c.p;
  std::string text = c.text(body);
  const auto at = text.find('|');
  text.erase(at, 1);
  const auto r = parser.parse(text);
  std::vector<cursiv::NextFeature> features;
  parser.nextFeaturesAt(r.cst(), at, at, features);
  for (const auto& feature : features)
    if (parser.getGrammar().terminalText(feature.feature) == literal)
      return true;
  return false;
}

// Whether the caret at `|` offers the terminal named `name`.
bool names_at(const Case& c, std::string_view body, std::string_view name) {
  const cursiv::Parser& parser = c.p;
  std::string text = c.text(body);
  const auto at = text.find('|');
  text.erase(at, 1);
  const auto r = parser.parse(text);
  std::vector<cursiv::NextFeature> features;
  parser.nextFeaturesAt(r.cst(), at, at, features);
  for (const auto& feature : features)
    if (parser.getGrammar().terminalName(feature.feature) == name) return true;
  return false;
}

void guarded_completion(const Case& c) {
  const cursiv::Parser& parser = c.p;
  const auto offers = [&](std::string_view body, unsigned replaced,
                          std::string_view literal, std::string_view name = {}) {
    std::string text = c.text(body);
    const auto at = text.find('|');
    text.erase(at, 1);
    const auto r = parser.parse(text);
    std::vector<cursiv::NextFeature> features;
    parser.nextFeaturesAt(r.cst(), at, at + replaced, features);
    for (const auto& feature : features)
      if (parser.getGrammar().terminalText(feature.feature) == literal &&
          (name.empty() ||
           parser.getGrammar().terminalName(feature.feature) == name))
        return true;
    return false;
  };
  CHECK(offers("call |();", 1, "("));
  CHECK(offers("call\t|();", 1, "("));
  CHECK(!offers("call\r|();", 1, "("));
  CHECK(!offers("call\n|();", 1, "("));
  CHECK(offers("call |;", 0, "("));
  CHECK(!offers("call\n|;", 0, "("));
  CHECK(offers("bang a|!", 1, "!"));
  CHECK(!offers("bang a |!", 1, "!"));
  CHECK(!offers("bang a\n|!", 1, "!"));
  CHECK(offers("tail |;", 0, "", "ID"));
  CHECK(offers("tail |", 0, "", "ID"));
  CHECK(!offers("tail\n|;", 0, "", "ID"));
  CHECK(offers("use a |;", 0, "", "ID"));
  CHECK(offers("use a |;", 0, "in"));
  CHECK(offers("call(); bang a|!", 1, "!"));
  CHECK(!offers("call(); call\n|;", 0, "("));
  CHECK(offers("bAng a|!", 1, "!"));
  CHECK(!offers("bAng a |!", 1, "!"));
  CHECK(offers("cAll |();", 1, "("));
  CHECK(!offers("cAll\n|;", 0, "("));
  CHECK(offers("bang a,|!", 1, "!"));
  CHECK(!offers("bang a, |!", 1, "!"));
}

void statements() {
  // Behind a line break a refused token ends the statement...
  CHECK(same("a\nb", "a b"));
  CHECK(same("a\n\n\nb", "a b"));
  CHECK(same("a /* one\n line */ b", "a b"));
  // ...and one the statement can take does not: this is a call.
  CHECK(same("a\n(b)", "a(b)"));
  CHECK(same("a\n(b)\n(c)", "a(b,c)"));
  // On one line nothing is inserted.
  CHECK(same("a b", "<refused>"));
  CHECK(same("a /* no break */ b", "<refused>"));
  CHECK(same("a; b", "a b"));
  // The end of input and a closing brace insert one.
  CHECK(same("a", "a"));
  CHECK(same("{ a }", "{a}"));
  CHECK(same("{ a\nb }", "{a b}"));
  CHECK(same("{ a b }", "<refused>"));
  CHECK(same("return\n", "return"));
  CHECK(same("{ return a }", "{return(a)}"));
  CHECK(same("return a", "return(a)"));
  CHECK(same("{ return }", "{return}"));
  // A restricted production: an operand may not follow a line break.
  CHECK(same("return\na", "return a"));
  CHECK(same("return /* no\n break */ a", "return a"));
  CHECK(same("a++", "a++"));
  CHECK(same("a ++\nb", "a++ b"));
  CHECK(same("a\n++", "<refused>"));
  CHECK(same("{ a++ }", "{a++}"));
  // A `/` after an operand divides, whatever the state may read past
  // `!gap::line` elsewhere; after `return` it opens a regex.
  CHECK(same("a / b / c", "a/b/c"));
  CHECK(same("a / b / c++", "a/b/c++"));
  CHECK(same("return /ab/", "return(/ab/)"));
  CHECK(same("x = a / b", "<refused>"));
  // A keyword that is also a name: the cell is elected by what follows.
  CHECK(same("go c", "go<c>"));
  CHECK(same("go\nc", "go c"));
  CHECK(same("go(c)", "<refused>"));  // the keyword, and `(` is no operand
  CHECK(same("go", "go"));
  CHECK(same("{ go }", "{go}"));
  CHECK(same("go c\ngo d", "go<c> go<d>"));
  CHECK(same("a = go c", "<refused>"));
  // A written semicolon stands on its own; none is inserted beside it.
  CHECK(same("a\n;", "a"));
  CHECK(same("a;;", "a ;"));
  CHECK(same(";\n;", "; ;"));
  // Never an empty statement of its own: one insertion per position.
  CHECK(same("a\nb\nc", "a b c"));
  CHECK(same("{\n}", "{}"));
  // A gap past one machine word reads the same.
  CHECK(same("a /* a comment long enough to hold more than sixty-four bytes \n"
             " of text */ b",
             "a b"));
  CHECK(same("a /* a comment long enough to hold more than sixty-four bytes "
             "of text */ b",
             "<refused>"));
  // A statement never opens on `{`: a block, whatever an object reads.
  CHECK(same("{ b } c", "{b} c"));
  CHECK(same("{ b }\nc", "{b} c"));
  CHECK(same("f({ b })", "f(=b)"));
  CHECK(same("{ b } { c }", "{b} {c}"));
  // A guarded reduce read on a literal's first byte is still guarded.
  CHECK(same("return\n{a}", "return {a}"));
  CHECK(same("return {a}", "return(=a)"));
  CHECK(same("go\n{a}", "go {a}"));
}

void spell_prec(const tasi::prec::ast::Expr* e, std::string& out) {
  using namespace tasi::prec::ast;
  if (const auto* const b = cursiv::ast_cast<Bin>(e)) {
    out += '(';
    spell_prec(b->left, out);
    out += b->op;
    spell_prec(b->right, out);
    out += ')';
  } else {
    out += cursiv::ast_cast<Name>(e)->name;
  }
}

bool prec(std::string_view text, std::string_view want) {
  const std::string doc = "prec " + std::string(text);
  const cursiv::ParseResult r = cases().parse(doc);
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty()) {
    got.clear();
    for (const auto* const item : r.value_as<tasi::prec::ast::Doc>()->items) {
      if (!got.empty()) got += ' ';
      spell_prec(item, got);
    }
  }
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

// Where the first statement's tree ends past the opening keyword, or -1
// where the parse complained.
long first_end(std::string_view text) {
  constexpr std::string_view head = "doc ";
  const std::string whole = std::string(head) + std::string(text);
  const cursiv::ParseResult r = cases().parse(whole);
  if (!r.accepted || !r.diagnostics.empty()) return -1;
  const auto* const doc = r.value_as<tasi::ast::Doc>();
  return doc->body.empty()
             ? -1
             : static_cast<long>(r.cst()[doc->body[0]->cstNodeId].end) -
                   static_cast<long>(head.size());
}

bool clean(const Case& c, std::string_view body) {
  const std::string text = c.text(body);
  const cursiv::ParseResult r = c.p.parse(text);
  return r.accepted && r.diagnostics.empty();
}

// Whether a diagnostic names a token the parse lays, which no author types.
bool names_laid(const Case& c, std::string_view body) {
  const cursiv::Parser& p = c.p;
  const std::string text = c.text(body);
  const cursiv::ParseResult r = p.parse(text);
  const auto laid = [&](cursiv::ElementId e) {
    return p.getGrammar().terminalName(e) == "implied";
  };
  for (const auto& d : r.diagnostics) {
    if (laid(d.element)) return true;
    for (const cursiv::ElementId e : d.also)
      if (laid(e)) return true;
  }
  return false;
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  static const auto parser = tasi::generated::G::create_parser();
  const Case spelled{*parser, ""};
  const Case more{cases(), "more "};
  current = &spelled;
  statements();
  // `gap::line` is `eol`: a line separator past ASCII is not one.
  CHECK(same("a /* \xE2\x80\xA8 */ b", "<refused>"));
  // Two implied tokens at one position, one of them a named terminal.
  CHECK(clean(more, "loop\nuse a;"));
  CHECK(clean(more, "loop:; use a;"));
  CHECK(clean(more, "loop\n"));
  CHECK(!clean(more, "loop"));
  CHECK(!clean(more, "loop use a;"));
  // A keyword read with its boundary: `inner` is a name, `in` is not.
  CHECK(clean(more, "use a inner;"));
  CHECK(clean(more, "use a in b;"));
  CHECK(clean(more, "use a inner in b;"));
  // ...and with its case: `.` is implied before `END`, not before `ender`.
  CHECK(clean(more, "begin a\nb END"));
  CHECK(!clean(more, "begin a b end"));
  CHECK(clean(more, "begin a. b. end"));
  CHECK(!clean(more, "begin a ender end"));
  // A statement a line break ends.
  CHECK(clean(more, "use a"));
  CHECK(clean(more, "use a\n"));
  CHECK(clean(more, "use a;\nuse b"));
  CHECK(clean(more, "use a\nuse b"));
  CHECK(!clean(more, "use a use b"));
  // Conditions composed in a rule.
  CHECK(clean(more, "tail a;"));
  CHECK(!clean(more, "tail\na;"));
  CHECK(clean(more, "call ();"));
  CHECK(!clean(more, "call\n();"));
  guarded_completion(more);
  // A guard before an optional tail, on either side of a line break.
  CHECK(clean(more, "bang a!\nbang b"));
  CHECK(clean(more, "bang a\nbang b"));
  CHECK(!clean(more, "bang a !\nbang b"));
  // A repair names the token an implied one stands for, never the implied.
  CHECK(!names_laid(more, "begin a b end"));
  CHECK(!names_laid(more, "loop x"));
  CHECK(!names_laid(more, "bang a ! x"));
  CHECK(!names_laid(spelled, "a b"));
  // A repair around an implied token finishes, and says something.
  {
    const std::string text = more.text("bang a !\nbang b ) use c\nuse d");
    const cursiv::ParseResult r = more.p.parse(text);
    CHECK(!r.diagnostics.empty());
  }
  // A class holds on the byte of a literal the grammar spells: it reads
  // exactly the token's bytes, whichever terminal the lexer chose.
  const Case brace{cases(), "brace "};
  current = &brace;
  CHECK(same("{a}", "{a}"));
  CHECK(same("{a;b}", "{a b}"));
  CHECK(same("{a b}", "<refused>"));
  CHECK(same("a", "a"));
  // A closing tag that opens on an operator ends the statement before it.
  const Case tag{cases(), "tag "};
  current = &tag;
  CHECK(same("<? a ?>", "a"));
  CHECK(same("<? a ? b : c ?>", "a(b,c)"));
  CHECK(same("<? a; b ?>", "a b"));
  CHECK(same("<? a b ?>", "<refused>"));
  const Case namedtag{cases(), "namedtag "};
  current = &namedtag;
  CHECK(same("<? a ?>", "a"));
  CHECK(same("<? a ?>\n", "a"));
  CHECK(same("<? a ? b : c ?>\r\n", "a(b,c)"));
  CHECK(same("<? a; b ?>\n", "a b"));
  CHECK(same("<? a b ?>", "<refused>"));
  current = &spelled;
  // The caret past an implied token reads on, trivia behind it or not.
  CHECK(offers_at(spelled, "a\nb|", "("));
  CHECK(offers_at(spelled, "{a\nb|", "("));
  CHECK(offers_at(spelled, "a /* c */\nb|", "("));
  CHECK(offers_at(spelled, "a\nreturn |", "{"));
  // An operand a guard refuses past the line: the implied `;` ends the return.
  CHECK(names_at(spelled, "return\n|x", "ID"));
  // Behind a line break the caret is offered what the implied `;` leads to,
  // and never the implied token itself; on one line only what it closes.
  CHECK(offers_at(spelled, "a\n|", "return"));
  CHECK(offers_at(spelled, "a\n|", "{"));
  CHECK(names_at(spelled, "a\n|", "ID"));
  CHECK(!names_at(spelled, "a\n|", "implied"));
  CHECK(offers_at(spelled, "{ a |", "}"));
  CHECK(!offers_at(spelled, "a |", "return"));
  CHECK(!names_at(spelled, "a |", "ID"));
  // Two implied tokens owed at one position, and one before a keyword.
  CHECK(offers_at(more, "loop\n|", "bang"));
  CHECK(offers_at(more, "begin a |", "end"));
  // Behind the implied tokens `loop` owes, `end` glued to `begin` would lex
  // as one word.
  CHECK(offers_at(more, "loop\nbegin |", "end") &&
        !offers_at(more, "loop\nbegin|", "end"));
  // The drive met the missing `)` with the implied `;` in hand, reduced by
  // it: the replay finds the invented token where the drive did.
  CHECK(offers_at(spelled, "{a(b}|", "return"));
  CHECK(offers_at(spelled, "{a(b} |", "return"));
  // An implied `;` the tape laid before dropped text closed the statement.
  CHECK(offers_at(spelled, "a\n@@ |", "return"));
  CHECK(!offers_at(spelled, "a\n@@ |", "("));
  // A statement that ends on no token wide ends where its last token did.
  CHECK(first_end("a /*c*/\nb") == 1);
  CHECK(first_end("a; /*c*/\nb") == 2);
  CHECK(first_end("@x /*c*/ @y 2") == 2);
  CHECK(first_end("@x 1 /*c*/ @y 2") == 4);
  // Precedence through operators that open on a condition.
  CHECK(prec("a+b*c", "(a+(b*c))"));
  CHECK(prec("a*b+c", "((a*b)+c)"));
  CHECK(prec("a+b+c", "((a+b)+c)"));
  CHECK(prec("a*b*c+d*e", "(((a*b)*c)+(d*e))"));
  CHECK(prec("a\nb", "a b"));
  // The gaps around an operator decide which operator it is.
  CHECK(ops("a-b", "(a-b)"));
  CHECK(ops("a - b", "(a-b)"));
  CHECK(ops("a -b", "a (-b)"));
  CHECK(ops("a- b", "(a-) b"));
  CHECK(ops("a-b-c", "(a-(b-c))"));
  CHECK(ops("a - b - c", "(a-(b-c))"));
  CHECK(ops("-a", "(-a)"));
  CHECK(ops("a /* c */ -b", "a (-b)"));
  CHECK(ops("a\n-b", "a (-b)"));
  CHECK(ops("a- -b", "(a-) (-b)"));
  CHECK(ops("a- - b", "((a-)-b)"));
  CHECK(tail("a-b;", "a-b"));
  CHECK(tail("a - b;", "a-b"));
  CHECK(tail("a - b + c;", "a-b+c"));
  CHECK(tail("a -b;", "<refused>"));
  CHECK(tail("a- b;", "<refused>"));
  CHECK(block("x", "x"));
  CHECK(block("{ x; }", "{x}"));
  CHECK(block("{ x }", "{x}"));
  CHECK(block("{ x\n}", "{x}"));
  CHECK(block("y; { x }", "y {x}"));
  CHECK(block("{ { x } }", "{{x}}"));
  CHECK(block("a = b", "<refused>"));
  CHECK(block("{ a } = b", "<refused>"));
  CHECK(optgap("go x; go !y;") == "x y");
  CHECK(optgap("go\nx;") == "<repaired>");
  CHECK(optgap("go\n!x;") == "<repaired>");
  // The repair's reading refuses the empty option past the line as the drive
  // does: no name is read after it, and the rest of the line is given up.
  {
    const Case opt{cases(), "optgap "};
    const std::string text = opt.text("go\nx;");
    const cursiv::ParseResult r = opt.p.parse(text);
    const auto at = static_cast<cursiv::TextOffset>(opt.head.size());
    CHECK(r.accepted && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::replaced &&
          r.diagnostics[0].begin == at + 3 && r.diagnostics[0].end == at + 5);
  }
  // The state's entry reads a literal the empty option's guarded goto
  // answers as the dispatch does: refused past a line break, then repaired.
  {
    const Case close{cases(), "close "};
    CHECK(clean(close, "a !; b;"));
    for (const std::string_view body : {"a\n;", "a;\nb\n;\nc;"}) {
      const std::string text = close.text(body);
      const cursiv::ParseResult r = close.p.parse(text);
      CHECK(r.accepted && r.diagnostics.size() == 1);
    }
    CHECK(offers_at(close, "a |", ";") && !offers_at(close, "a\n|", ";"));
  }
  // The goto that closes a rule lands by what held where the rule opened.
  {
    const Case held{cases(), "held "};
    CHECK(offers_at(held, "ab|", "x") && !offers_at(held, "ab|", "y"));
    CHECK(offers_at(held, "\nab|", "y") && !offers_at(held, "\nab|", "x"));
  }
  // A comment the tree keeps, behind an implied `;`, is recorded once.
  {
    for (const std::string_view body :
         {"b/**/", "b /**/\nc", "b/**//**/", "b c /**/"}) {
      const std::string text = "doc " + std::string(body);
      const cursiv::ParseResult r = cases().parse(text);
      unsigned breaches = 0;
      cursiv::test::audit_cst_structure(r, text, [&](const char* m) {
        std::printf("  %s: %s\n", text.c_str(), m);
        ++breaches;
      });
      CHECK(breaches == 0);
    }
  }
  return cursiv::test::report("LrAsi_test");
}
