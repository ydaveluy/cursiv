// A branch, or an optional item, that reads the same tokens as an earlier one
// of its choice is never chosen: both lowerings drop it, and the generator
// names it in a warning.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

class WarningFound;

// One twin, as image nodes: under the choice `at`, branch `arm` and the
// earlier branches that read all it reads; under the sequence `at`, optional
// item `arm` and the earlier item of its run that reads the same.
struct TwinFound {
  ImageId at = kNoImage;
  std::uint32_t arm = 0;
  std::vector<std::uint32_t> over;
  bool item = false;
};

// Twins are compared by the tokens they read, a call to a data rule read
// through; a region, a condition and any other call stand for themselves.
// Languages are not compared.
class TwinReader {
 public:
  // The body a call to `rule` reads in place, where the rule is a data rule of
  // the grammar itself; `kNoImage` for any other rule.
  using DataBody = std::function<ImageId(std::uint32_t rule)>;

  TwinReader(GrammarImage& image, DataBody dataBody);
  // A reader of the rules as the pipeline resolved them, as warnings read
  // them.
  static TwinReader resolved(GrammarImage& image);

  // `body` without the twins it holds. Each twin of the body as written is
  // added to `found` where given, the first time a node is read.
  ImageId drop(ImageId body, std::vector<TwinFound>* found = nullptr);

 private:
  // A position of a sequence as read: its shape, its node and, for an
  // optional item, the operand it reads when taken.
  struct Item {
    std::uint32_t key = 0;
    ImageId node = kNoImage;
    ImageId operand = kNoImage;
  };
  using Reading = std::vector<std::uint32_t>;

  ImageId child_at(ImageId id, std::uint32_t i) const {
    return _im.children[_im.nodes[id].first + i];
  }
  std::vector<ImageId> children(ImageId id) const;
  std::uint32_t intern(std::vector<std::uint64_t> shape);
  std::uint32_t text(std::string_view s);
  Descent<std::uint32_t> key(ImageId id);
  Descent<std::uint32_t> shape(ImageId id);
  Descent<std::vector<std::uint32_t>> branch_keys(ImageId choice);
  ImageId expanded(std::uint32_t rule);
  Descent<const std::vector<Item>*> called(std::uint32_t rule);
  void analyse_calls(std::uint32_t rule);
  Descent<void> flatten(ImageId id, std::vector<Item>& out);
  bool reads(const std::vector<Item>& items) const;
  Descent<std::vector<Reading>> readings(const std::vector<Item>& items);
  bool optional(ImageId id) const;
  Descent<bool> folds(ImageId id);
  Descent<bool> read_same(ImageId a, ImageId b);

  Descent<ImageId> drop_node(ImageId id);
  Descent<ImageId> drop_choice(ImageId id);
  Descent<ImageId> drop_sequence(ImageId id);
  Descent<ImageId> first_present(const std::vector<ImageId>& run);

  GrammarImage& _im;
  DataBody _dataBody;
  std::vector<TwinFound>* _found = nullptr;
  std::map<std::vector<std::uint64_t>, std::uint32_t> _shapes;
  std::map<std::string_view, std::uint32_t> _texts;
  // Each node's key plus one, zero until read.
  std::vector<std::uint32_t> _keys;
  struct DataRule {
    ImageId body = kNoImage;
    bool cyclic = false;
  };
  std::unordered_map<std::uint32_t, DataRule> _dataRules;
  std::vector<bool> _analysedNodes;
  std::unordered_map<std::uint32_t, std::vector<Item>> _calls;
  std::unordered_map<ImageId, bool> _folds;
  std::unordered_map<ImageId, ImageId> _dropped;
  // The runs read by their first item present that enclose the one in hand.
  unsigned _runs = 0;
};

struct TwinBranch {
  std::string rule;
  // As the dump spells it: the branch, or the optional item, never chosen.
  std::string branch;
  // The branch's place among the branches of its choice; zero for an item.
  std::uint16_t arm = 0, arms = 0;
  // The earlier branches it repeats, by place.
  std::vector<std::uint16_t> over;
  // The earlier optional item, as the dump spells it.
  std::string earlier;

  bool operator==(const TwinBranch&) const = default;
};

// Each twin once, in the order the rules were reached.
std::vector<TwinBranch> twin_branches(WarningFound& found);

std::string twin_branch_text(const TwinBranch& t);

}  // namespace cursiv::engine
