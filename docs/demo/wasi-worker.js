// One WASI "thread" of the language server. The page creates every worker up
// front: one runs `_start` (the LSP read loop), the others wait in a pool for
// `wasi.thread-spawn`. A worker blocked in wasm cannot start a new worker, so
// spawning only hands a start argument to an idle pool worker.
//
// Minimal wasi_snapshot_preview1: stdio, plus one read-only preopened folder
// holding the example documents, which the server scans like a workspace.

const ESUCCESS = 0, EBADF = 8, EINVAL = 28, EISDIR = 31, ENOENT = 44, ENOSYS = 52, ENOTDIR = 54, ESPIPE = 70, EROFS = 69;
const DIR = 3, FILE = 4;     // WASI file types
const PREOPEN = 3;           // the one preopened descriptor
const EXIT = Symbol('proc_exit');

// Control words (Int32Array over a SharedArrayBuffer):
//   0 next thread id, 1 exit code (-1 while running), 2.. pool slot states.
// Slot states: 0 idle, 1 claimed, 2 start, 3 busy.
const NEXT_TID = 0, EXIT_CODE = 1, SLOT0 = 2;

let ctx;  // { module, memory, ctl, spawnArgs, stdinHdr, stdinBuf, env, slot, root, files }

// The workspace: `ctx.files` maps absolute paths under `ctx.root` to bytes.
// Descriptors are per worker, from a range of their own.
const fds = new Map();
let nextFd = 0;
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
  const view = dv(), mem = u8(), enc8 = new TextEncoder();
  let used = 0;
  const entries = [['.', DIR], ['..', DIR], ...d.entries];
  for (let i = Number(cookie); i < entries.length; i++) {
    const [name, type] = entries[i];
    const bytes = enc8.encode(name);
    const header = new Uint8Array(24), hv = new DataView(header.buffer);
    hv.setBigUint64(0, BigInt(i + 1), true);
    hv.setBigUint64(8, BigInt(i + 1), true);
    hv.setUint32(16, bytes.length, true);
    hv.setUint8(20, type);
    const entry = new Uint8Array(24 + bytes.length);
    entry.set(header); entry.set(bytes, 24);
    const room = bufLen - used;
    mem.set(entry.subarray(0, Math.min(room, entry.length)), buf + used);
    used += Math.min(room, entry.length);
    if (used >= bufLen) break;
  }
  view.setUint32(usedPtr, used, true);
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
// 1 tail (bytes read), 2 closed. Counters wrap at 2^32.
function readStdin(iovs, n, nreadPtr) {
  const hdr = ctx.stdinHdr, cap = ctx.stdinBuf.length;
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
      let want = Math.min(len, avail - done);
      for (let k = 0; k < want; k++) mem[ptr + k] = ctx.stdinBuf[(tail + done + k) % cap];
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

// poll_oneoff: clock subscriptions only (sleeps, timed waits).
function pollOneoff(inPtr, outPtr, nsubs, neventsPtr) {
  const view = dv();
  let timeoutNs = Infinity;
  for (let i = 0; i < nsubs; i++) {
    const s = inPtr + 48 * i;
    if (view.getUint8(s + 8) === 0) {
      let t = Number(view.getBigUint64(s + 24, true));
      if (view.getUint16(s + 40, true) & 1) t = Math.max(0, t - nowNs(view.getUint32(s + 16, true)));
      timeoutNs = Math.min(timeoutNs, t);
    }
  }
  if (timeoutNs !== Infinity) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, timeoutNs / 1e6);
  let events = 0;
  for (let i = 0; i < nsubs; i++) {
    const s = inPtr + 48 * i, e = outPtr + 32 * events;
    view.setBigUint64(e, view.getBigUint64(s, true), true);
    view.setUint16(e + 8, view.getUint8(s + 8) === 0 ? ESUCCESS : ENOSYS, true);
    view.setUint8(e + 10, view.getUint8(s + 8));
    events++;
  }
  view.setUint32(neventsPtr, events, true);
  return ESUCCESS;
}

// One clock for every worker: timeOrigin differs per worker.
const nowNs = (id) => id === 0 ? Date.now() * 1e6
                                : Math.round((performance.timeOrigin + performance.now()) * 1e6);

function wasiImports() {
  const args = ['language-server'];
  const env = Object.entries(ctx.env).map(([k, v]) => `${k}=${v}`);
  return {
    args_get: (p, b) => writeStrings(args, p, b),
    args_sizes_get: (c, s) => sizes(args, c, s),
    environ_get: (p, b) => writeStrings(env, p, b),
    environ_sizes_get: (c, s) => sizes(env, c, s),
    clock_time_get: (id, _precision, out) => { dv().setBigUint64(out, BigInt(nowNs(id)), true); return ESUCCESS; },
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
      const fd = nextFd++;
      fds.set(fd, { ...node, pos: 0 });
      dv().setUint32(fdOut, fd, true);
      return ESUCCESS;
    },
    poll_oneoff: pollOneoff,
    proc_exit: (code) => {
      Atomics.compareExchange(ctx.ctl, EXIT_CODE, -1, code & 0xff);
      postMessage({ type: 'exit', code });
      throw EXIT;
    },
    sched_yield: () => ESUCCESS,
  };
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

const instantiate = () => WebAssembly.instantiate(ctx.module, {
  wasi_snapshot_preview1: wasiImports(),
  wasi: { 'thread-spawn': threadSpawn },
  env: { memory: ctx.memory },
});

function report(error) {
  if (error === EXIT) return;
  postMessage({ type: 'crash', text: String(error?.stack ?? error) });
}

onmessage = async ({ data }) => {
  ctx = data;
  nextFd = 16 + (data.role === 'main' ? 0 : (data.slot + 1) * 4096);
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
    while (Atomics.load(ctx.ctl, slot) !== 2) Atomics.wait(ctx.ctl, slot, Atomics.load(ctx.ctl, slot));
    Atomics.store(ctx.ctl, slot, 3);
    const tid = ctx.spawnArgs[2 * data.slot], arg = ctx.spawnArgs[2 * data.slot + 1];
    try { instance.exports.wasi_thread_start(tid, arg); }
    catch (error) { report(error); if (error === EXIT) return; }
    instance = await instantiate();
  }
};
