// Compares two ASTs node by node: type, value members, then child members,
// in order. Reports the path to the first divergence. A member neither side
// can read is named as blind, never treated as equal; `visitMembers` skips a
// node held in a variant member, so those are never compared at all.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/fuzz/AstExcused.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"

namespace cursiv::fuzzer {

struct AstCompare {
  const AstReflection& refl;
  const GrammarActions& act;

  struct Result {
    bool equal = true;
    std::string where;  // the path to the first divergence
    std::string want, got;
    // Members no reader reaches, by name: a blind spot of the instruments.
    std::vector<std::string> blind;
  };

  // `excused` names members the walk could not write; they are dropped from
  // both sides so a blind spot never counts as agreement.
  [[nodiscard]] Result run(const AstNode& want, const AstNode& got,
                           std::span<const AstExcused> excused = {}) const {
    Result r;
    node(want, got, "", excused, r);
    return r;
  }

 private:
  struct Value {
    std::string name;
    std::uint32_t index;
    ValueKind kind;
    std::string text;
  };
  struct Child {
    std::string name;
    std::uint32_t index;
    const AstNode* node;
  };

  [[nodiscard]] static bool skipped(std::span<const AstExcused> excused,
                                    const AstNode& mine,
                                    std::string_view name) {
    for (const AstExcused& x : excused)
      if (x.node == &mine && x.member == name) return true;
    return false;
  }

  static void fail(Result& r, std::string where, std::string want,
                   std::string got) {
    if (!r.equal) return;
    r.equal = false;
    r.where = std::move(where);
    r.want = std::move(want);
    r.got = std::move(got);
  }

  [[nodiscard]] static std::string step(const std::string& at, const Child& c) {
    std::string out = at + "/" + c.name;
    if (c.index != AstReflection::kNoIndex)
      out += "[" + std::to_string(c.index) + "]";
    return out;
  }

  [[nodiscard]] static std::string list(std::string cls,
                                        const std::vector<Child>& xs) {
    cls += ':';
    for (const Child& c : xs) cls += ' ' + c.name;
    return xs.empty() ? cls + " none" : cls;
  }

  [[nodiscard]] static std::string name_of(const AstNode& n) {
    return n.type != nullptr ? std::string(n.type->name) : std::string("?");
  }

  [[nodiscard]] std::vector<Value> values(const AstNode& n) const {
    std::vector<Value> out;
    act.visitValues(n, [&](std::string_view p, std::uint32_t i, ValueKind k,
                           std::string_view t) {
      out.push_back(Value{std::string(p), i, k, std::string(t)});
    });
    return out;
  }

  [[nodiscard]] std::vector<Child> children(const AstNode& n) const {
    std::vector<Child> out;
    refl.visitMembers(n, [&](std::string_view p, std::uint32_t i, AstNode& c) {
      out.push_back(Child{std::string(p), i, &c});
    });
    return out;
  }

  // An excused member is dropped from both lists: the walk never wrote it,
  // so neither side's value should count.
  template <class T>
  static void drop(std::span<const AstExcused> excused, const AstNode& mine,
                   std::vector<T>& xs) {
    std::size_t at = 0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
      if (skipped(excused, mine, xs[i].name)) continue;
      if (at != i) xs[at] = std::move(xs[i]);
      ++at;
    }
    xs.resize(at);
  }

  void node(const AstNode& a, const AstNode& b, const std::string& at,
            std::span<const AstExcused> excused, Result& r) const {
    if (!r.equal) return;
    if (a.type != b.type) {
      fail(r, at.empty() ? "/" : at, name_of(a), name_of(b));
      return;
    }
    std::vector<Value> va = values(a);
    std::vector<Value> vb = values(b);
    drop(excused, a, va);
    drop(excused, a, vb);
    // The two walks read one table in one order: a length that differs is the
    // table disagreeing with itself, not a tree.
    if (va.size() != vb.size()) {
      fail(r, at, name_of(a) + ": " + std::to_string(va.size()) + " values",
           name_of(b) + ": " + std::to_string(vb.size()) + " values");
      return;
    }
    for (std::size_t i = 0; i < va.size(); ++i) {
      if (va[i].kind == ValueKind::unreadable ||
          vb[i].kind == ValueKind::unreadable) {
        r.blind.push_back(va[i].name);
        continue;
      }
      if (va[i].name != vb[i].name || va[i].index != vb[i].index ||
          va[i].text != vb[i].text)
        fail(r, at + "/" + va[i].name, va[i].name + "=" + va[i].text,
             vb[i].name + "=" + vb[i].text);
    }
    if (!r.equal) return;

    std::vector<Child> ca = children(a);
    std::vector<Child> cb = children(b);
    drop(excused, a, ca);
    drop(excused, a, cb);
    if (ca.size() != cb.size()) {
      fail(r, at.empty() ? "/" : at, list(name_of(a), ca),
           list(name_of(b), cb));
      return;
    }
    for (std::size_t i = 0; i < ca.size(); ++i) {
      if (ca[i].name != cb[i].name || ca[i].index != cb[i].index) {
        fail(r, at.empty() ? "/" : at, step(at, ca[i]), step(at, cb[i]));
        return;
      }
      node(*ca[i].node, *cb[i].node, step(at, ca[i]), excused, r);
      if (!r.equal) return;
    }
  }
};

}  // namespace cursiv::fuzzer
