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

template <class V>
class ConcurrentValue;

// Epoch-based reclamation shared by every table. A reader publishes its
// epoch in a slot of its own thread; what a writer releases is freed once every
// reader has moved to a later epoch, or left.
class TableEpochs {
  template <class, class, class, class>
  friend class ConcurrentTable;
  template <class>
  friend class ConcurrentValue;

  static constexpr std::uint64_t kIdle = ~std::uint64_t{0};

  // What a release leaves for the collection, kept in what it releases:
  // releasing asks for no memory, so what is out of reach is never lost.
  struct PendingFree {
    PendingFree* next = nullptr;
    void* object = nullptr;
    void (*free)(void*) = nullptr;
    std::uint64_t epoch = 0;
  };

  // A reader's stay. Only the outermost guard of a thread publishes its epoch:
  // a nested read costs nothing more.
  class Guard {
   public:
    Guard() noexcept {
      Reader& s = reader;
      if (s.depth++ != 0) return;
      if (s.slot == nullptr) [[unlikely]] {
        s.slot = takeSlot();
        if (s.slot == nullptr) {
          enterWithoutSlot();
          return;
        }
      }
      // Acquire: a reader that sees a `freeLater`'s epoch sees what it took out
      // of reach. Where a writer can fence every thread, the epoch is stored
      // plainly; elsewhere the store fences itself.
      const std::uint64_t epoch = current.load(std::memory_order_acquire);
      if (asymmetric) [[likely]] {
        s.slot->store(epoch, std::memory_order_relaxed);
        std::atomic_signal_fence(std::memory_order_seq_cst);
      } else {
        s.slot->exchange(epoch, std::memory_order_seq_cst);
      }
    }
    ~Guard() {
      Reader& s = reader;
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

  // `object` is out of every new reader's reach and is freed once the readers
  // that may hold it have left; `record` lives in it. A release collects once
  // enough releases, `bytes` or time have gone by since the last collection.
  static void freeLater(PendingFree& record, void* object, void (*free)(void*),
                        std::size_t bytes) noexcept;

  // Trivial and `constinit`, so a read needs no initialisation check. Defined
  // in the source file: Apple's linker refuses an inline thread-local.
  struct Reader {
    std::atomic<std::uint64_t>* slot = nullptr;
    std::uint32_t depth = 0;
    bool exiting = false;
  };
  static constinit thread_local Reader reader;
  static std::atomic<std::uint64_t> current;
  // Set once, before any reader, when the process can fence all its threads.
  static const bool asymmetric;

  // Gives the slot back at thread exit; defined where it is taken.
  struct Release;
  static thread_local Release release;

  // The slots and the released list; defined in the source file.
  struct State;
  static State& state() noexcept;

  // Null when every slot is held.
  static std::atomic<std::uint64_t>* takeSlot() noexcept;
  static void giveBack(Reader& s) noexcept;
  static void enterWithoutSlot() noexcept;
  static void leaveWithoutSlot() noexcept;
};

inline std::atomic<std::uint64_t> TableEpochs::current{0};

// A hash table read without a lock or a wait: a key's first stored value
// wins and stays until `clear`. An insert claims a slot with a compare-exchange
// and may wait for another writer's few stores. Calls hand out copies.
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

  // A control byte: free, blocked by a growth, or claimed with seven bits of
  // the hash. Bit seven marks a claimed slot, bit five a blocked one; a claimed
  // slot is readable once its ready byte is set.
  static constexpr std::uint8_t kEmpty = 0x00;
  static constexpr std::uint8_t kBlocked = 0x20;
  static constexpr std::uint8_t kFull = 0x80;
  static_assert((kEmpty & 0xa0) == 0 && (kBlocked & 0xa0) == 0x20 &&
                (kFull & 0x80) != 0);
  static constexpr std::uint64_t kLows = 0x0101010101010101ull;
  static constexpr std::uint64_t kHighs = 0x8080808080808080ull;

  static constexpr std::size_t kGroup = 8;
  static constexpr std::size_t kMinGroups = 2;
  // Groups a helper copies at a time.
  static constexpr std::size_t kBlock = 32;
  // Full groups are counted on at most `kCounts` counters, each for an even
  // share of the groups. A quarter of the groups full (about seven tenths of
  // the slots), or a probe longer than `kMaxProbes`, asks for a growth.
  static constexpr std::size_t kCounts = 32;
  static constexpr std::size_t kMaxProbes = 16;
  // Below this many groups a growth quadruples: a table filled from nothing
  // copies a third of its entries instead of all of them, for at most four
  // times the room it needs; past it, doubling bounds that room.
  static constexpr std::size_t kQuadrupleBelow = 4096;
  // The generation of the closed table, which no cache reaches by counting.
  static constexpr std::uint64_t kClosed = ~std::uint64_t{0};

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
    TableEpochs::PendingFree pendingFree;

    [[nodiscard]] static Table* make(std::uint64_t g, std::size_t groups) {
      return build(::operator new(bytes(groups), std::align_val_t{64}), g,
                   groups);
    }
    // The table a clear with no memory leaves: one empty group, and a
    // generation no insert matches. Shared by every cache of this type.
    [[nodiscard]] static Table& closed() noexcept {
      alignas(64) static unsigned char room[bytes(1)];
      static Table* const table = build(room, kClosed, 1);
      return *table;
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
      return bytes(groups());
    }

