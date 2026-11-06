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

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/AstExcused.hpp"
#include "cursiv/GrammarActions.hpp"

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
    std::vector<Frame> walk;
    enter(want, got, walk, excused, r);
    while (r.equal && !walk.empty()) {
      Frame& top = walk.back();
      if (top.next == top.a.size()) {
        walk.pop_back();
        continue;
      }
      const Child& ca = top.a[top.next];
      const Child& cb = top.b[top.next];
      if (ca.name != cb.name || ca.index != cb.index) {
        // Not entered: the way down stops at the node that holds them.
        const std::string at = path(std::span(walk).first(walk.size() - 1));
        fail(r, at.empty() ? "/" : at, step(at, ca), step(at, cb));
        break;
      }
      ++top.next;
      enter(*ca.node, *cb.node, walk, excused, r);
    }
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

  // A node whose children are being compared, and the next pair of them.
  struct Frame {
    std::vector<Child> a, b;
    std::size_t next = 0;
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

  static void append_step(std::string& at, const Child& c) {
    at += '/';
    at += c.name;
    if (c.index != AstReflection::kNoIndex)
      at += "[" + std::to_string(c.index) + "]";
  }

  [[nodiscard]] static std::string step(std::string at, const Child& c) {
    append_step(at, c);
    return at;
  }

  [[nodiscard]] static std::string list(std::string cls,
                                        const std::vector<Child>& xs) {
    cls += ':';
    for (const Child& c : xs) cls += ' ' + c.name;
    return xs.empty() ? cls + " none" : cls;
  }

  [[nodiscard]] static std::string shown(const Value& v) {
    switch (v.kind) {
      case ValueKind::unset:
        return v.name + " unset";
      case ValueKind::unreadable:
        return v.name + " set";
      case ValueKind::text:
        break;
    }
    return v.name + "=" + v.text;
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

  // The way down to the node last entered: each frame stands just past its
  // child on that way.
  [[nodiscard]] static std::string path(std::span<const Frame> walk) {
    std::string out;
    for (const Frame& f : walk) append_step(out, f.a[f.next - 1]);
    return out;
  }

  // Compares two nodes, and leaves their children on the walk.
  void enter(const AstNode& a, const AstNode& b, std::vector<Frame>& walk,
             std::span<const AstExcused> excused, Result& r) const {
    if (a.type != b.type) {
      const std::string at = path(walk);
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
      fail(r, path(walk),
           name_of(a) + ": " + std::to_string(va.size()) + " values",
           name_of(b) + ": " + std::to_string(vb.size()) + " values");
      return;
    }
    for (std::size_t i = 0; i < va.size(); ++i) {
      const Value& x = va[i];
      const Value& y = vb[i];
      // Whether an optional member holds a value reads even where the value
      // does not.
      const bool unset = x.kind == ValueKind::unset;
      if (unset != (y.kind == ValueKind::unset)) {
        fail(r, path(walk) + "/" + x.name, shown(x), shown(y));
        continue;
      }
      if (unset) continue;
      if (x.kind == ValueKind::unreadable || y.kind == ValueKind::unreadable) {
        r.blind.push_back(x.name);
        continue;
      }
      if (x.name != y.name || x.index != y.index || x.text != y.text)
        fail(r, path(walk) + "/" + x.name, shown(x), shown(y));
    }
    if (!r.equal) return;

    std::vector<Child> ca = children(a);
    std::vector<Child> cb = children(b);
    drop(excused, a, ca);
    drop(excused, a, cb);
    if (ca.size() != cb.size()) {
      const std::string at = path(walk);
      fail(r, at.empty() ? "/" : at, list(name_of(a), ca),
           list(name_of(b), cb));
      return;
    }
    if (!ca.empty()) walk.push_back(Frame{std::move(ca), std::move(cb)});
  }
};

}  // namespace cursiv::fuzzer
