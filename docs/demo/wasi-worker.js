// One WASI "thread" of the language server. The page creates every worker up
// front: one runs `_start`, the others wait in a pool for `wasi.thread-spawn`.
// Minimal WASI: stdio and one read-only preopened folder of example documents.

const ESUCCESS = 0, EBADF = 8, EINVAL = 28, EISDIR = 31, ENOENT = 44, ENOSYS = 52, ENOTDIR = 54, ENOTSUP = 58,
      ESPIPE = 70, EROFS = 69;
const DIR = 3, FILE = 4;     // WASI file types
const PREOPEN = 3;           // the one preopened descriptor
const EXIT = Symbol('proc_exit');

// Control words (Int32Array over a SharedArrayBuffer):
//   0 next thread id, 1 exit code (-1 while running), 2.. pool slot states.
// Slot states: 0 idle, 1 claimed, 2 start, 3 busy.
const NEXT_TID = 0, EXIT_CODE = 1, SLOT0 = 2;

let ctx;  // { module, memory, ctl, spawnArgs, stdinHdr, stdinBuf, env, slot, root, files }

// The workspace: `ctx.files` maps absolute paths under `ctx.root` to bytes. A
// descriptor is known only to the worker that opened it, and each worker
// numbers its own, every `fdStep`.
const fds = new Map();
let nextFd = 0, fdStep = 1;
const dec = new TextDecoder();
function lookup(path) {
  if (ctx.files[path]) return { type: FILE, path, data: ctx.files[path] };
  const prefix = path.endsWith('/') ? path : path + '/';
  const names = new Map();
  for (const file of Object.keys(ctx.files)) {
    if (!file.startsWith(prefix)) continue;
    const rest = file.slice(prefix.length), slash = rest.indexOf('/');
    names.set(slash < 0 ? rest : rest.slice(0, slash), slash < 0 ? FILE : DIR);
  }
  return names.size || path === ctx.root ? { type: DIR, path, entries: [...names] } : null;
}
function resolve(dirfd, ptr, len) {
  const base = dirfd === PREOPEN ? ctx.root : fds.get(dirfd)?.path;
  if (base === undefined) return null;
  const parts = base.split('/');
  for (const part of dec.decode(u8().slice(ptr, ptr + len)).split('/')) {
    if (part === '' || part === '.') continue;
    if (part === '..') { if (parts.length > 2) parts.pop(); } else parts.push(part);
  }
  return parts.join('/');
}
function fileStat(out, node) {
  const view = dv();
  for (let i = 0; i < 64; i += 8) view.setBigUint64(out + i, 0n, true);
  view.setUint8(out + 16, node.type);
  view.setBigUint64(out + 24, 1n, true);
  view.setBigUint64(out + 32, BigInt(node.type === FILE ? node.data.length : 0), true);
}
function readFile(fd, iovs, n, nreadPtr) {
  const f = fds.get(fd);
  if (!f) return EBADF;
  if (f.type !== FILE) return EISDIR;
  let done = 0;
  const mem = u8();
  for (const [ptr, len] of iovecs(iovs, n)) {
    const chunk = f.data.subarray(f.pos, f.pos + len);
    mem.set(chunk, ptr);
    f.pos += chunk.length; done += chunk.length;
    if (chunk.length < len) break;
  }
  dv().setUint32(nreadPtr, done, true);
  return ESUCCESS;
}
function readDir(fd, buf, bufLen, cookie, usedPtr) {
  const d = fds.get(fd);
  if (!d || d.type !== DIR) return EBADF;
  const mem = u8();
  let used = 0;
  const entries = [['.', DIR], ['..', DIR], ...d.entries];
  // Each entry a header of 24 bytes then its name, cut off where the buffer ends.
  for (let i = Number(cookie); i < entries.length && used < bufLen; i++) {
    const [name, type] = entries[i];
    const bytes = enc.encode(name);
    const entry = new Uint8Array(24 + bytes.length), view = new DataView(entry.buffer);
    view.setBigUint64(0, BigInt(i + 1), true);   // the next entry's cookie
    view.setBigUint64(8, BigInt(i + 1), true);   // an inode
    view.setUint32(16, bytes.length, true);
    view.setUint8(20, type);
    entry.set(bytes, 24);
    const kept = Math.min(entry.length, bufLen - used);
    mem.set(entry.subarray(0, kept), buf + used);
    used += kept;
  }
  dv().setUint32(usedPtr, used, true);
  return ESUCCESS;
}

