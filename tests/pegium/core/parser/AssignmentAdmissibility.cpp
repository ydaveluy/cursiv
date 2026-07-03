#include <gtest/gtest.h>

#include <pegium/core/parser/AssignmentHelpers.hpp>
#include <pegium/core/parser/Nest.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/Reference.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Compile-time probes pinning the assign<>/append<>/nest<> admissibility gate
// (helpers::IsValidAssignment and its named cases in AssignmentHelpers.hpp).
// Each named case gets a positive probe; the historical clause-5 over-admission
// bug gets a negative probe per container shape it once leaked through.

using namespace pegium;
using namespace pegium::parser;

namespace {

struct Foo : AstNode {};
struct Bar : Foo {};
enum class Color : int { Red, Green };
struct PlainStruct {
  std::string name;
};

struct NestChild : AstNode {};
struct NestHost : AstNode {
  NestChild *child;  // AST-pointer feature — a valid nest<> target
  std::string label; // non-AST feature — nest<> must reject this
};

struct Node : AstNode {
  int i;
  double wide;
  bool flag;
  std::string text;
  Color color;
  Foo *astPtr;                     // pointer<Foo>
  std::vector<Foo *> astPtrs;      // append target
  Reference<Foo> ref;
  MultiReference<Foo> multiRef;
  std::vector<Reference<Foo>> refs;
  std::optional<Reference<Foo>> optRef;
  std::variant<bool, Foo *> variant;
  std::unique_ptr<Foo> owned;
  PlainStruct *plainPtr;           // pointer<non-AstNode>
  // vector-of-primitive append targets
  std::vector<int> vInt;
  std::vector<long> vLong;
  std::vector<float> vFloat;
  std::vector<double> vDouble;
  std::vector<char> vChar;
  std::vector<std::string> vString;
  std::vector<Color> vEnum;
  std::vector<bool> vBool;
  // additional variant shapes
  std::variant<int, double> vIntDouble;
  std::variant<int, std::string> vIntStr;
  std::variant<Color, int> vColorInt;
  std::variant<Foo *, Bar *> vFooBar;
  std::variant<std::string, Foo *> vStrFoo;
};

template <class T> struct Elem {
  using type = T;
};

template <auto feature, class T>
constexpr bool admits = helpers::IsValidAssignment<feature, Elem<T>>::value;

// classify_assignment<feature, T>() restates assignment_admitted at finer
// grain: assignment_admitted is defined as
// `classify_assignment(...) != Inadmissible`, so every admits<>/!admits<>
// cell above and below already exercises it (a wrong priority order or a
// mismatched concept call would fail one of those static_asserts). The pins
// below additionally name, for one representative cell per case, WHICH
// action was chosen -- not just that one was.
template <auto feature, class T>
constexpr auto classify = helpers::classify_assignment<feature, T>();

using Action = helpers::AssignmentAction;

static_assert(classify<&Node::astPtr, Foo> == Action::AstPointerUpcast);
static_assert(classify<&Node::i, int> == Action::WriteExact);
static_assert(classify<&Node::wide, int> == Action::ConvertScalar);
static_assert(classify<&Node::text, std::string_view> == Action::ConstructScalar);
static_assert(classify<&Node::refs, std::string_view> ==
             Action::ReferenceFromText); // AttrType == Reference<Foo>
static_assert(classify<&Node::owned, Foo> == Action::PointerHolderFromValue);
static_assert(classify<&Node::flag, Foo> == Action::BoolSetTrue);
static_assert(classify<&Node::vIntDouble, int> == Action::VariantExact);
static_assert(classify<&Node::vColorInt, int> == Action::EnumFromIntegral);
static_assert(classify<&Node::vIntStr, std::string_view> ==
             Action::StringFromView);
static_assert(classify<&Node::vFooBar, Foo> == Action::VariantFromAstPointer);
static_assert(classify<&Node::i, std::string> == Action::Inadmissible);

// --- AstPointerUpcast: an AST value goes into an AST-pointer member ----------
static_assert(admits<&Node::astPtr, Foo>);
static_assert(admits<&Node::astPtr, Bar>);   // derived value upcasts
static_assert(admits<&Node::astPtrs, Bar>);  // vector<T*> append

// --- ConvertibleScalar / ConstructibleScalar --------------------------------
static_assert(admits<&Node::i, int>);
static_assert(admits<&Node::wide, int>);          // widening conversion
static_assert(admits<&Node::text, std::string_view>);  // string from view
static_assert(admits<&Node::color, Color>);

// --- ReferenceFromString (incl. the vector<Reference<T>> shape) -------------
static_assert(admits<&Node::ref, std::string>);
static_assert(admits<&Node::ref, std::string_view>);
static_assert(admits<&Node::multiRef, std::string_view>);
static_assert(admits<&Node::refs, std::string_view>);   // AttrType == Reference<Foo>
static_assert(admits<&Node::optRef, std::string>);

// --- PointerHolderFromValue: holder built from a pointer to the value --------
static_assert(admits<&Node::variant, Foo>);   // variant<bool, Foo*> <- Foo
static_assert(admits<&Node::owned, Foo>);     // unique_ptr<Foo>     <- Foo
static_assert(admits<&Node::plainPtr, PlainStruct>);  // PlainStruct* <- PlainStruct

// --- BoolMember: a bool member accepts any element (set-true) ----------------
static_assert(admits<&Node::flag, std::string_view>);
static_assert(admits<&Node::flag, Foo>);
static_assert(admits<&Node::flag, int>);

// --- The fixed bug: clause-5 no longer admits assignments the runtime throws
//     on. `char` is fundamental, so its address is never a meaningful stored
//     pointer; every string-backed target below used to compile then throw. ---
static_assert(!admits<&Node::text, char>);
static_assert(!admits<&Node::ref, char>);
static_assert(!admits<&Node::multiRef, char>);
static_assert(!admits<&Node::refs, char>);
static_assert(!admits<&Node::optRef, char>);

// --- General rejections (unchanged by the refactor) -------------------------
static_assert(!admits<&Node::i, std::string>);
static_assert(!admits<&Node::astPtr, int>);

// --- vector<primitive> append targets: the unwrapped attribute type is the
//     element type, so admission mirrors the corresponding scalar. Vectors of
//     int / long / float / double / char / string / enum are supported. -------
static_assert(admits<&Node::vInt, int>);
static_assert(admits<&Node::vInt, double>);        // narrowing but convertible
static_assert(admits<&Node::vLong, int>);
static_assert(admits<&Node::vFloat, float>);
static_assert(admits<&Node::vFloat, int>);         // widening
static_assert(admits<&Node::vDouble, double>);
static_assert(admits<&Node::vDouble, int>);
static_assert(admits<&Node::vChar, char>);
static_assert(admits<&Node::vString, std::string>);
static_assert(admits<&Node::vString, std::string_view>);
static_assert(admits<&Node::vEnum, Color>);
static_assert(!admits<&Node::vInt, std::string>);
static_assert(!admits<&Node::vEnum, int>);         // enum-from-integral not gated

// --- vector<bool>: only the runtime-writable cases are admitted. The set-true
//     rule is keyed on the raw member type, so vector<bool> (unlike a scalar
//     bool) does NOT accept an arbitrary element -- only values convertible to
//     bool. Admitting more would compile then throw at parse time. ------------
static_assert(admits<&Node::vBool, int>);          // int -> bool (convertible)
static_assert(admits<&Node::vBool, bool>);
static_assert(!admits<&Node::vBool, std::string>);
static_assert(!admits<&Node::vBool, std::string_view>);
static_assert(!admits<&Node::vBool, Color>);
static_assert(!admits<&Node::vBool, Foo>);
// (a scalar bool member, by contrast, accepts any element -- set-true.)
static_assert(admits<&Node::flag, Foo>);
static_assert(admits<&Node::flag, std::string_view>);

// --- std::variant members: admission mirrors the runtime's exact-match
//     dispatch (assign_variant_exact / assign_variant_from_ast_ptr / the
//     string_view->string retry), NOT generic convertibility. -----------------
// exact alternative match
static_assert(admits<&Node::vIntDouble, int>);
static_assert(admits<&Node::vIntDouble, double>);
static_assert(admits<&Node::vIntStr, int>);
static_assert(admits<&Node::vIntStr, std::string>);
// string_view feeds a std::string alternative (the runtime retry)
static_assert(admits<&Node::vIntStr, std::string_view>);
static_assert(admits<&Node::vStrFoo, std::string_view>);
// integral feeds an enum alternative (enum-from-integral)
static_assert(admits<&Node::vColorInt, Color>);
static_assert(admits<&Node::vColorInt, int>);
static_assert(admits<&Node::vColorInt, bool>);     // bool integral -> Color alt
static_assert(admits<&Node::vColorInt, char>);
// AstNode value upcasts to an ast-pointer alternative
static_assert(admits<&Node::vFooBar, Foo>);
static_assert(admits<&Node::vFooBar, Bar>);
static_assert(admits<&Node::vStrFoo, Foo>);
static_assert(admits<&Node::vStrFoo, Bar>);
// A value only *convertible* to an alternative (not an exact match, no enum
// alternative) is REJECTED: the runtime never converts into a variant, so
// admitting these would compile then throw (assign_variant_exact returns false).
static_assert(!admits<&Node::vIntDouble, bool>);
static_assert(!admits<&Node::vIntDouble, char>);
static_assert(!admits<&Node::vIntStr, bool>);
static_assert(!admits<&Node::vIntStr, char>);
static_assert(!admits<&Node::vIntDouble, std::string>);
static_assert(!admits<&Node::vFooBar, int>);

// --- Nest routed through the gate: an AST-pointer feature is accepted, a
//     non-AST feature is rejected at the call site (clean diagnostic) --------
template <typename T, auto feature>
concept NestableInto = requires { nest<T, feature>(); };
template <auto feature>
concept NestableDeduced = requires { nest<feature>(); };

static_assert(NestableInto<NestHost, &NestHost::child>);
static_assert(NestableDeduced<&NestHost::child>);   // deduced host type
static_assert(!NestableInto<NestHost, &NestHost::label>);
static_assert(!NestableDeduced<&NestHost::label>);

} // namespace

TEST(AssignmentAdmissibilityTest, CompileTimeGateProbesHold) {
  // The behaviour is entirely in the static_asserts above; this anchors the
  // translation unit in the runtime suite.
  SUCCEED();
}
