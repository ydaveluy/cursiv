// Starts a Cursiv language server (a threaded WASI module) in Web Workers and
// speaks LSP to it: stdin is a SharedArrayBuffer ring, stdout arrives by
// postMessage. The page needs cross-origin isolation for SharedArrayBuffer.

const POOL = 4;                 // pre-started thread workers
const STDIN_CAPACITY = 1 << 20; // a power of two: see readStdin in wasi-worker.js
const HEADER_LIMIT = 4096;      // the most a frame's header may take
const modules = new Map();      // compiled modules, kept across restarts

// The import section's memory entry: `flags` then `min` then, when the flags
// say so, `max`, all LEB128. Bit 0 is "has maximum", bit 1 "shared". JS does
// not reflect the maximum, and a shared memory must match it exactly. The same
// reader as tools/wasi-run.mjs, whose tests hold the two to the same answers.
export function importedMemoryLimits(bytes) {
  let at = 8;  // past the magic and the version
  let limit = bytes.length;
  const byte = () => {
    if (at >= limit) throw new Error('Truncated WebAssembly import section');
    return bytes[at++];
  };
  const leb = () => {
    let value = 0;
    for (let shift = 0; shift <= 28; shift += 7) {
      const part = byte();
      if (shift === 28 && part > 0x0f)
        throw new Error('Invalid WebAssembly u32 LEB128');
      value += (part & 0x7f) * 2 ** shift;
      if (!(part & 0x80)) return value;
    }
    throw new Error('Invalid WebAssembly u32 LEB128');
  };
  const skipLeb = () => {
    for (let count = 0; count < 10; ++count)
      if (!(byte() & 0x80)) return;
    throw new Error('Invalid WebAssembly LEB128');
  };
  const name = () => {
    const length = leb();
    if (length > limit - at) throw new Error('Truncated WebAssembly import name');
    at += length;
  };
  const valueType = () => {
    const type = byte();
    if (type === 0x63 || type === 0x64) skipLeb();  // typed reference + heap type
  };
  const skipLimits = () => {
    const flags = leb();
    skipLeb();
    if (flags & 1) skipLeb();
  };
  while (at < bytes.length) {
    const id = byte();
    const size = leb();
    const end = at + size;
    if (end > bytes.length) throw new Error('Truncated WebAssembly section');
    if (id !== 2) { at = end; continue; }          // 2 is the import section
    limit = end;
    for (let i = leb(); i > 0; --i) {
      name(); name();
      const kind = byte();
      if (kind === 0x02) {                          // 0x02 is a memory import
        const flags = leb();
        if (flags & ~0x03)
          throw new Error('the demo supports wasm32 imported memory only');
        const initial = leb();
        return { initial, shared: Boolean(flags & 0x02),
          ...(flags & 0x01 ? { maximum: leb() } : {}) };
      }
      if (kind === 0x00) leb();                     // function: a type index
      else if (kind === 0x01) { valueType(); skipLimits(); }
      else if (kind === 0x03) { valueType(); byte(); } // global: type, mutability
      else if (kind === 0x04) { byte(); leb(); }     // exception tag: attribute, type index
      else throw new Error(`Unsupported WebAssembly import kind ${kind}`);
    }
    at = end;
    limit = bytes.length;
  }
  return undefined;
}

async function loadModule(url) {
  if (!modules.has(url)) {
    modules.set(url, (async () => {
      const response = await fetch(url);
      if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
      const bytes = new Uint8Array(await response.arrayBuffer());
      const module = await WebAssembly.compile(bytes);
      // Its threads run in workers, which only a shared memory reaches.
      const limits = importedMemoryLimits(bytes);
      if (!limits?.shared) throw new Error(`${url} imports no shared memory: the page runs threaded modules only`);
      return { module, limits, size: bytes.length };
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
  const memory = new WebAssembly.Memory(limits);

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
    worker.addEventListener('message', ({ data }) => {
      if (data.type === 'ready') resolve(worker);
      else if (data.type === 'crash') reject(new Error(data.text.split('\n')[0]));
    });
    worker.addEventListener('error', (e) => { reject(e); stop(`worker error: ${e.message}`); });
    worker.postMessage({ ...shared, ...msg });
    if (msg.role === 'main') resolve(worker);
  });
  await Promise.all(Array.from({ length: POOL }, (_, slot) => spawn({ role: 'thread', slot })));
  await spawn({ role: 'main' });

  // The LSP base protocol, framed per worker: a message never spans threads.
  // A frame the page cannot read ends the session.
  function feed(worker, chunk) {
    const before = frames.get(worker);
    let buf = chunk;
    if (before?.length) {
      buf = new Uint8Array(before.length + chunk.length);
      buf.set(before); buf.set(chunk, before.length);
    }
    const messages = [];
    try {
      for (;;) {
        const sep = headerEnd(buf);
        if (sep < 0) break;
        const length = /^Content-Length: *(\d+)\s*$/im.exec(dec.decode(buf.subarray(0, sep)));
        if (!length) throw new Error('no Content-Length');
        const start = sep + 4, end = start + Number(length[1]);
        if (buf.length < end) break;
        messages.push(JSON.parse(dec.decode(buf.subarray(start, end))));
        buf = buf.subarray(end);
      }
    } catch (error) {
      stop(`the server sent a message the page cannot read: ${error.message}`);
      return;
    }
    frames.set(worker, buf.slice());
    for (const message of messages)
      for (const listener of listeners) listener(message);
  }
  // Where the header's blank line ends, -1 while it has not come.
  function headerEnd(buf) {
    for (let i = 0; i + 3 < Math.min(buf.length, HEADER_LIMIT); i++)
      if (buf[i] === 13 && buf[i + 1] === 10 && buf[i + 2] === 13 && buf[i + 3] === 10) return i;
    if (buf.length >= HEADER_LIMIT) throw new Error(`a header past ${HEADER_LIMIT} bytes`);
    return -1;
  }

  // What the ring has no room for yet, retried by one timer.
  const pending = [];
  let retry = null;
  function pump() {
    retry = null;
    while (pending.length && !stopped) {
      const head = Atomics.load(stdinHdr, 0), tail = Atomics.load(stdinHdr, 1);
      const free = STDIN_CAPACITY - ((head - tail) >>> 0);
      const chunk = pending[0];
      const n = Math.min(free, chunk.length);
      if (n === 0) { retry = setTimeout(pump, 1); return; }
      for (let k = 0; k < n; k++) stdinBuf[(head + k) & (STDIN_CAPACITY - 1)] = chunk[k];
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
    if (!retry) pump();
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
