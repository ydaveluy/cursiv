#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "cursiv/utils/Pause.hpp"

namespace cursiv::detail {

template <class K, class V, class Hash, class KeyEqual>
class ConcurrentTable;

// Process-wide epoch-based reclamation, the tables' own: nothing else reads
// it. A reader announces the epoch it entered in a slot of its own thread,
// so reading writes nothing another thread touches; what a writer takes out
// of reach is retired with the epoch of that moment and freed once every
// reader has announced a later epoch, or none. One manager for every table,
// so a thread holds one slot whatever it reads. A thread that finds every
// slot held reads all the same, and holds back every collection while it
// does.
class TableEpochs {
  template <class, class, class, class>
  friend class ConcurrentTable;

  static constexpr std::uint64_t kIdle = ~std::uint64_t{0};

  // What a retirement leaves for the collection, kept in what it retires:
  // retiring asks for no memory, so what is out of reach is never lost.
  struct Retired {
    Retired* next = nullptr;
    void* object = nullptr;
    void (*free)(void*) = nullptr;
    std::uint64_t epoch = 0;
  };

  // A reader's stay. Only the outermost guard of a thread announces: a read
  // that reads through another costs nothing more.
  class Guard {
   public:
    Guard() noexcept {
      Seat& s = seat;
      if (s.depth++ != 0) return;
      if (s.slot == nullptr) [[unlikely]] {
        s.slot = takeSlot();
        if (s.slot == nullptr) {
          enterWithoutSlot();
          return;
        }
      }
      // The epoch read with acquire: a reader that sees one `retire` moved
      // on to sees what that writer took out of reach. The announcement must
      // be seen by a writer's scan before this reader loads anything: where
      // the system can fence every thread at once, the writer pays for that
      // and this is a plain store; elsewhere the store fences itself.
      const std::uint64_t epoch = current.load(std::memory_order_acquire);
      if (asymmetric) [[likely]] {
        s.slot->store(epoch, std::memory_order_relaxed);
        std::atomic_signal_fence(std::memory_order_seq_cst);
      } else {
        s.slot->exchange(epoch, std::memory_order_seq_cst);
      }
    }
    ~Guard() {
      Seat& s = seat;
      if (--s.depth != 0) return;
      if (s.slot == nullptr) [[unlikely]] {
        leaveWithoutSlot();
        return;
      }
      s.slot->store(kIdle, std::memory_order_release);
      if (s.exiting) [[unlikely]]
        giveBack(s);
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
  };

  // `object` is already out of every new reader's reach: it is freed once the
  // readers that may still hold it have left. `record` lives in `object` and
  // is used once. Writers retire rarely -- a table grown or emptied -- so
  // this takes a lock, and collects once enough have come since the last
  // collection, in number or in `bytes`, the room `object` takes.
  static void retire(Retired& record, void* object, void (*free)(void*),
                     std::size_t bytes) noexcept;

  // Trivial, and declared `constinit`, so reading it costs no initialisation
  // check. Defined once, in the source file: an inline thread-local variable
  // gets an access routine in every object file, which Apple's linker refuses
  // as a duplicate. The slot is given back at thread exit by a companion of
  // `takeSlot`; a guard taken after that, from another thread-local
  // destructor, holds a slot for its stay alone.
  struct Seat {
    std::atomic<std::uint64_t>* slot = nullptr;
    std::uint32_t depth = 0;
    bool exiting = false;
  };
  static constinit thread_local Seat seat;
  static std::atomic<std::uint64_t> current;
  // Set once, before any reader, when the process can fence all its threads.
  static const bool asymmetric;

  // Gives the slot back at thread exit; defined where it is taken.
  struct Release;
  static thread_local Release release;

  // The slots and the retired list; defined in the source file.
  struct State;
  static State& state() noexcept;