   private:
    Table(std::uint64_t g, std::size_t m, std::size_t c)
        : generation(g), mask(m), countMask(c) {}
    [[nodiscard]] static constexpr std::size_t counts_for(
        std::size_t groups) noexcept {
      return std::min((groups + kBlock - 1) / kBlock, kCounts);
    }
    [[nodiscard]] static constexpr std::size_t bytes(
        std::size_t groups) noexcept {
      return groupsOffset(counts_for(groups)) + groups * sizeof(Group);
    }
    [[nodiscard]] static Table* build(void* raw, std::uint64_t g,
                                      std::size_t groups) noexcept {
      const std::size_t counts = counts_for(groups);
      auto* const t = new (raw) Table(g, groups - 1, counts - 1);
      for (std::size_t c = 0; c < counts; ++c) new (&t->counts()[c]) Count;
      for (std::size_t at = 0; at < groups; ++at) new (&t->group(at)) Group;
      return t;
    }
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
  ~ConcurrentTable() {
    if (Table* const t = _table.load(); t != &Table::closed())
      destroy_with_entries(t);
  }
  ConcurrentTable(const ConcurrentTable&) = delete;
  ConcurrentTable& operator=(const ConcurrentTable&) = delete;

  // Moved by every `clear`: an insert made for an older generation stores
  // nothing.
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return _generation.load(std::memory_order_seq_cst);
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

  // What `key` holds, or else what `provider` answers, unless a value got
  // there first. The provider runs with no guard or lock; after a `clear`
  // meanwhile, its answer is returned but not stored.
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

  // Takes every entry out of new readers' reach, freed once old readers
  // leave. `keepCapacity` keeps the size for a refill. With no memory for a
  // table, the cache is closed: it keeps nothing until a later clear.
  void clear(bool keepCapacity = false) noexcept {
    // Guarded too: a growth may release the current table while it is read.
    const TableEpochs::Guard guard;
    Table* old = nullptr;
    {
      const std::scoped_lock lock(_writers);
      // Sequentially consistent, as a read's: a growth may release this table
      // meanwhile, and the guard keeps it only for loads in that order.
      const Table* current = _table.load(std::memory_order_seq_cst);
      const std::uint64_t next =
          _generation.load(std::memory_order_relaxed) + 1;
      const std::size_t groups =
          keepCapacity ? std::max(current->groups(), kMinGroups) : kMinGroups;
      Table* fresh = &Table::closed();
      try {
        fresh = Table::make(next, groups);
      } catch (const std::bad_alloc&) {
        // The cache is closed.
      }
      _generation.store(next, std::memory_order_seq_cst);
      // An exchange: a growth may publish a successor meanwhile, and the table
      // taken out is the one whose entries go.
      old = _table.exchange(fresh, std::memory_order_seq_cst);
    }
    // Released with no lock held: a value the collection frees may use this
    // table.
    if (old != &Table::closed())
      TableEpochs::freeLater(old->pendingFree, old,
                             &destroy_with_entries_erased, old->allocated());
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
          if (has_blocked(word)) {
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

  // Mixed first: a standard hash of an integer is often the value itself.
  // Always 64 bits, even where `std::size_t` is 32.
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
  [[nodiscard]] static bool has_blocked(std::uint64_t word) noexcept {
    return (~word & (word << 2) & kHighs) != 0;
  }
  // A probe ends at a group with a slot never claimed, free or blocked.
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

  // Copies blocks until none is left, then waits for the last one. Takes no
  // block once a clear has taken `t` out.
  void help(Table* t) {
    Table* const bigger = t->next.load(std::memory_order_acquire);
    if (bigger == nullptr) return;
    const std::size_t blocks = t->blocks();
    while (_table.load(std::memory_order_acquire) == t) {
      const std::size_t b = t->claimed.fetch_add(1, std::memory_order_relaxed);
      if (b >= blocks) break;
      copyBlock(*t, *bigger, b);
      if (t->copied.fetch_add(1, std::memory_order_acq_rel) + 1 == blocks)
        publish(t, bigger);
    }
    for (unsigned spins = 0; _table.load(std::memory_order_acquire) == t;)
      relax(spins);
  }

  // Seals a group's free slots, then copies each claimed slot once its writer
  // is done. No one reads `bigger` before it is published.
  static void copyBlock(Table& t, Table& bigger, std::size_t block) {
    const std::size_t end = std::min(t.groups(), (block + 1) * kBlock);
    for (std::size_t g = block * kBlock; g < end; ++g) {
      Group& group = t.group(g);
      std::uint64_t word = group.ctrl.load(std::memory_order_acquire);
      for (std::uint64_t free = free_of(word); free != 0;
           free = free_of(word)) {
        const std::uint64_t blocked = word | ((free >> 7) * kBlocked);
        if (group.ctrl.compare_exchange_weak(word, blocked,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
          word = blocked;
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
      TableEpochs::freeLater(t->pendingFree, t, &destroy_table_erased,
                             t->allocated());
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
  std::atomic<std::uint64_t> _generation{0};
  // Starting a growth, and clearing; a read never takes it, an insert only
  // to start a growth.
  alignas(64) std::mutex _writers;
};

}  // namespace cursiv::detail