const u8 = () => new Uint8Array(ctx.memory.buffer);
const dv = () => new DataView(ctx.memory.buffer);
const enc = new TextEncoder();

function iovecs(iovs, n) {
  const view = dv(), out = [];
  for (let i = 0; i < n; i++)
    out.push([view.getUint32(iovs + 8 * i, true), view.getUint32(iovs + 8 * i + 4, true)]);
  return out;
}

function writeStrings(strings, ptrs, buf) {
  const view = dv(), mem = u8();
  for (const s of strings) {
    view.setUint32(ptrs, buf, true); ptrs += 4;
    const bytes = enc.encode(s + '\0');
    mem.set(bytes, buf); buf += bytes.length;
  }
  return ESUCCESS;
}
function sizes(strings, countPtr, sizePtr) {
  const view = dv();
  view.setUint32(countPtr, strings.length, true);
  view.setUint32(sizePtr, strings.reduce((n, s) => n + enc.encode(s).length + 1, 0), true);
  return ESUCCESS;
}

// stdin: a single-consumer ring buffer. Header words: 0 head (bytes written),
// 1 tail (bytes read), 2 closed. Counters wrap at 2^32, and the capacity is a
// power of two, so a counter's low bits are its place in the ring.
function readStdin(iovs, n, nreadPtr) {
  const hdr = ctx.stdinHdr, ring = ctx.stdinBuf, mask = ring.length - 1;
  for (;;) {
    const head = Atomics.load(hdr, 0), tail = Atomics.load(hdr, 1);
    const avail = (head - tail) >>> 0;
    if (avail === 0) {
      if (Atomics.load(hdr, 2)) { dv().setUint32(nreadPtr, 0, true); return ESUCCESS; }
      Atomics.wait(hdr, 0, head);
      continue;
    }
    let done = 0;
    const mem = u8();
    for (const [ptr, len] of iovecs(iovs, n)) {
      const want = Math.min(len, avail - done);
      // Up to the ring's end, then on from its start.
      const from = (tail + done) & mask, first = Math.min(want, ring.length - from);
      mem.set(ring.subarray(from, from + first), ptr);
      mem.set(ring.subarray(0, want - first), ptr + first);
      done += want;
      if (done === avail) break;
    }
    Atomics.store(hdr, 1, (tail + done) | 0);
    Atomics.notify(hdr, 1);
    dv().setUint32(nreadPtr, done, true);
    return ESUCCESS;
  }
}

function writeOut(fd, iovs, n, nwrittenPtr) {
  const parts = iovecs(iovs, n);
  const total = parts.reduce((s, [, len]) => s + len, 0);
  const bytes = new Uint8Array(total), mem = u8();
  let at = 0;
  for (const [ptr, len] of parts) { bytes.set(mem.subarray(ptr, ptr + len), at); at += len; }
  postMessage({ type: 'out', fd, bytes }, [bytes.buffer]);
  dv().setUint32(nwrittenPtr, total, true);
  return ESUCCESS;
}

// poll_oneoff: clock subscriptions only (sleeps, timed waits). Any other
// subscription, or a clock with no such id, is due at once with its error.
// It waits for the earliest due, then reports what is due by then.
function pollOneoff(inPtr, outPtr, nsubs, neventsPtr) {
  const view = dv();
  const subs = [];   // per subscription, its error and the time left in ns
  let wait = Infinity;
  for (let i = 0; i < nsubs; i++) {
    const s = inPtr + 48 * i, id = view.getUint32(s + 16, true);
    const error = view.getUint8(s + 8) === 0 ? clockError(id) : ENOSYS;
    let left = 0;
    if (error === ESUCCESS) {
      left = Number(view.getBigUint64(s + 24, true));
      if (view.getUint16(s + 40, true) & 1) left = Math.max(0, left - nowNs(id));
    }
    subs.push({ error, left });
    wait = Math.min(wait, left);
  }
  const start = performance.now();
  if (wait > 0 && wait !== Infinity) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, wait / 1e6);
  const waited = Math.max(wait, (performance.now() - start) * 1e6);
  let events = 0;
  subs.forEach(({ error, left }, i) => {
    if (left > waited) return;                  // a clock not due yet
    const s = inPtr + 48 * i, e = outPtr + 32 * events;
    view.setBigUint64(e, view.getBigUint64(s, true), true);
    view.setUint16(e + 8, error, true);
    view.setUint8(e + 10, view.getUint8(s + 8));
    events++;
  });
  view.setUint32(neventsPtr, events, true);
  return ESUCCESS;
}

