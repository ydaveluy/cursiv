#include "cursiv/core/execution/WorkerPool.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cursiv/core/utils/FunctionRef.hpp"

namespace {

// How long a publisher waits on the last parts before handing the core back.
constexpr std::size_t kCorunSpins = 256;

void pause_once() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

std::uint64_t next_random(std::uint64_t& state) noexcept {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 0x2545f4914f6cdd1dULL;
}

// Fewer buckets than workers: each is one more place every thief has to look,
// and a submitter that is not a worker is rare.
std::size_t bucket_count(std::size_t workers) noexcept {
  return std::max<std::size_t>(1, std::bit_floor(workers / 2));
}

}  // namespace

namespace cursiv {

WorkerPool::WorkerPool(std::size_t workers) : _idle(workers) {
  _workers.reserve(workers);
  for (std::size_t i = 0; i < workers; ++i)
    _workers.push_back(std::make_unique<Worker>());
  for (std::size_t i = 0; i < bucket_count(workers); ++i)
    _buckets.push_back(std::make_unique<Bucket>());

  // Started only once every queue exists: a worker steals from all of them from
  // its first instruction.
  //
  // A thread that fails to start must not leave the others running: a joinable
  // `std::thread` destroyed with the half-built pool terminates the process,
  // and the constructor's throw means no destructor will run for it.
  try {
    for (std::size_t i = 0; i < workers; ++i) {
      _workers[i]->victim = i;
      _workers[i]->random = i + 1;
      _workers[i]->thread = std::thread([this, i] { workerLoop(i); });
    }
  } catch (...) {
    stopAndJoin();
    throw;
  }
}

// Wakes every parked worker before joining: one blocked on the notifier would
// never see the flag on its own.
void WorkerPool::stopAndJoin() noexcept {
  _stopping.store(true, std::memory_order_release);
  _idle.notifyAll();
  for (auto& worker : _workers)
    if (worker->thread.joinable()) worker->thread.join();
}

WorkerPool::~WorkerPool() noexcept { stopAndJoin(); }

bool WorkerPool::onWorkerThread() const noexcept {
  return selfIndex() != _workers.size();
}

// `_workers` is written once, by the constructor, so this reads it unlocked.
// A scan is enough: its cost lands once per run, not once per item.
std::size_t WorkerPool::selfIndex() const noexcept {
  const auto self = std::this_thread::get_id();
  for (std::size_t i = 0; i < _workers.size(); ++i)
    if (_workers[i]->thread.get_id() == self) return i;
  return _workers.size();
}

// Coarse slices claimed by compare-exchange while much of the range is left,
// then a tight `fetch_add` of one at the tail. The two regimes are the point
// -- a compare-exchange per item would cost the tail, a fixed slice would
// strand whoever drew the last big cut.
void WorkerPool::drain(Job& job) noexcept {
  const std::size_t fineBelow = 4 * job.parties;
  std::size_t first = job.next.load(std::memory_order_relaxed);

  while (first < job.count) {
    const std::size_t left = job.count - first;
    if (left < fineBelow) {
      for (;;) {
        first = job.next.fetch_add(1, std::memory_order_relaxed);
        if (first >= job.count) return;
        job.body(first);
      }
    }
    const std::size_t slice =
        std::max<std::size_t>(1, left / (2 * job.parties));
    const std::size_t last = std::min(first + slice, job.count);
    if (job.next.compare_exchange_strong(first, last, std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
      for (std::size_t index = first; index < last; ++index) job.body(index);
      first = job.next.load(std::memory_order_relaxed);
    }
  }
}

// The cell is read before the work, and that ordering is the whole safety: the
// publisher's stack dies the moment the count reaches zero, so the job must not
// be touched after the decrement -- while the cell, owned by the pool, may.
void WorkerPool::runTask(Task& task) noexcept {
  Job& job = *task.job;
  Cell& cell = *job.cell;
  drain(job);
  if (cell.pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
    cell.pending.notify_all();
}

WorkerPool::Task* WorkerPool::steal(std::size_t victim) noexcept {
  return victim < _workers.size()
             ? _workers[victim]->queue.steal()
             : _buckets[victim - _workers.size()]->queue.steal();
}

bool WorkerPool::queueEmpty(std::size_t victim) const noexcept {
  return victim < _workers.size()
             ? _workers[victim]->queue.empty()
             : _buckets[victim - _workers.size()]->queue.empty();
}

// Steals from the victim that last paid, then at random. Gives up after enough
// consecutive misses that the queues are worth calling empty; returns false
// only when the pool is closing.
bool WorkerPool::explore(Worker& worker, Task*& task) noexcept {
  const std::size_t victims = victimCount();
  const std::size_t maxSteals = (victims + 1) << 1;
  std::size_t misses = 0;
  std::size_t victim = worker.victim;

  for (;;) {
    task = steal(victim);
    if (task != nullptr) {
      worker.victim = victim;
      return true;
    }
    if (++misses > maxSteals) {
      std::this_thread::yield();
      if (misses > 100 + maxSteals) return true;  // empty-handed, still alive
    }
    if (_stopping.load(std::memory_order_relaxed)) return false;
    victim = static_cast<std::size_t>(
        ((next_random(worker.random) >> 32) * victims) >> 32);
  }
}

// The two-phase park: announce, look at every queue once more, and sleep only
// if all were empty. A task pushed between the last search and the sleep moves
// the epoch, so the sleep returns instead of missing it.
bool WorkerPool::waitForTask(Worker& worker, std::size_t index,
                             Task*& task) noexcept {
  for (;;) {
    if (!explore(worker, task)) return false;
    if (task != nullptr) return true;

    _idle.prepareWait(_idle.waiter(index));

    bool retry = false;
    for (std::size_t victim = 0; victim < victimCount(); ++victim)
      if (!queueEmpty(victim)) {
        worker.victim = victim;
        retry = true;
        break;
      }
    if (retry) {
      _idle.cancelWait();
      continue;
    }
    if (_stopping.load(std::memory_order_relaxed)) {
      _idle.cancelWait();
      return false;
    }
    _idle.commitWait(_idle.waiter(index));
  }
}

void WorkerPool::workerLoop(std::size_t index) noexcept {
  Worker& worker = *_workers[index];
  Task* task = nullptr;
  for (;;) {
    // Drain the local queue, then steal, then park.
    while (task != nullptr) {
      runTask(*task);
      task = worker.queue.pop();
    }
    if (!waitForTask(worker, index, task)) return;
  }
}

void WorkerPool::submit(Task& task, std::size_t self, std::size_t parts) {
  std::size_t published = 0;
  try {
    if (self < _workers.size()) {
      for (; published < parts; ++published)
        _workers[self]->queue.push(&task);
      return;
    }
    for (std::size_t at = 0; at < _buckets.size() && at < parts; ++at) {
      // Rank `at`, `at + buckets`, `at + 2*buckets`... all land here.
      const std::size_t share =
          (parts - at + _buckets.size() - 1) / _buckets.size();
      Bucket& bucket = *_buckets[at];
      const std::lock_guard lock(bucket.mutex);
      const auto end = published + share;
      for (; published < end; ++published) bucket.queue.push(&task);
    }
  } catch (...) {
    // Only published parts can finish. Their task still points into run's
    // stack, so the caller must join them before propagating this failure.
    task.job->cell->pending.fetch_sub(parts - published,
                                      std::memory_order_relaxed);
    throw;
  }
}

// The publisher's own scheduling loop: it takes tasks -- its own first, then
// anyone's -- until its run is done. It never blocks, which is what lets a body
// call `run` again without a worker waiting on a worker.
void WorkerPool::corun(Job& job, std::size_t self) noexcept {
  const std::size_t victims = victimCount();
  const bool isWorker = self < _workers.size();
  Cell& cell = *job.cell;
  std::size_t misses = 0;
  while (cell.pending.load(std::memory_order_acquire) != 0) {
    Task* task = isWorker ? _workers[self]->queue.pop() : nullptr;
    // Started at a rotating offset: sweeping from zero would put every
    // publisher on the first worker's queue first.
    for (std::size_t at = 0; task == nullptr && at < victims; ++at)
      task = steal((self + at) % victims);
    if (task != nullptr) {
      runTask(*task);
      misses = 0;
      continue;
    }
    // Nothing left to steal. A short spin first, since what remains is
    // usually one slice; then a non-worker sleeps -- spinning would cost a
    // core the running parts need -- while a worker keeps yielding, since it
    // may not hold a thread those parts need.
    if (++misses < kCorunSpins) {
      pause_once();
      continue;
    }
    if (isWorker) {
      std::this_thread::yield();
      continue;
    }
    const std::size_t left = cell.pending.load(std::memory_order_acquire);
    if (left != 0) cell.pending.wait(left, std::memory_order_acquire);
  }
}

void WorkerPool::run(std::size_t count,
                     cursiv::function_ref<void(std::size_t)> body) {
  if (count == 0) return;

  // No more parts than items, and never more than the pool can run.
  const std::size_t parts = std::min(count, _workers.size() + 1);
  if (parts <= 1) {
    for (std::size_t index = 0; index < count; ++index) body(index);
    return;
  }

  Cell* cell = claimCell();
  // Every cell in flight: run it alone rather than wait for one. Never wrong.
  if (cell == nullptr) {
    for (std::size_t index = 0; index < count; ++index) body(index);
    return;
  }
  cell->pending.store(parts, std::memory_order_relaxed);

  const std::size_t self = selfIndex();
  Job job{.body = body, .count = count, .parties = parts, .cell = cell};
  Task task{&job};
  try {
    submit(task, self, parts - 1);
  } catch (...) {
    _idle.notifyN(parts - 1);
    drain(job);
    if (cell->pending.fetch_sub(1, std::memory_order_acq_rel) != 1)
      corun(job, self);
    cell->taken.store(false, std::memory_order_release);
    throw;
  }
  _idle.notifyN(parts - 1);

  // The publisher runs the last part itself, then co-runs until every part
  // has reported.
  drain(job);
  if (cell->pending.fetch_sub(1, std::memory_order_acq_rel) != 1)
    corun(job, self);
  cell->taken.store(false, std::memory_order_release);
}

// A cell is free the moment its run is over, so the scan almost always stops
// at the first one.
WorkerPool::Cell* WorkerPool::claimCell() noexcept {
  for (Cell& cell : _cells) {
    bool free = false;
    if (cell.taken.compare_exchange_strong(
            free, true, std::memory_order_acquire, std::memory_order_relaxed))
      return &cell;
  }
  return nullptr;
}

}  // namespace cursiv
