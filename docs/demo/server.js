// Starts a Cursiv language server (a threaded WASI module) in Web Workers and
// speaks LSP to it: stdin is a SharedArrayBuffer ring, stdout arrives by
// postMessage. The page needs cross-origin isolation for SharedArrayBuffer.

const POOL = 4;                 // pre-started thread workers
const STDIN_CAPACITY = 1 << 20;
const modules = new Map();      // compiled modules, kept across restarts

// The import section's memory entry: JS does not reflect the maximum, and a
// shared memory must match it exactly (same reader as tools/wasi-run.mjs).
function importedMemoryLimits(bytes) {
  let at = 8;
  const leb = () => { let v = 0, s = 0, b; do { b = bytes[at++]; v |= (b & 0x7f) << s; s += 7; } while (b & 0x80); return v >>> 0; };
  const skipName = () => { const n = leb(); at += n; };
  while (at < bytes.length) {
    const id = bytes[at++], size = leb(), end = at + size;
    if (id !== 2) { at = end; continue; }
    for (let i = leb(); i > 0; --i) {
      skipName(); skipName();
      const kind = bytes[at++];
      if (kind === 0x02) { const flags = leb(), initial = leb(); return { initial, maximum: flags & 1 ? leb() : initial }; }
      if (kind === 0x00) leb();
      else if (kind === 0x01) { at++; const f = leb(); leb(); if (f & 1) leb(); }
      else if (kind === 0x03) at += 2;
      else at = end;
    }
    at = end;
  }
  throw new Error('the module imports no memory');
}

async function loadModule(url) {
  if (!modules.has(url)) {
    modules.set(url, (async () => {
      const response = await fetch(url);
      if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
      const bytes = new Uint8Array(await response.arrayBuffer());
      return { module: await WebAssembly.compile(bytes), limits: importedMemoryLimits(bytes), size: bytes.length };
    })());
    modules.get(url).catch(() => modules.delete(url));
  }
  return modules.get(url);
}