// The wall clock (0) and the monotonic one (1). No CPU clock (2, 3) is at
// hand, and other ids name no clock.
const clockError = (id) => id <= 1 ? ESUCCESS : id <= 3 ? ENOTSUP : EINVAL;
// One clock for every worker: timeOrigin differs per worker.
const nowNs = (id) => id === 0 ? Date.now() * 1e6
                                : Math.round((performance.timeOrigin + performance.now()) * 1e6);

// WASI passes addresses, sizes and descriptors as unsigned 32-bit numbers,
// which reach JavaScript signed.
const unsigned = (imports) => Object.fromEntries(Object.entries(imports).map(([name, call]) =>
  [name, (...args) => call(...args.map((arg) => typeof arg === 'number' ? arg >>> 0 : arg))]));

function wasiImports() {
  const args = ['language-server'];
  const env = Object.entries(ctx.env).map(([k, v]) => `${k}=${v}`);
  return unsigned({
    args_get: (p, b) => writeStrings(args, p, b),
    args_sizes_get: (c, s) => sizes(args, c, s),
    environ_get: (p, b) => writeStrings(env, p, b),
    environ_sizes_get: (c, s) => sizes(env, c, s),
    clock_time_get: (id, _precision, out) => {
      const error = clockError(id);
      if (error === ESUCCESS) dv().setBigUint64(out, BigInt(nowNs(id)), true);
      return error;
    },
    fd_close: (fd) => { fds.delete(fd); return ESUCCESS; },
    fd_fdstat_get: (fd, out) => {
      const node = fd <= 2 ? { type: 2 } : fd === PREOPEN ? { type: DIR } : fds.get(fd);
      if (!node) return EBADF;
      const view = dv();
      view.setUint8(out, node.type);                // 2 = character device
      view.setUint16(out + 2, 0, true);
      view.setBigUint64(out + 8, 0xffffffffffffffffn, true);
      view.setBigUint64(out + 16, 0xffffffffffffffffn, true);
      return ESUCCESS;
    },
    fd_fdstat_set_flags: () => ESUCCESS,
    fd_prestat_get: (fd, out) => {
      if (fd !== PREOPEN || !ctx.root) return EBADF;
      dv().setUint8(out, 0);                        // a directory
      dv().setUint32(out + 4, new TextEncoder().encode(ctx.root).length, true);
      return ESUCCESS;
    },
    fd_prestat_dir_name: (fd, ptr, len) => {
      if (fd !== PREOPEN || !ctx.root) return EBADF;
      u8().set(new TextEncoder().encode(ctx.root).subarray(0, len), ptr);
      return ESUCCESS;
    },
    fd_read: (fd, iovs, n, nread) => fd === 0 ? readStdin(iovs, n, nread) : readFile(fd, iovs, n, nread),
    fd_readdir: readDir,
    fd_seek: (fd, offset, whence, out) => {
      if (fd <= 2) return ESPIPE;
      const f = fds.get(fd);
      if (!f) return EBADF;
      if (f.type !== FILE) return EISDIR;
      const at = [0, f.pos, f.data.length][whence] + Number(offset);
      if (at < 0) return EINVAL;
      f.pos = at;
      dv().setBigUint64(out, BigInt(at), true);
      return ESUCCESS;
    },
    fd_write: (fd, iovs, n, nwritten) => (fd === 1 || fd === 2) ? writeOut(fd, iovs, n, nwritten) : EBADF,
    path_filestat_get: (dirfd, _flags, ptr, len, out) => {
      const path = resolve(dirfd, ptr, len);
      const node = path && lookup(path);
      if (!node) return path ? ENOENT : EBADF;
      fileStat(out, node);
      return ESUCCESS;
    },
    path_open: (dirfd, _dirflags, ptr, len, oflags, _rb, _ri, _fdflags, fdOut) => {
      const path = resolve(dirfd, ptr, len);
      if (!path) return EBADF;
      if (oflags & (1 | 4 | 8)) return EROFS;      // create, exclusive, truncate
      const node = lookup(path);
      if (!node) return ENOENT;
      if ((oflags & 2) && node.type !== DIR) return ENOTDIR;
      const fd = nextFd;
      nextFd += fdStep;
      fds.set(fd, { ...node, pos: 0 });
      dv().setUint32(fdOut, fd, true);
      return ESUCCESS;
    },
    poll_oneoff: pollOneoff,
    proc_exit: (code) => {
      Atomics.compareExchange(ctx.ctl, EXIT_CODE, -1, code & 0xff);
      postMessage({ type: 'exit', code: code & 0xff });
      throw EXIT;
    },
    // The allocator's entropy. getRandomValues refuses shared memory and takes
    // 64 KiB at most.
    random_get: (ptr, len) => {
      const mem = u8();
      for (let at = 0; at < len; at += 65536) {
        const chunk = crypto.getRandomValues(new Uint8Array(Math.min(65536, len - at)));
        mem.set(chunk, ptr + at);
      }
      return ESUCCESS;
    },
    sched_yield: () => ESUCCESS,
  });
}