  // Null when every slot is held.
  static std::atomic<std::uint64_t>* takeSlot() noexcept;
  static void giveBack(Seat& s) noexcept;
  static void enterWithoutSlot() noexcept;
  static void leaveWithoutSlot() noexcept;
};

inline std::atomic<std::uint64_t> TableEpochs::current{0};

// A hash table read without a lock and without a write: what a key maps to is
// set once, the first value stored wins, and it never changes until `clear`
// drops them all. Slots come in groups of eight, each group on lines of its
// own with a control byte per slot, read as one word: a lookup compares a
// group at once instead of looping slot by slot, and inserts into different
// groups touch different lines. An insert claims a slot with one
// compare-exchange and announces the written entry with a plain store. A
// small pair that copies trivially and without throwing is kept in the slot
// itself, and a growth copies it; anything else lives in an entry of its own
// the slot points to. A thread that would insert during a growth helps copy
// rather than wait. Each call reads under a guard of its own, and hands out
// copies: what the table holds may be freed once the guard is gone.
template <class K, class V, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class ConcurrentTable {
  // A key that is not a scalar may cost to hash, and a growth rehashes what
  // it copies: such a pair keeps its hash.
  static constexpr bool kKeepHash = !std::is_scalar_v<K>;
  struct Pair {
    K key;
    V value;
  };
  struct HashedPair {
    std::uint64_t hash;
    K key;
    V value;
  };
  using Slotted = std::conditional_t<kKeepHash, HashedPair, Pair>;
  // Copied from const references, so no other constructor can be chosen,
  // and never throwing once a slot is claimed: a claimed slot always ends
  // ready.
  static constexpr bool kInline =
      std::is_trivially_copyable_v<K> && std::is_trivially_copyable_v<V> &&
      std::is_trivially_destructible_v<K> &&
      std::is_trivially_destructible_v<V> &&
      std::is_nothrow_copy_constructible_v<K> &&
      std::is_nothrow_copy_constructible_v<V> && sizeof(Slotted) <= 32;

  struct Node {
    std::uint64_t hash;
    K key;
    V value;
  };
  using Item = std::conditional_t<kInline, Slotted, Node*>;

  // A control byte: free, sealed by a growth, or claimed with seven bits of
  // the hash. A claimed slot's entry is readable once its ready byte is set.
  // Bit seven tells a claimed slot, and bit five a sealed one from a free
  // one: a whole group's word is read with a few masks.
  static constexpr std::uint8_t kEmpty = 0x00;
  static constexpr std::uint8_t kSealed = 0x20;
  static constexpr std::uint8_t kFull = 0x80;
  static_assert((kEmpty & 0xa0) == 0 && (kSealed & 0xa0) == 0x20 &&
                (kFull & 0x80) != 0);
  static constexpr std::uint64_t kLows = 0x0101010101010101ull;
  static constexpr std::uint64_t kHighs = 0x8080808080808080ull;

  static constexpr std::size_t kGroup = 8;
  static constexpr std::size_t kMinGroups = 2;
  // Groups a helper copies at a time.
  static constexpr std::size_t kBlock = 32;
  // Full groups are counted on at most this many counts, a group on the one
  // its low bits pick: each covers an even share of the table, and a growth
  // reads a few counts, however large the table. A count moves only when an
  // insert takes a group's last free slot, so it is written once per group,
  // not per insert. A quarter of a count's groups full, or a probe longer
  // than the limit, asks for a growth; it happens if a quarter of the table's
  // groups are full, which a spread hash reaches at about seven tenths of the
  // slots. Keys of one hash fill neighbouring groups and grow nothing:
  // doubling would not part them, so their probe goes on instead.
  static constexpr std::size_t kCounts = 32;
  static constexpr std::size_t kMaxProbes = 16;
  // Below this many groups a growth quadruples: a table filled from nothing
  // copies a third of its entries instead of all of them, for at most four
  // times the room it needs; past it, doubling bounds that room.
  static constexpr std::size_t kQuadrupleBelow = 4096;

  struct alignas(64) Count {
    std::atomic<std::uint32_t> fullGroups{0};
  };

  struct alignas(64) Group {
    std::atomic<std::uint64_t> ctrl{0};
    std::atomic<std::uint8_t> ready[kGroup]{};
    alignas(Item) unsigned char storage[kGroup * sizeof(Item)];

    [[nodiscard]] Item* item(std::size_t at) noexcept {
      return std::launder(reinterpret_cast<Item*>(storage)) + at;
    }
    [[nodiscard]] const Item* item(std::size_t at) const noexcept {
      return std::launder(reinterpret_cast<const Item*>(storage)) + at;
    }
    // The entry at `at`, once its writer is done.
    [[nodiscard]] const Item* readyItem(std::size_t at) const noexcept {
      return ready[at].load(std::memory_order_acquire) != 0 ? item(at)
                                                            : nullptr;
    }
    void waitReady(std::size_t at) const noexcept {
      for (unsigned spins = 0; ready[at].load(std::memory_order_acquire) == 0;)
        relax(spins);
    }
  };

  // Header, counts and groups in one allocation.
  struct alignas(64) Table {
    // Read by every operation, and never written: on a line of its own.
    std::uint64_t generation;
    std::size_t mask;       // groups - 1
    std::size_t countMask;  // counts - 1
    // A growth under way: the successor, the blocks handed out, the blocks
    // copied. A successor not yet published belongs to this table and is
    // freed with it. Its own line, as helpers write it block after block.
    alignas(64) std::atomic<Table*> next{nullptr};
    std::atomic<std::size_t> claimed{0};
    std::atomic<std::size_t> copied{0};
    // Written once, when a clear or a growth takes this table out.
    TableEpochs::Retired retired;

    [[nodiscard]] static Table* make(std::uint64_t g, std::size_t groups) {
      const std::size_t counts =
          std::min((groups + kBlock - 1) / kBlock, kCounts);
      void* const raw = ::operator new(
          groupsOffset(counts) + groups * sizeof(Group), std::align_val_t{64});
      auto* const t = new (raw) Table(g, groups - 1, counts - 1);
      for (std::size_t c = 0; c < counts; ++c) new (&t->counts()[c]) Count;
      for (std::size_t at = 0; at < groups; ++at) new (&t->group(at)) Group;
      return t;
    }
    static void unmake(Table* t) noexcept {
      t->~Table();
      ::operator delete(t, std::align_val_t{64});
    }
    [[nodiscard]] std::size_t groups() const noexcept { return mask + 1; }
    [[nodiscard]] std::size_t blocks() const noexcept {
      return (groups() + kBlock - 1) / kBlock;
    }
    [[nodiscard]] std::uint32_t countLimit() const noexcept {
      return static_cast<std::uint32_t>(
          std::max<std::size_t>(1, groups() / (countMask + 1) / 4));
    }
    [[nodiscard]] std::size_t fullGroups() noexcept {
      std::size_t full = 0;
      for (std::size_t c = 0; c <= countMask; ++c)
        full += counts()[c].fullGroups.load(std::memory_order_relaxed);
      return full;
    }
    // The count of group `g`.
    [[nodiscard]] Count& countOf(std::size_t g) noexcept {
      return counts()[g & countMask];
    }
    [[nodiscard]] Group& group(std::size_t at) noexcept {
      return std::launder(
          reinterpret_cast<Group*>(bytes() + groupsOffset(countMask + 1)))[at];
    }
    [[nodiscard]] const Group& group(std::size_t at) const noexcept {
      return const_cast<Table*>(this)->group(at);
    }
    // The table's own allocation; entries kept apart are not counted.
    [[nodiscard]] std::size_t allocated() const noexcept {
      return groupsOffset(countMask + 1) + groups() * sizeof(Group);
    }

   private:
    Table(std::uint64_t g, std::size_t m, std::size_t c)
        : generation(g), mask(m), countMask(c) {}
    [[nodiscard]] char* bytes() noexcept {
      return reinterpret_cast<char*>(this);
    }
    [[nodiscard]] Count* counts() noexcept {
      return std::launder(reinterpret_cast<Count*>(bytes() + sizeof(Table)));
    }
    [[nodiscard]] static constexpr std::size_t groupsOffset(
        std::size_t counts) noexcept {
      return sizeof(Table) + counts * sizeof(Count);
    }
  };

 public:
  ConcurrentTable() : _table(Table::make(0, kMinGroups)) {}
  ~ConcurrentTable() { destroy_with_entries(_table.load()); }
  ConcurrentTable(const ConcurrentTable&) = delete;
  ConcurrentTable& operator=(const ConcurrentTable&) = delete;

  // Moved by every `clear`: a value computed under an earlier one is stale.
  [[nodiscard]] std::uint64_t generation() const noexcept {
    const TableEpochs::Guard guard;
    return _table.load(std::memory_order_seq_cst)->generation;
  }

  [[nodiscard]] std::optional<V> find(const K& key) const {
    const TableEpochs::Guard guard;
    if (const V* found = lookup(key)) return *found;
    return std::nullopt;
  }

  // Stores `value` under `key` unless the key already has one, which is then
  // the answer. Nothing, with `value` left in place, when a `clear` has
  // passed since `generation` was read.
  [[nodiscard]] std::optional<V> insert(std::uint64_t generation, const K& key,
                                        V& value) {
    const TableEpochs::Guard guard;
    if (const V* kept = claim(generation, key, value)) return *kept;
    return std::nullopt;
  }

  // What `key` holds, or else what `provider` answers, stored unless a value
  // got there first, which is then the answer. The provider runs under no
  // guard and no lock. A `clear` that passes while it runs keeps its answer
  // out of the table; the caller still gets it.
  template <class Provider>
  [[nodiscard]] V findOrCompute(const K& key, Provider&& provider) {
    if (std::optional<V> found = find(key)) return std::move(*found);
    // Read after the miss and before the provider: a clear from here on
    // keeps its answer out.
    const std::uint64_t before = generation();
    V computed = std::invoke(std::forward<Provider>(provider));
    if (std::optional<V> kept = insert(before, key, computed))
      return std::move(*kept);
    return computed;
  }

  // Every entry, out of new readers' reach at once; freed once the old ones
  // have left. `keepCapacity` is for a table about to be filled again, as a
  // cache emptied for a rebuild is: it starts at the old size and grows no
  // more. Without it, the table goes back to its smallest.
  void clear(bool keepCapacity = false) {
    // Guarded too: a growth may retire the current table while it is read.
    const TableEpochs::Guard guard;
    const std::scoped_lock lock(_writers);
    // Sequentially consistent, as a read's: a growth may retire this table
    // meanwhile, and the guard keeps it only for loads in that order.
    const Table* current = _table.load(std::memory_order_seq_cst);
    const std::size_t groups = keepCapacity ? current->groups() : kMinGroups;
    // An exchange, not a store: a growth may publish its successor meanwhile,
    // and whichever table this takes out is the one whose entries go.
    Table* old = _table.exchange(Table::make(current->generation + 1, groups),
                                 std::memory_order_seq_cst);
    TableEpochs::retire(old->retired, old, &destroy_with_entries_erased,
                        old->allocated());
  }

  // Every value, in no particular order.
  template <class Fn>
  void forEach(Fn&& fn) const {
    const TableEpochs::Guard guard;
    const Table* t = _table.load(std::memory_order_seq_cst);
    for (std::size_t g = 0; g <= t->mask; ++g) {
      const Group& group = t->group(g);
      const std::uint64_t word = group.ctrl.load(std::memory_order_acquire);
      for (std::uint64_t m = word & kHighs; m != 0; m &= m - 1)
        if (const Item* item = group.readyItem(byte_of(m)))
          fn(item_value(*item));
    }
  }

 private:
  // What `key` holds, read under the caller's guard.
  [[nodiscard]] const V* lookup(const K& key) const {
    const std::uint64_t hash = mixed(key);
    const std::uint8_t tag = tag_of(hash);
    const Table* t = _table.load(std::memory_order_seq_cst);
    std::size_t g = static_cast<std::size_t>(hash) & t->mask;
    for (std::size_t probes = 0; probes <= t->mask;
         ++probes, g = (g + 1) & t->mask) {
      const Group& group = t->group(g);
      const std::uint64_t word = group.ctrl.load(std::memory_order_acquire);
      for (std::uint64_t m = bytes_equal(word, tag); m != 0; m &= m - 1)
        if (const Item* item = group.readyItem(byte_of(m)))
          if (const V* v = value_if(*item, hash, key)) return v;
      if (ends(word)) return nullptr;
    }
    return nullptr;
  }

  // `insert` under the caller's guard: what it returns stays readable for
  // that guard, but its address is no identity, a growth may give the same
  // key another.
  const V* claim(std::uint64_t generation, const K& key, V& value) {
    const std::uint64_t hash = mixed(key);
    const std::uint8_t tag = tag_of(hash);
    // Allocated before a slot is claimed: a claimed slot always ends ready.
    std::unique_ptr<Node> fresh;
    for (;;) {
      Table* t = _table.load(std::memory_order_seq_cst);
      if (t->generation != generation) {
        if constexpr (!kInline)
          if (fresh) value = std::move(fresh->value);
        return nullptr;
      }
      bool crowded = true;
      if (t->next.load(std::memory_order_acquire) == nullptr) {
        std::size_t g = static_cast<std::size_t>(hash) & t->mask;
        for (std::size_t probes = 0; probes < t->groups();
             ++probes, g = (g + 1) & t->mask) {
          // A long probe asks for a growth; refused, it goes on.
          if (probes == kMaxProbes && startGrowth(t)) break;
          Group& group = t->group(g);
          std::uint64_t word = group.ctrl.load(std::memory_order_acquire);
          for (;;) {
            // A slot with this tag may hold this key, still being written:
            // its writer finishes first.
            for (std::uint64_t m = bytes_equal(word, tag); m != 0; m &= m - 1) {
              const std::size_t at = byte_of(m);
              group.waitReady(at);
              if (const V* v = value_if(*group.item(at), hash, key)) return v;
            }
            const std::uint64_t free = free_of(word);
            if (free == 0) break;
            const std::size_t at = byte_of(free);
            if constexpr (!kInline)
              if (!fresh) fresh.reset(new Node{hash, key, std::move(value)});
            const std::uint64_t claimed =
                word | (std::uint64_t{tag} << (8 * at));
            if (!group.ctrl.compare_exchange_weak(word, claimed,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
              continue;
            const V* stored = nullptr;
            if constexpr (kInline) {
              // Members built from the arguments themselves: a temporary
              // moved in could pick a constructor that throws.
              if constexpr (kKeepHash)
                std::construct_at(group.item(at), hash, std::as_const(key),
                                  std::as_const(value));
              else
                std::construct_at(group.item(at), std::as_const(key),
                                  std::as_const(value));
              stored = &group.item(at)->value;
            } else {
              std::construct_at(group.item(at), fresh.get());
              stored = &fresh.release()->value;
            }
            group.ready[at].store(1, std::memory_order_release);
            // A growth this insert starts is finished here: left to the next
            // insert, a fill that ends on it would keep a successor it never
            // uses. What `stored` points to stays readable for the guard.
            if (free_of(claimed) == 0 &&
                t->countOf(g).fullGroups.fetch_add(1,
                                                   std::memory_order_relaxed) +
                        1 >
                    t->countLimit() &&
                startGrowth(t))
              help(t);
            return stored;
          }
          if (has_sealed(word)) {
            crowded = false;
            break;
          }
        }
      } else {
        crowded = false;
      }
      // Every group full: the table has to grow, whatever the counts say.
      if (crowded) startGrowth(t, true);
      help(t);
    }
  }

  // Spread first: a standard hash of an integer or a pointer is often the
  // value itself, and neighbouring keys would land in one group. Sixty-four
  // bits by declaration, not by the width `std::size_t` happens to have: on
  // a 32-bit target the tag's bits would not exist.
  [[nodiscard]] static std::uint64_t mixed(const K& key) noexcept {
    std::uint64_t h = Hash{}(key);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
  }
  // The top bits: the group comes from the bottom ones.
  [[nodiscard]] static std::uint8_t tag_of(std::uint64_t hash) noexcept {
    return static_cast<std::uint8_t>(kFull | (hash >> 57));
  }
  // A high bit in each byte of `word` equal to `b`, exactly: no borrow runs
  // from one byte into the next.
  [[nodiscard]] static std::uint64_t bytes_equal(std::uint64_t word,
                                                 std::uint8_t b) noexcept {
    const std::uint64_t x = word ^ (kLows * b);
    return ~(((x & ~kHighs) + ~kHighs) | x | ~kHighs);
  }
  // A high bit in each byte of `word` that is free.
  [[nodiscard]] static std::uint64_t free_of(std::uint64_t word) noexcept {
    return ~word & ~(word << 2) & kHighs;
  }
  [[nodiscard]] static bool has_sealed(std::uint64_t word) noexcept {
    return (~word & (word << 2) & kHighs) != 0;
  }
  // A probe ends at a group with a slot never claimed, free or sealed.
  [[nodiscard]] static bool ends(std::uint64_t word) noexcept {
    return (~word & kHighs) != 0;
  }
  [[nodiscard]] static std::size_t byte_of(std::uint64_t mask) noexcept {
    return static_cast<std::size_t>(std::countr_zero(mask)) / 8;
  }
  // A wait on another thread's few instructions: spin, then let it run if
  // it was preempted.
  static void relax(unsigned& spins) noexcept {
    if (++spins < 64)
      pause_once();
    else
      std::this_thread::yield();
  }

  [[nodiscard]] static const V* value_if(const Item& item, std::uint64_t hash,
                                         const K& key) {
    if constexpr (kInline) {
      if constexpr (kKeepHash)
        if (item.hash != hash) return nullptr;
      return KeyEqual{}(item.key, key) ? &item.value : nullptr;
    } else {
      return item->hash == hash && KeyEqual{}(item->key, key) ? &item->value
                                                              : nullptr;
    }
  }
  [[nodiscard]] static const V& item_value(const Item& item) noexcept {
    if constexpr (kInline)
      return item.value;
    else
      return item->value;
  }
  [[nodiscard]] static std::uint64_t item_hash(const Item& item) noexcept {
    if constexpr (!kInline)
      return item->hash;
    else if constexpr (kKeepHash)
      return item.hash;
    else
      return mixed(item.key);
  }

  // True when a growth of `t` is under way, started here or before. Asked
  // often by inserts that fill a group: the answer is read first, and the
  // lock taken only to start one.
  bool startGrowth(Table* t, bool full = false) {
    const auto refused = [&] {
      return _table.load(std::memory_order_relaxed) != t ||
             (!full && t->fullGroups() * 4 <= t->groups());
    };
    if (t->next.load(std::memory_order_acquire) != nullptr) return true;
    if (refused()) return false;
    const std::scoped_lock lock(_writers);
    if (t->next.load(std::memory_order_relaxed) != nullptr) return true;
    if (refused()) return false;
    const std::size_t factor = t->groups() < kQuadrupleBelow ? 4 : 2;
    t->next.store(Table::make(t->generation, factor * t->groups()),
                  std::memory_order_release);
    return true;
  }

  // Copies blocks until none is left, then waits for the last one: an insert
  // that lands in a group before it is copied is copied with it, and one that
  // meets a seal comes back here.
  void help(Table* t) {
    Table* const bigger = t->next.load(std::memory_order_acquire);
    if (bigger == nullptr) return;
    const std::size_t blocks = t->blocks();
    for (std::size_t b = t->claimed.fetch_add(1, std::memory_order_relaxed);
         b < blocks; b = t->claimed.fetch_add(1, std::memory_order_relaxed)) {
      copyBlock(*t, *bigger, b);
      if (t->copied.fetch_add(1, std::memory_order_acq_rel) + 1 == blocks)
        publish(t, bigger);
    }
    for (unsigned spins = 0; _table.load(std::memory_order_acquire) == t;)
      relax(spins);
  }

  // Seals a group's free slots, so nothing more is claimed there, then
  // copies each claimed slot once its writer is done. Nobody reads `bigger`
  // before it is published, so a helper claims a slot there and writes it
  // after.
  static void copyBlock(Table& t, Table& bigger, std::size_t block) {
    const std::size_t end = std::min(t.groups(), (block + 1) * kBlock);
    for (std::size_t g = block * kBlock; g < end; ++g) {
      Group& group = t.group(g);
      std::uint64_t word = group.ctrl.load(std::memory_order_acquire);
      for (std::uint64_t free = free_of(word); free != 0;
           free = free_of(word)) {
        const std::uint64_t sealed = word | ((free >> 7) * kSealed);
        if (group.ctrl.compare_exchange_weak(word, sealed,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
          word = sealed;
          break;
        }
      }
      for (std::uint64_t m = word & kHighs; m != 0; m &= m - 1) {
        const std::size_t at = byte_of(m);
        group.waitReady(at);
        const auto tag = static_cast<std::uint8_t>(word >> (8 * at));
        place(bigger, *group.item(at), tag);
      }
    }
  }

  static void place(Table& bigger, const Item& item, std::uint8_t tag) {
    for (std::size_t g =
             static_cast<std::size_t>(item_hash(item)) & bigger.mask;
         ; g = (g + 1) & bigger.mask) {
      Group& group = bigger.group(g);
      std::uint64_t word = group.ctrl.load(std::memory_order_relaxed);
      for (std::uint64_t free = free_of(word); free != 0;
           free = free_of(word)) {
        const std::size_t at = byte_of(free);
        const std::uint64_t claimed = word | (std::uint64_t{tag} << (8 * at));
        if (!group.ctrl.compare_exchange_weak(word, claimed,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed))
          continue;
        std::construct_at(group.item(at), item);
        group.ready[at].store(1, std::memory_order_relaxed);
        if (free_of(claimed) == 0)
          bigger.countOf(g).fullGroups.fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
  }

  // The last block copied. A `clear` that came first took the old table out,
  // and the unpublished successor goes with it.
  void publish(Table* t, Table* bigger) {
    Table* expected = t;
    if (_table.compare_exchange_strong(expected, bigger,
                                       std::memory_order_seq_cst))
      TableEpochs::retire(t->retired, t, &destroy_table_erased, t->allocated());
  }

  static void destroy_table_erased(void* t) {
    Table::unmake(static_cast<Table*>(t));
  }
  static void destroy_with_entries_erased(void* t) {
    destroy_with_entries(static_cast<Table*>(t));
  }
  // A table that is current or was taken out by a `clear`: its successor, if
  // one was started, was never published, and goes too. The entries are the
  // successor's as well; they are freed once.
  static void destroy_with_entries(Table* t) {
    if (Table* pending = t->next.load(std::memory_order_relaxed))
      Table::unmake(pending);
    if constexpr (!kInline)
      for (std::size_t g = 0; g <= t->mask; ++g) {
        Group& group = t->group(g);
        const std::uint64_t word = group.ctrl.load(std::memory_order_relaxed);
        for (std::uint64_t m = word & kHighs; m != 0; m &= m - 1)
          delete *group.item(byte_of(m));
      }
    Table::unmake(t);
  }

  // Read by every operation: kept off the line the lock writes.
  alignas(64) std::atomic<Table*> _table;
  // Starting a growth, and clearing; a read never takes it, an insert only
  // to start a growth.
  alignas(64) std::mutex _writers;
};

}  // namespace cursiv::detail
