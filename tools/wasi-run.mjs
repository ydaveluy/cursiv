// Runs a wasi module on the host, threads included: ctest's emulator and the
// VS Code extensions both start servers through it. Threads are read off the
// module, not configured, and the provisional `wasi-threads` protocol stays
// confined to this file.
import { WASI } from 'node:wasi';
import { readFile } from 'node:fs/promises';
import { availableParallelism } from 'node:os';
import { setFlagsFromString } from 'node:v8';
import { Worker, isMainThread, workerData, threadId } from 'node:worker_threads';

// Generated modules use standardized Wasm exceptions (try_table), which
// Node 20 cannot compile. The runner needs Node 22 or later.
const [major] = process.versions.node.split('.').map(Number);
if (major < 22) {
  console.error(`wasi-run.mjs needs node >= 22, got ${process.version}`);
  process.exit(1);
}
// Node 22–24 implement the instructions behind a flag; newer V8 enables them.
// Every worker runs this before instantiating its copy of the module.
if (major < 25) setFlagsFromString('--experimental-wasm-exnref');

// The whole filesystem is preopened: this runner is the caller, so the
// module reads the paths its caller chose. `.` is separate because WASI has
// no chdir -- a relative path resolves against a preopen by name.
const preopens = () => ({ '.': process.cwd(), '/': '/' });

function wasiFor(args) {
  return new WASI({
    version: 'preview1',
    args,
    env: process.env,
    preopens: preopens(),
    returnOnExit: true,
  });
}

// The import section's memory entry: `flags` then `min` then, when the flags
// say so, `max`. Everything is LEB128. Bit 0 is "has maximum", bit 1 "shared".
function importedMemoryLimits(bytes) {
  let at = 8;  // past the magic and the version
  const leb = () => {
    let value = 0, shift = 0, byte;
    do { byte = bytes[at++]; value |= (byte & 0x7f) << shift; shift += 7; }
    while (byte & 0x80);
    return value >>> 0;
  };
  const name = () => { const n = leb(); const s = bytes.toString('utf8', at, at + n); at += n; return s; };
  while (at < bytes.length) {
    const id = bytes[at++];
    const size = leb();
    const end = at + size;
    if (id !== 2) { at = end; continue; }          // 2 is the import section
    for (let i = leb(); i > 0; --i) {
      name(); name();
      const kind = bytes[at++];
      if (kind === 0x02) {                          // 0x02 is a memory import
        const flags = leb();
        const initial = leb();
        return { initial, maximum: flags & 0x1 ? leb() : initial };
      }
      if (kind === 0x00) leb();                     // function: a type index
      else if (kind === 0x01) { at++; const f = leb(); leb(); if (f & 1) leb(); }
      else if (kind === 0x03) { at++; at++; }       // global: type, mutability
      else at = end;
    }
    at = end;
  }
  return { initial: 256, maximum: 2048 };
}

// `node:wasi` reads `exports.memory`; a threaded module imports it instead,
// so it is presented here. The one seam between its host and ours.
const withMemory = (instance, memory) =>
  memory ? { exports: { ...instance.exports, memory } } : instance;

// Node's IDs are unique across the process, including nested workers. WASI
// reserves zero and the top three bits of an i32 for its thread IDs.
const validThreadId = (id) => id > 0 && id < 2 ** 29;

// One process exit, shared even by a child whose parent is blocked in WASM.
// The supervisor waits asynchronously, so traps cannot strand a pthread_join.
const exited = Symbol('WASI process exit');
function exitProcess(control, code) {
  if (Atomics.compareExchange(control, 0, -1, code & 0xff) === -1)
    Atomics.notify(control, 0);
}

function threadSpawner(module, memory, args, control) {
  return (startArg) => {
    let worker;
    try {
      // Share the compiled module and memory; copying and recompiling the
      // binary for each thread is unnecessary.
      worker = new Worker(new URL(import.meta.url), {
        workerData: { module, memory, startArg, args, control: control.buffer },
        // Forwarding worker output would put the host's fd 1 in O_NONBLOCK
        // and could truncate WASI's synchronous protocol writes.
        stdout: true,
        stderr: true,
      });
    } catch {
      return -1;
    }
    if (!validThreadId(worker.threadId)) {
      void worker.terminate();
      return -1;
    }
    // A child may outlive its C++ creator: keep its parent's JS host alive.
    return worker.threadId;
  };
}