// Synchronous: claims an idle pool worker, or fails with -EAGAIN.
function threadSpawn(startArg) {
  const ctl = ctx.ctl, slots = ctl.length - SLOT0;
  for (let k = 0; k < slots; k++) {
    if (Atomics.compareExchange(ctl, SLOT0 + k, 0, 1) !== 0) continue;
    const tid = Atomics.add(ctl, NEXT_TID, 1);
    ctx.spawnArgs[2 * k] = tid;
    ctx.spawnArgs[2 * k + 1] = startArg;
    Atomics.store(ctl, SLOT0 + k, 2);
    Atomics.notify(ctl, SLOT0 + k);
    return tid;
  }
  postMessage({ type: 'log', text: 'thread-spawn: pool exhausted' });
  return -6;  // -EAGAIN
}

// The shared memory goes where the module imports it from.
function instantiate() {
  const imports = { wasi_snapshot_preview1: wasiImports(), wasi: { 'thread-spawn': threadSpawn } };
  for (const entry of WebAssembly.Module.imports(ctx.module))
    if (entry.kind === 'memory') imports[entry.module] = { ...imports[entry.module], [entry.name]: ctx.memory };
  return WebAssembly.instantiate(ctx.module, imports);
}

function report(error) {
  if (error === EXIT) return;
  // The message first: Firefox leaves it out of the stack.
  postMessage({ type: 'crash', text: `${error}\n${error?.stack ?? ''}` });
}

async function run(data) {
  ctx = data;
  // The main worker and each pool worker: one number in every `fdStep`.
  fdStep = ctx.ctl.length - SLOT0 + 1;
  nextFd = 16 + (data.role === 'main' ? 0 : data.slot + 1);
  if (data.role === 'main') {
    const instance = await instantiate();
    try { instance.exports._start(); postMessage({ type: 'exit', code: 0 }); }
    catch (error) { report(error); }
    return;
  }
  // Pool worker: instantiate ahead of time, then serve one thread per instance.
  const slot = SLOT0 + data.slot;
  let instance = await instantiate();
  postMessage({ type: 'ready' });
  for (;;) {
    Atomics.store(ctx.ctl, slot, 0);
    // Waits on the state it read: a spawn published since is not missed.
    for (let state; (state = Atomics.load(ctx.ctl, slot)) !== 2;) Atomics.wait(ctx.ctl, slot, state);
    Atomics.store(ctx.ctl, slot, 3);
    const tid = ctx.spawnArgs[2 * data.slot], arg = ctx.spawnArgs[2 * data.slot + 1];
    try { instance.exports.wasi_thread_start(tid, arg); }
    catch (error) { report(error); if (error === EXIT) return; }
    instance = await instantiate();
  }
}

// A rejection here would reach nobody: the page would wait for the worker forever.
onmessage = ({ data }) => { run(data).catch(report); };
