// Runs a wasi module on the host, threads included, for ctest and the VS Code
// extensions. Whether the module uses threads is read off the module itself.
import { WASI } from 'node:wasi';
import { readFile } from 'node:fs/promises';
import { existsSync, readdirSync, realpathSync, writeSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { availableParallelism } from 'node:os';
import { setFlagsFromString } from 'node:v8';
import { Worker, isMainThread, workerData, threadId } from 'node:worker_threads';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { join, resolve, win32 } from 'node:path';

// Generated modules use standardized Wasm exceptions (try_table), which
// Node 20 cannot compile. The runner needs Node 22 or later.
const [major] = process.versions.node.split('.').map(Number);
if (major < 22) {
  console.error(`wasi-run.mjs needs node >= 22, got ${process.version}`);
  process.exit(1);
}
// Test the actual V8 feature, not Node's version: Electron can expose a newer
// V8 without the old experimental flag. Unknown V8 flags write directly to
// stderr, so check availability before attempting to enable one.
const exnrefProbe = Uint8Array.from([
  0,97,115,109,1,0,0,0, 1,4,1,96,0,0, 3,2,1,0,
  10,8,1,6,0,31,64,0,11,11,
]);
let exnrefFlagAvailable = false;
if (!WebAssembly.validate(exnrefProbe)) {
  exnrefFlagAvailable = workerData?.exnrefFlagAvailable === true;
  if (!exnrefFlagAvailable) {
    const options = spawnSync(process.execPath, ['--v8-options'], {
      encoding: 'utf8', timeout: 5000, maxBuffer: 4 * 1024 * 1024,
      env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' },
    });
    exnrefFlagAvailable = options.status === 0 && /--experimental-wasm-exnref(?:\s|$)/.test(options.stdout);
  }
  if (!exnrefFlagAvailable)
    throw new Error('This JavaScript runtime does not support WebAssembly exnref');
  setFlagsFromString('--experimental-wasm-exnref');
  if (!WebAssembly.validate(exnrefProbe))
    throw new Error('Cannot enable WebAssembly exnref in this JavaScript runtime');
}

const kOpenDirectory = 2;
const kErrnoBadFd = 8;
const kErrnoIo = 29;

// The whole filesystem is preopened: this runner is the caller, so the module
// reads the paths its caller chose. The module has one root, so on Windows each
// drive is a directory of it, named as a file URI names it: `/c:/a` is `C:\\a`.
export function preopensFor(platform, exists = existsSync) {
  if (platform !== 'win32') return { '/': '/' };
  const drives = {};
  for (const letter of 'abcdefghijklmnopqrstuvwxyz')
    if (exists(`${letter}:\\`)) drives[`/${letter}:`] = `${letter}:\\`;
  return drives;
}

// A host path as the module reads it. On Windows, one outside every drive, a
// share, is nowhere the module can read.
export function guestPath(platform, hostPath) {
  if (platform !== 'win32') return hostPath;
  const drive = /^([A-Za-z]):[\\/]*(.*)$/.exec(hostPath);
  return drive
    ? `/${drive[1].toLowerCase()}:/${drive[2].replaceAll('\\', '/')}`
    : '/';
}

// `fd_readdir` from the path a directory was opened by, which `node:wasi` lacks
// on Windows. `preopens` are the runtime's, numbered after the standard streams;
// addresses and sizes arrive signed and are read as unsigned.
export function listDirectoriesByPath(imports, memoryOf, preopens) {
  const { path_open: pathOpen, fd_close: fdClose } = imports;
  const directories = new Map(
    Object.values(preopens).map((path, index) => [index + 3, { path }]));
  imports.path_open = (dirFd, dirFlags, path, length, oflags, ...rest) => {
    const result = pathOpen(dirFd, dirFlags, path, length, oflags, ...rest);
    const parent = directories.get(dirFd);
    if (result === 0 && parent && (oflags & kOpenDirectory) !== 0) {
      const { buffer } = memoryOf();
      directories.set(new DataView(buffer).getUint32(rest.at(-1) >>> 0, true), {
        path: join(parent.path, Buffer.from(buffer, path >>> 0, length >>> 0).toString()),
      });
    }
    return result;
  };
  imports.fd_close = (fd) => {
    directories.delete(fd);
    return fdClose(fd);
  };
  imports.fd_readdir = (fd, buffer, length, cookie, used) => {
    const directory = directories.get(fd);
    if (!directory) return kErrnoBadFd;
    // Read once per listing: the module comes back once per buffer.
    if (cookie === 0n) {
      try {
        directory.entries = readdirSync(directory.path, { withFileTypes: true });
      } catch {
        return kErrnoIo;
      }
    }
    const memory = memoryOf().buffer;
    length >>>= 0;
    const target = new Uint8Array(memory, buffer >>> 0, length);
    let written = 0;
    for (let index = Number(cookie);
         index < directory.entries.length && written < length; ++index) {
      const entry = directory.entries[index];
      const name = Buffer.from(entry.name);
      const record = Buffer.alloc(24 + name.length);
      // The next cookie, then an inode: none is at hand, and zero would make the
      // C library ask for each one.
      record.writeBigUInt64LE(BigInt(index + 1), 0);
      record.writeBigUInt64LE(BigInt(index + 1), 8);
      record.writeUInt32LE(name.length, 16);
      record[20] = entry.isDirectory() ? 3 : entry.isFile() ? 4
        : entry.isSymbolicLink() ? 7 : 0;
      name.copy(record, 24);
      const kept = Math.min(record.length, length - written);
      target.set(record.subarray(0, kept), written);
      written += kept;
    }
    new DataView(memory).setUint32(used >>> 0, written, true);
    return 0;
  };
}

// On Windows, an argument naming a host path, an absolute drive path or an
// existing file, reaches the module with `/` separators; any other is kept.
export function guestArguments(platform, args, exists = existsSync) {
  if (platform !== 'win32') return args;
  return args.map((arg, index) =>
    index !== 0 && (/^[A-Za-z]:[\\/]/.test(arg) || exists(arg))
      ? guestPath(platform, win32.resolve(arg)) : arg);
}

function wasiFor(args, environment, hostModule, preopens) {
  return new WASI({
    version: 'preview1',
    args: guestArguments(process.platform, args),
    env: { ...process.env, ...environment,
      ...(hostModule ? { CURSIV_WASI_HOST_MODULE: hostModule } : {}),
      // The actual working directory, also when the caller changed it
      // without updating its inherited environment.
      PWD: guestPath(process.platform, process.cwd()) },
    preopens,
    returnOnExit: true,
  });
}

// An application host exports createHost({ memory, args, runner, hostModule }),
// which returns { imports, env, instantiated }; the runner owns WASI and
// threads. See docs/recipes/wasm.md.
async function applicationHost(hostModule, memory, args) {
  if (!hostModule) return {};
  const extension = await import(pathToFileURL(hostModule));
  if (typeof extension.createHost !== 'function')
    throw new Error('A WASI host module must export createHost(context)');
  return await extension.createHost({ memory, args,
    runner: fileURLToPath(import.meta.url), hostModule }) ?? {};
}

// The import section's memory entry: `flags` then `min` then, when the flags
// say so, `max`. Everything is LEB128. Bit 0 is "has maximum", bit 1 "shared".
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
          throw new Error('only a wasm32 imported memory is supported');
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

// Worker stdio is isolated to keep fd 1 blocking for the WASI protocol. JS's
// process.stderr goes through that isolated stream, so write plugin logs to
// the real host fd instead of dropping them in an unread worker Readable.
function writeStderr(text) {
  const bytes = Buffer.from(text);
  for (let at = 0; at < bytes.length;)
    at += writeSync(2, bytes, at, bytes.length - at);
}

// `node:wasi` reads `exports.memory`; a threaded module imports it instead,
// so it is presented here.
const withMemory = (instance, memory) =>
  memory ? { exports: { ...instance.exports, memory } } : instance;

// Node's IDs are unique across the process, including nested workers. WASI
// reserves zero and the top three bits of an i32 for its thread IDs.
const validThreadId = (id) => id > 0 && id < 2 ** 29;

// The words threads share with the supervisor, the main thread: the exit code
// (-1 while running), a bell the supervisor waits on, and one thread request at
// a time, with its lock, state, start argument and answer.
const EXIT = 0, BELL = 1, LOCK = 2, REQUEST = 3, START_ARG = 4, ANSWER = 5, CONTROL_WORDS = 6;
const ASKED = 1, ANSWERED = 2;
function ring(control) {
  Atomics.add(control, BELL, 1);
  Atomics.notify(control, BELL);
}

// One process exit, shared even by a child whose parent is blocked in WASM.
// The supervisor waits asynchronously, so traps cannot strand a pthread_join.
const exited = Symbol('WASI process exit');
function exitProcess(control, code) {
  if (Atomics.compareExchange(control, EXIT, -1, code & 0xff) === -1) ring(control);
}

// Asks the supervisor for a thread's worker, since a worker blocked in WASM
// would never learn of its failure. Answers the thread's ID, or -1.
function threadRequester(control) {
  return (startArg) => {
    while (Atomics.compareExchange(control, LOCK, 0, 1) !== 0) Atomics.wait(control, LOCK, 1);
    try {
      Atomics.store(control, START_ARG, startArg);
      Atomics.store(control, REQUEST, ASKED);
      ring(control);
      while (Atomics.load(control, REQUEST) === ASKED) Atomics.wait(control, REQUEST, ASKED);
      return Atomics.load(control, ANSWER);
    } finally {
      Atomics.store(control, REQUEST, 0);
      Atomics.store(control, LOCK, 0);
      Atomics.notify(control, LOCK, 1);
    }
  };
}

// Creates the workers and answers thread requests until the process has an
// exit code. A worker that fails or exits nonzero ends the process with 1.
async function supervise(control, workerData) {
  const start = (data) => {
    // Every worker shares the compiled module and the memory.
    const worker = new Worker(new URL(import.meta.url), {
      workerData: { ...workerData, ...data, control: control.buffer },
      // Forwarded output would make the host's fd 1 non-blocking and could
      // truncate WASI's synchronous writes.
      stdout: true,
      stderr: true,
    });
    worker.on('error', (error) => {
      console.error(error);
      exitProcess(control, 1);
    });
    return worker;
  };
  const command = start({ command: true });
  command.on('exit', (code) => exitProcess(control, code || 1));
  const thread = (startArg) => {
    let worker;
    try {
      worker = start({ startArg });
    } catch {
      return -1;
    }
    if (!validThreadId(worker.threadId)) {
      void worker.terminate();
      return -1;
    }
    worker.on('exit', (code) => { if (code !== 0) exitProcess(control, 1); });
    return worker.threadId;
  };
  for (;;) {
    const bell = Atomics.load(control, BELL);
    if (Atomics.load(control, EXIT) !== -1) return;
    if (Atomics.load(control, REQUEST) === ASKED) {
      Atomics.store(control, ANSWER, thread(Atomics.load(control, START_ARG)));
      Atomics.store(control, REQUEST, ANSWERED);
      Atomics.notify(control, REQUEST);
      continue;
    }
    await Atomics.waitAsync(control, BELL, bell).value;
  }
}

async function execute(module, memory, args, command, needsThreads, control,
                       startArg, hostModule) {
  let instance;
  const extension = await applicationHost(hostModule,
    () => memory ?? instance?.exports.memory, args);
  const preopens = preopensFor(process.platform);
  const wasi = wasiFor(args, extension.env, hostModule, preopens);
  const importObject = wasi.getImportObject();
  if (process.platform === 'win32')
    listDirectoriesByPath(importObject.wasi_snapshot_preview1,
      () => memory ?? instance.exports.memory, preopens);
  for (const [namespace, entries] of Object.entries(extension.imports ?? {}))
    importObject[namespace] = { ...importObject[namespace], ...entries };
  if (!command) {
    // returnOnExit only catches proc_exit inside start(), not a pthread entry.
    importObject.wasi_snapshot_preview1.proc_exit = (code) => {
      exitProcess(control, code);
      throw exited;
    };
  }
  if (needsThreads)
    importObject.wasi = { ...importObject.wasi, 'thread-spawn': threadRequester(control) };
  if (memory)
    for (const entry of WebAssembly.Module.imports(module))
      if (entry.kind === 'memory')
        importObject[entry.module] = {
          ...importObject[entry.module], [entry.name]: memory,
        };
  instance = await WebAssembly.instantiate(module, importObject);
  await extension.instantiated?.(instance);
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

function commandArguments(arguments_) {
  let hostModule = process.env.CURSIV_WASI_HOST_MODULE;
  if (arguments_[0] === '--host-module') {
    if (!arguments_[1]) throw new Error('--host-module requires a module path');
    hostModule = arguments_[1];
    arguments_ = arguments_.slice(2);
  } else if (arguments_[0]?.startsWith('--host-module=')) {
    hostModule = arguments_[0].slice('--host-module='.length);
    if (!hostModule) throw new Error('--host-module requires a module path');
    arguments_ = arguments_.slice(1);
  }
  if (!arguments_[0])
    throw new Error('Usage: wasi-run.mjs [--host-module PATH] MODULE [ARGUMENTS...]');
  return { modulePath: arguments_[0], args: arguments_.slice(1),
    hostModule: hostModule ? resolve(hostModule) : undefined };
}

function reportMemory(memory) {
  if (!process.env.CURSIV_WASI_REPORT_MEMORY) return;
  const grown = memory.buffer.byteLength;
  console.error(`[wasi-run] linear memory grew to ${grown} bytes (${(grown / 1048576).toFixed(1)} MiB)`);
}

if (isMainThread && process.argv[1] && realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const { modulePath, args, hostModule } = commandArguments(process.argv.slice(2));
  const bytes = await readFile(modulePath);
  const module = await WebAssembly.compile(bytes);

  const imports = WebAssembly.Module.imports(module);
  const memoryImports = imports.filter((i) => i.kind === 'memory');
  const needsMemory = memoryImports.length > 0;
  if (memoryImports.length > 1)
    throw new Error('wasi-run supports one imported linear memory');
  const needsThreads = imports.some((i) => i.module === 'wasi' && i.name === 'thread-spawn');

  // The sharing mode and optional maximum come from the module: memory must match the
  // declared maximum, which the JS reflection does not report -- so it is
  // read off the binary. Getting it wrong is a LinkError, never silent.
  const memory = needsMemory
    ? new WebAssembly.Memory(importedMemoryLimits(bytes))
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
    const control = new Int32Array(new SharedArrayBuffer(4 * CONTROL_WORDS));
    control[EXIT] = -1;
    await supervise(control, { module, memory, args: [modulePath, ...args], exnrefFlagAvailable, hostModule });
    reportMemory(memory);
    process.exit(Atomics.load(control, EXIT));
  }
  const result = await execute(module, memory, [modulePath, ...args], true,
                               false, undefined, undefined, hostModule);
  reportMemory(result.memory);
  process.exit(result.code);
} else if (!isMainThread && validThreadId(threadId) && workerData?.module instanceof WebAssembly.Module) {
  // An out-of-range ID must never enter WASI: its creator returns failure and
  // may release startArg before this worker has finished starting up.
  const { module, memory, startArg, args, command = false, hostModule } = workerData;
  const control = new Int32Array(workerData.control);
  try {
    const result = await execute(module, memory, args, command, true, control,
                                 startArg, hostModule);
    if (command) exitProcess(control, result.code);
  } catch (error) {
    if (error !== exited) {
      // Worker console output is intentionally isolated from the protocol fd.
      writeStderr(`${error?.stack ?? error}\n`);
      exitProcess(control, 1);
    }
  }
}