async function execute(module, memory, args, command, needsThreads, control,
                       startArg) {
  const wasi = wasiFor(args);
  const imports = wasi.getImportObject();
  if (!command) {
    // returnOnExit only catches proc_exit inside start(), not a pthread entry.
    imports.wasi_snapshot_preview1.proc_exit = (code) => {
      exitProcess(control, code);
      throw exited;
    };
  }
  const instance = await WebAssembly.instantiate(module, {
    ...imports,
    ...(memory ? { env: { memory } } : {}),
    ...(needsThreads ? {
      wasi: { 'thread-spawn': threadSpawner(module, memory, args, control) },
    } : {}),
  });
  if (command)
    return { code: wasi.start(withMemory(instance, memory)),
             memory: memory ?? instance.exports.memory };
  try {
    wasi.initialize(withMemory(instance, memory));
  } catch {
    // Command modules export _start, which initialize refuses after binding.
  }
  instance.exports.wasi_thread_start(threadId, startArg);
}

function reportMemory(memory) {
  if (!process.env.CURSIV_WASI_REPORT_MEMORY) return;
  const grown = memory.buffer.byteLength;
  console.error(`[wasi-run] linear memory grew to ${grown} bytes (${(grown / 1048576).toFixed(1)} MiB)`);
}

if (isMainThread) {
  const [, , modulePath, ...args] = process.argv;
  const bytes = await readFile(modulePath);
  const module = await WebAssembly.compile(bytes);

  const imports = WebAssembly.Module.imports(module);
  const needsMemory = imports.some((i) => i.kind === 'memory');
  const needsThreads = imports.some((i) => i.module === 'wasi' && i.name === 'thread-spawn');

  // Shared, and bounded by the module: an imported memory must match the
  // declared maximum, which the JS reflection does not report -- so it is
  // read off the binary. Getting it wrong is a LinkError, never silent.
  const memory = needsMemory
    ? new WebAssembly.Memory({ ...importedMemoryLimits(bytes), shared: true })
    : undefined;

  // A module without threads must not be told to use them: `CURSIV_WORKERS`
  // asks for a pool, and building one there throws "Not supported" out of
  // every request. The module's own imports are the authority, not the caller.
  if (!needsThreads) {
    process.env.CURSIV_WORKERS = '0';
  } else if (process.env.CURSIV_WORKERS === undefined) {
    // WASI cannot query the host's CPUs. Resolve its default once here,
    // leaving one participant for the calling thread and respecting overrides.
    process.env.CURSIV_WORKERS = String(availableParallelism() - 1);
  }

  if (needsThreads) {
    const control = new Int32Array(new SharedArrayBuffer(4));
    control[0] = -1;
    const worker = new Worker(new URL(import.meta.url), {
      workerData: { module, memory, args: [modulePath, ...args],
                    command: true, control: control.buffer },
      stdout: true,
      stderr: true,
    });
    worker.on('error', (error) => {
      console.error(error);
      exitProcess(control, 1);
    });
    worker.on('exit', (code) => exitProcess(control, code || 1));
    while (Atomics.load(control, 0) === -1)
      await Atomics.waitAsync(control, 0, -1).value;
    reportMemory(memory);
    process.exit(Atomics.load(control, 0));
  }
  const result = await execute(module, memory, [modulePath, ...args], true,
                               false);
  reportMemory(result.memory);
  process.exit(result.code);
} else if (validThreadId(threadId)) {
  // An out-of-range ID must never enter WASI: its creator returns failure and
  // may release startArg before this worker has finished starting up.
  const { module, memory, startArg, args, command = false } = workerData;
  const control = new Int32Array(workerData.control);
  try {
    const result = await execute(module, memory, args, command, true, control,
                                 startArg);
    if (command) exitProcess(control, result.code);
  } catch (error) {
    if (error !== exited) {
      // Worker console output is intentionally isolated from the protocol fd.
      const { writeSync } = await import('node:fs');
      writeSync(2, `${error?.stack ?? error}\n`);
      exitProcess(control, 1);
    }
  }
}