// `files` maps absolute paths under `root` to bytes: a read-only folder the
// server sees as its workspace.
export async function startServer(url, { env = {}, root = null, files = {}, onLog = () => {}, onExit = () => {} } = {}) {
  if (!crossOriginIsolated) throw new Error('the page is not cross-origin isolated');
  const { module, limits, size } = await loadModule(url);
  const memory = new WebAssembly.Memory({ ...limits, shared: true });

  const ctl = new Int32Array(new SharedArrayBuffer(4 * (2 + POOL)));
  ctl[0] = 2; ctl[1] = -1; ctl.fill(3, 2);
  const spawnArgs = new Int32Array(new SharedArrayBuffer(8 * POOL));
  const stdinHdr = new Int32Array(new SharedArrayBuffer(12));
  const stdinBuf = new Uint8Array(new SharedArrayBuffer(STDIN_CAPACITY));
  const shared = { module, memory, ctl, spawnArgs, stdinHdr, stdinBuf, root, files,
                   env: { CURSIV_WORKERS: '0', ...env } };

  const listeners = new Set();
  const waiting = new Map();    // request id -> [resolve, reject]
  const frames = new Map();
  const workers = [];
  let stopped = false;
  const stop = (reason) => {
    if (stopped) return;
    stopped = true;
    workers.forEach((w) => w.terminate());
    for (const [, fail] of waiting.values()) fail(new Error(reason));
    waiting.clear();
    onExit(reason);
  };
  const dec = new TextDecoder();
  const onMessage = (worker) => ({ data }) => {
    if (data.type === 'out' && data.fd === 1) feed(worker, data.bytes);
    else if (data.type === 'out') onLog(dec.decode(data.bytes));
    else if (data.type === 'log') onLog(data.text);
    else if (data.type === 'crash') stop(`server crashed: ${data.text.split('\n')[0]}`);
    else if (data.type === 'exit') stop(`server exited with code ${data.code}`);
  };
  const spawn = (msg) => new Promise((resolve, reject) => {
    const worker = new Worker(new URL('./wasi-worker.js', import.meta.url));
    workers.push(worker);
    worker.addEventListener('message', onMessage(worker));
    worker.addEventListener('message', ({ data }) => data.type === 'ready' && resolve(worker));
    worker.addEventListener('error', (e) => { reject(e); stop(`worker error: ${e.message}`); });
    worker.postMessage({ ...shared, ...msg });
    if (msg.role === 'main') resolve(worker);
  });
  await Promise.all(Array.from({ length: POOL }, (_, slot) => spawn({ role: 'thread', slot })));
  await spawn({ role: 'main' });

  // The LSP base protocol, framed per worker: a message never spans threads.
  function feed(worker, chunk) {
    let buf = frames.get(worker) ?? new Uint8Array(0);
    const merged = new Uint8Array(buf.length + chunk.length);
    merged.set(buf); merged.set(chunk, buf.length); buf = merged;
    for (;;) {
      const head = dec.decode(buf.subarray(0, Math.min(buf.length, 256)));
      const sep = head.indexOf('\r\n\r\n');
      if (sep < 0) break;
      const len = Number(/Content-Length: *(\d+)/i.exec(head.slice(0, sep))[1]);
      const start = sep + 4;
      if (buf.length < start + len) break;
      const message = JSON.parse(dec.decode(buf.subarray(start, start + len)));
      buf = buf.slice(start + len);
      for (const listener of listeners) listener(message);
    }
    frames.set(worker, buf);
  }

  const pending = [];
  function pump() {
    while (pending.length && !stopped) {
      const head = Atomics.load(stdinHdr, 0), tail = Atomics.load(stdinHdr, 1);
      const free = STDIN_CAPACITY - ((head - tail) >>> 0);
      const chunk = pending[0];
      const n = Math.min(free, chunk.length);
      if (n === 0) { setTimeout(pump, 1); return; }
      for (let k = 0; k < n; k++) stdinBuf[(head + k) % STDIN_CAPACITY] = chunk[k];
      Atomics.store(stdinHdr, 0, (head + n) | 0);
      Atomics.notify(stdinHdr, 0);
      if (n === chunk.length) pending.shift(); else pending[0] = chunk.subarray(n);
    }
  }
  const enc = new TextEncoder();
  function send(message) {
    if (stopped) return;
    const body = enc.encode(JSON.stringify(message));
    pending.push(enc.encode(`Content-Length: ${body.length}\r\n\r\n`), body);
    pump();
  }

  let nextId = 1;
  const notificationHandlers = new Map();
  listeners.add((m) => {
    if (m.id !== undefined && !m.method) {
      const entry = waiting.get(m.id);
      if (!entry) return;
      waiting.delete(m.id);
      if (m.error) entry[1](Object.assign(new Error(m.error.message), m.error));
      else entry[0](m.result);
    } else if (m.id !== undefined && m.method) {
      // Requests from the server: no settings, every registration accepted.
      send({ jsonrpc: '2.0', id: m.id,
             result: m.method === 'workspace/configuration' ? m.params.items.map(() => null) : null });
    } else if (m.method) {
      for (const fn of notificationHandlers.get(m.method) ?? []) fn(m.params);
    }
  });

  return {
    size,
    request(method, params, token) {
      return new Promise((resolve, reject) => {
        if (stopped) { reject(new Error('the server is not running')); return; }
        const id = nextId++;
        waiting.set(id, [resolve, reject]);
        token?.onCancellationRequested?.(() => send({ jsonrpc: '2.0', method: '$/cancelRequest', params: { id } }));
        send({ jsonrpc: '2.0', id, method, params });
      });
    },
    notify: (method, params) => send({ jsonrpc: '2.0', method, params }),
    on(method, fn) {
      if (!notificationHandlers.has(method)) notificationHandlers.set(method, new Set());
      notificationHandlers.get(method).add(fn);
    },
    get running() { return !stopped; },
    dispose: () => stop('stopped'),
  };
}
