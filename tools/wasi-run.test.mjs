import assert from 'node:assert/strict';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir, homedir } from 'node:os';
import { join } from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { guestArguments, guestPath, importedMemoryLimits, listDirectoriesByPath,
  preopensFor } from './wasi-run.mjs';
import { importedMemoryLimits as pageMemoryLimits } from '../docs/demo/server.js';

const runner = process.env.CURSIV_TEST_WASI_RUNNER ?? fileURLToPath(new URL('./wasi-run.mjs', import.meta.url));
const argument = process.argv.indexOf('--sdk');
const sdk = argument >= 0 ? process.argv[argument + 1]
  : [process.env.WASI_SDK, join(homedir(), 'wasi-sdk-34.0'), '/opt/wasi-sdk']
      .find(path => path && existsSync(join(path, 'bin/clang')));
test('each Windows drive is a directory of the module\'s one root', () => {
  const exists = (path) => path === 'c:\\' || path === 'D:\\' || path === 'd:\\';
  assert.deepEqual(preopensFor('win32', exists),
    { '/c:': 'c:\\', '/d:': 'd:\\' });
  assert.deepEqual(preopensFor('linux', exists), { '/': '/' });
  assert.deepEqual(preopensFor('darwin', exists), { '/': '/' });
});

// The module passes its addresses as i32: past 2 GiB, they arrive negative.
for (const base of [0, 2 ** 31]) test(`a directory is listed from the path it was opened by${base ? ', its buffers past 2 GiB' : ''}`, () => {
  const root = mkdtempSync(join(tmpdir(), 'cursiv-wasi-list-'));
  try {
    const listed = join(root, 'a', 'b');
    mkdirSync(join(listed, 'folder'), { recursive: true });
    const files = Array.from({ length: 40 }, (_, i) => `file-${i}-with-a-long-name.txt`);
    for (const file of files) writeFileSync(join(listed, file), '');
    // Creating a link takes a privilege on Windows.
    const links = process.platform !== 'win32';
    if (links) symlinkSync('folder', join(listed, 'link'));

    const memory = new WebAssembly.Memory({ initial: base / 65536 + 1 });
    const closed = [];
    let nextFd = 7;
    // As node:wasi does, reading each address as unsigned.
    const imports = {
      path_open: (dirFd, dirFlags, path, length, oflags, rights, inheriting, flags, opened) => {
        new DataView(memory.buffer).setUint32(opened >>> 0, nextFd++, true);
        return 0;
      },
      fd_close: (fd) => { closed.push(fd); return 0; },
    };
    listDirectoriesByPath(imports, () => memory, { '/first': '/nowhere', '/second': root });

    const [pathAt, usedAt, openedAt, bufferAt] = [16, 32, 40, 1024].map((at) => base + at);
    const signed = (address) => address | 0;
    const open = (dirFd, path, oflags = 2) => {
      new Uint8Array(memory.buffer).set(Buffer.from(path), pathAt);
      assert.equal(imports.path_open(dirFd, 0, signed(pathAt), path.length, oflags, 0n, 0n, 0, signed(openedAt)), 0);
      return new DataView(memory.buffer).getUint32(openedAt, true);
    };
    // As the C library reads: what the buffer cut off comes again from the last
    // whole entry's cookie, and a buffer left short says that was all.
    const read = (fd, size) => {
      const entries = new Map();
      for (let cookie = 0n; ;) {
        assert.equal(imports.fd_readdir(fd, signed(bufferAt), size, cookie, signed(usedAt)), 0);
        const view = new DataView(memory.buffer);
        const used = view.getUint32(usedAt, true);
        for (let at = 0; at + 24 <= used;) {
          const nameLength = view.getUint32(bufferAt + at + 16, true);
          if (at + 24 + nameLength > used) break;
          const name = Buffer.from(memory.buffer, bufferAt + at + 24, nameLength).toString();
          assert.ok(!entries.has(name), `${name} came twice`);
          entries.set(name, view.getUint8(bufferAt + at + 20));
          assert.notEqual(view.getBigUint64(bufferAt + at + 8, true), 0n);
          cookie = view.getBigUint64(bufferAt + at, true);
          at += 24 + nameLength;
        }
        if (used < size) return entries;
      }
    };

    const fd = open(4, 'a/b');
    const expected = new Map(files.map((file) => [file, 4]));
    expected.set('folder', 3);
    if (links) expected.set('link', 7);
    assert.deepEqual(read(fd, 256), expected);
    assert.deepEqual(read(fd, 4096), expected, 'a cookie of zero reads it again');

    writeFileSync(join(listed, 'later.txt'), '');
    expected.set('later.txt', 4);
    assert.deepEqual(read(fd, 4096), expected, 'a new reading sees what changed');

    // A directory opened from an open directory, not a preopen.
    assert.deepEqual(read(open(fd, 'folder'), 256), new Map());

    assert.equal(imports.fd_readdir(99, bufferAt, 64, 0n, usedAt), 8, 'an unknown descriptor');
    const file = open(4, join('a', 'b', files[0]), 0);
    assert.equal(imports.fd_readdir(file, bufferAt, 64, 0n, usedAt), 8, 'a file');
    imports.fd_close(fd);
    assert.deepEqual(closed, [fd]);
    assert.equal(imports.fd_readdir(fd, bufferAt, 64, 0n, usedAt), 8, 'a closed directory');

    const gone = open(4, 'a/gone');
    assert.equal(imports.fd_readdir(gone, bufferAt, 64, 0n, usedAt), 29, 'a directory gone');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a host path reaches the module in its own spelling', () => {
  assert.equal(guestPath('win32', 'C:\\Users\\me'), '/c:/Users/me');
  assert.equal(guestPath('win32', 'd:\\'), '/d:/');
  // A share is not a drive: the module sees its root.
  assert.equal(guestPath('win32', '\\\\server\\share'), '/');
  assert.equal(guestPath('linux', '/home/me'), '/home/me');
  assert.deepEqual(guestArguments('linux', ['m', 'a\\b'], () => true), ['m', 'a\\b']);
  // The module's own name is kept, and so is anything that names no file.
  assert.deepEqual(guestArguments('win32', ['m', '--flag', 'C:\\a'],
    (path) => path === 'C:\\a'), ['m', '--flag', '/c:/a']);
  // An absolute drive path is a path whether or not it exists yet.
  assert.deepEqual(guestArguments('win32', ['m', 'C:\\out\\new.txt', 'd:/b', 'new.txt'],
    () => false), ['m', '/c:/out/new.txt', '/d:/b', 'new.txt']);
});

const u32 = value => {
  const bytes = [];
  do {
    const part = value % 128;
    value = Math.floor(value / 128);
    bytes.push(part | (value ? 128 : 0));
  } while (value);
  return bytes;
};
const name = value => [...u32(Buffer.byteLength(value)), ...Buffer.from(value)];
const section = (id, bytes) => [id, ...u32(bytes.length), ...bytes];
const header = [0, 97, 115, 109, 1, 0, 0, 0];
const importEntry = (module, field, kind, descriptor) =>
  [...name(module), ...name(field), kind, ...descriptor];
function memoryModule(flags = 0, initial = 1, maximum, preceding = [], module = 'env', field = 'memory') {
  const descriptor = [...u32(flags), ...u32(initial), ...(flags & 1 ? u32(maximum) : [])];
  const imports = [...preceding, importEntry(module, field, 2, descriptor)];
  const grow = [0, 0x41, 1, 0x40, 0, 0x41, ...u32(initial), 0x47,
    0x04, 0x40, 0x00, 0x0b, 0x0b];
  return Buffer.from([...header,
    ...section(1, [1, 0x60, 0, 0]),
    ...section(2, [...u32(imports.length), ...imports.flat()]),
    ...section(3, [1, 0]),
    ...section(7, [2, ...name('memory'), 2, 0, ...name('_start'), 0,
      0]),
    ...section(10, [1, ...u32(grow.length), ...grow])]);
}

test('imported memory preserves sharing and optional maximum', async () => {
  const unbounded = memoryModule();
  await WebAssembly.compile(unbounded);
  assert.deepEqual(importedMemoryLimits(unbounded), { initial: 1, shared: false });
  assert.deepEqual(importedMemoryLimits(memoryModule(1, 2, 65536)),
    { initial: 2, maximum: 65536, shared: false });
  assert.deepEqual(importedMemoryLimits(memoryModule(3, 1, 32)),
    { initial: 1, maximum: 32, shared: true });
  const memory = new WebAssembly.Memory(importedMemoryLimits(unbounded));
  assert.equal(memory.grow(1), 1);
});

// The online demo cannot import this runner: it carries its own copy of the
// reader, which must stay the same.
test('the online demo reads the descriptor with the same code', () => {
  assert.equal(pageMemoryLimits.toString(), importedMemoryLimits.toString());
});

test('tag/table/global imports before memory do not hide its descriptor', async () => {
  const bytes = memoryModule(3, 1, 16, [
    importEntry('env', 'callback', 0, [0]),
    importEntry('env', 'exception', 4, [0, 0]),
    importEntry('env', 'table', 1, [0x70, 0, 0]),
    importEntry('env', 'constant', 3, [0x7f, 0]),
  ]);
  await WebAssembly.compile(bytes);
  assert.deepEqual(importedMemoryLimits(bytes), { initial: 1, maximum: 16, shared: true });
  assert.equal(importedMemoryLimits(Buffer.from(header)), undefined);
  assert.throws(() => importedMemoryLimits(Buffer.from([...header, 2, 10])), /Truncated/);
  assert.throws(() => importedMemoryLimits(memoryModule(4)), /wasm32/);
  assert.deepEqual(importedMemoryLimits(
    memoryModule(3, 1, 16, [importEntry('env', 'table', 1, [0x63, 0x70, 0, 0])])),
    { initial: 1, maximum: 16, shared: true });
});

test('runner starts nonshared imported memory and permits memory.grow without maximum', () => {
  const directory = mkdtempSync(join(tmpdir(), 'cursiv-wasi-memory-'));
  try {
    for (const [flags, maximum] of [[0, undefined], [1, 4]]) {
      const path = join(directory, `memory-${flags}.wasm`);
      // Alternate module/name proves the import is bound by its declaration.
      writeFileSync(path, memoryModule(flags, 1, maximum, [], 'host', 'linear'));
      const result = spawnSync(process.execPath, [runner, path], { encoding: 'utf8', timeout: 15000 });
      assert.equal(result.status, 0, result.stderr || String(result.error));
    }
    const alias = join(directory, 'runner.mjs');
    symlinkSync(runner, alias);
    const result = spawnSync(process.execPath, [alias, join(directory, 'memory-0.wasm')],
      { encoding: 'utf8', timeout: 15000 });
    assert.equal(result.status, 0, result.stderr || String(result.error));
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});

// Beside its destination, the SDK download works in a folder of its own. A
// proxy nobody answers keeps every download here from leaving the machine.
test('the SDK download keeps a folder it did not make, and takes back its own', () => {
  const directory = mkdtempSync(join(tmpdir(), 'cursiv-wasi-fetch-'));
  try {
    const fetcher = fileURLToPath(new URL('./fetch-wasi-sdk.cmake', import.meta.url));
    const dest = join(directory, 'sdk'), work = `${dest}.fetching`;
    const env = { ...process.env, https_proxy: 'http://127.0.0.1:9', HTTPS_PROXY: 'http://127.0.0.1:9' };
    const fetch = () => spawnSync('cmake', [`-DDEST=${dest.replaceAll('\\', '/')}`, '-P', fetcher],
      { encoding: 'utf8', timeout: 60000, env });
    mkdirSync(work);
    writeFileSync(join(work, 'notes.txt'), 'kept');
    const refused = fetch();
    assert.notEqual(refused.status, 0);
    assert.match(refused.stderr, /was not made by\s+this\s+script/);
    assert.equal(readFileSync(join(work, 'notes.txt'), 'utf8'), 'kept');

    // What an interrupted run leaves: its lock, and part of a download.
    rmSync(work, { recursive: true });
    mkdirSync(join(work, 'download'), { recursive: true });
    writeFileSync(join(work, 'cmake.lock'), '');
    writeFileSync(join(work, 'download', 'partial.tar.gz'), '');
    const resumed = fetch();
    assert.notEqual(resumed.status, 0);
    assert.match(resumed.stderr, /cannot\s+fetch\s+wasi-sdk/);
    assert.ok(!existsSync(join(work, 'download')), 'the failed download is cleared');
    assert.ok(!existsSync(dest));
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});

test('repository toolchain runs its C++ exception/pthread contract with threads ON and OFF',
  { skip: !sdk && 'wasi-sdk is not installed' }, () => {
    const directory = mkdtempSync(join(tmpdir(), 'cursiv-wasi-toolchain-'));
    const toolchain = fileURLToPath(new URL('../cmake/wasi.cmake', import.meta.url));
    try {
      writeFileSync(join(directory, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.24)
project(WasiRunnerContract LANGUAGES CXX)
add_executable(runtime main.cpp)
if(CURSIV_WASI_THREADS)
  target_compile_definitions(runtime PRIVATE EXPECTED_THREADS)
endif()
`);
      writeFileSync(join(directory, 'main.cpp'), `#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef EXPECTED_THREADS
#include <pthread.h>
static void* worker(void*) { std::fputs("toolchain-thread\\n", stderr); return nullptr; }
#endif
int main() {
  bool caught = false;
  try { throw 42; } catch (int value) { caught = value == 42; }
  if (!caught) return 40;
#ifdef EXPECTED_THREADS
  if (std::strcmp(std::getenv("CURSIV_WORKERS"), "2")) return 41;
  pthread_t thread;
  if (pthread_create(&thread, nullptr, worker, nullptr)) return 42;
  if (pthread_join(thread, nullptr)) return 43;
#else
  if (std::strcmp(std::getenv("CURSIV_WORKERS"), "0")) return 44;
#endif
  std::puts("toolchain-contract");
}
`);
      for (const mode of ['ON', 'OFF']) {
        const build = join(directory, `build-${mode}`);
        const configured = spawnSync('cmake', ['-S', directory, '-B', build, '-G', 'Ninja',
          `-DCMAKE_TOOLCHAIN_FILE=${toolchain}`, `-DWASI_SDK=${sdk}`,
          `-DCURSIV_WASI_THREADS=${mode}`, '-DCURSIV_WASI_MAX_MEMORY=67108864',
          '-DCURSIV_WASI_STACK=131072', '-DCMAKE_BUILD_TYPE=Release'],
        { encoding: 'utf8', timeout: 30000 });
        assert.equal(configured.status, 0, configured.stderr || String(configured.error));
        const compiled = spawnSync('cmake', ['--build', build, '--parallel', '2'],
          { encoding: 'utf8', timeout: 30000 });
        assert.equal(compiled.status, 0, compiled.stderr || compiled.stdout || String(compiled.error));
        const modulePath = join(build, 'runtime');
        const result = spawnSync(process.execPath, [runner, modulePath], {
          encoding: 'utf8', timeout: 15000, env: { ...process.env, CURSIV_WORKERS: '2' },
        });
        assert.equal(result.status, 0, `${mode}: ${result.stderr || result.error}`);
        assert.equal(result.stdout, 'toolchain-contract\n', mode);
        if (mode === 'ON') assert.match(result.stderr, /toolchain-thread/);
      }
      const fetcher = fileURLToPath(new URL('./fetch-wasi-sdk.cmake', import.meta.url));
      const existing = spawnSync('cmake', [`-DDEST=${sdk}`, '-P', fetcher],
        { encoding: 'utf8', timeout: 5000 });
      assert.equal(existing.status, 0, existing.stderr || String(existing.error));
      assert.match(existing.stdout, /already installed:/);
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });

// A thread whose worker fails before its code runs, here because the runner is
// gone, ends the process with an error instead of a wait.
test('a thread that cannot start ends the process', { skip: !sdk && 'wasi-sdk is not installed' }, async () => {
  const directory = mkdtempSync(join(tmpdir(), 'cursiv-wasi-lost-'));
  try {
    const copy = join(directory, 'wasi-run.mjs');
    copyFileSync(runner, copy);
    const input = join(directory, 'late.c'), output = join(directory, 'late.wasm');
    writeFileSync(input, `#include <pthread.h>
#include <stdio.h>
static void* run(void* unused) { return unused; }
int main(void) {
  puts("started");
  fflush(stdout);
  getchar();
  pthread_t thread;
  if (pthread_create(&thread, NULL, run, NULL)) return 3;
  pthread_join(thread, NULL);
  return 0;
}
`);
    const compiled = spawnSync(join(sdk, 'bin/clang'), ['--target=wasm32-wasip1-threads', '-O0', '-pthread',
      '-Wl,--import-memory', '-Wl,--max-memory=67108864', input, '-o', output],
    { encoding: 'utf8', timeout: 30000 });
    assert.equal(compiled.status, 0, compiled.stderr || String(compiled.error));
    const child = spawn(process.execPath, ['--no-warnings', copy, output]);
    let stdout = '', stderr = '';
    child.stderr.on('data', (data) => { stderr += data; });
    // Once the program runs, the runner goes, then the program starts its thread.
    child.stdout.on('data', (data) => {
      stdout += data;
      if (stdout === 'started\n') {
        rmSync(copy);
        child.stdin.end('\n');
      }
    });
    const code = await new Promise((resolve) => {
      const timer = setTimeout(() => { child.kill(); resolve('still running after 20 s'); }, 20000);
      child.on('exit', (status) => { clearTimeout(timer); resolve(status); });
    });
    assert.equal(code, 1, stderr);
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});

test('application hosts extend imports and environment for commands and nested threads',
  { skip: !sdk && 'wasi-sdk is not installed' }, () => {
    const directory = mkdtempSync(join(tmpdir(), 'cursiv-wasi-extension-'));
    try {
      const host = join(directory, 'host #?% é.mjs');
      writeFileSync(host, `export async function createHost({ memory, args, runner, hostModule }) {
  if (args.length !== 3 || args[1] !== '--host-module' || args[2] !== 'payload')
    throw new Error('Module arguments were consumed by the runner');
  if (!runner.endsWith('wasi-run.mjs') || !hostModule.endsWith('host #?% é.mjs'))
    throw new Error('Host context lost its resolved paths');
  let instantiated = null;
  return { instantiated(instance) { instantiated = instance; },
    env: { GENERIC_HOST_VALUE: 'application' }, imports: { contract: {
    check(pointer, size) {
      if (instantiated?.exports === undefined) return 0;
      const bytes = new Uint8Array(memory().buffer, pointer, size);
      if (new TextDecoder().decode(bytes) !== 'memory from wasm') return 0;
      bytes[0] = 77;
      return 37;
    },
  } } };
}
`);
      const input = join(directory, 'host.c');
      writeFileSync(input, `#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef WITH_THREADS
#include <pthread.h>
#endif
__attribute__((import_module("contract"), import_name("check")))
extern int check(char*, int);
static void* call(void* unused) {
  char value[] = "memory from wasm";
  if (check(value, sizeof(value)-1) != 37 || value[0] != 'M') abort();
  return NULL;
}
int main(int argc, char** argv) {
  if (argc != 3 || strcmp(argv[1], "--host-module") || strcmp(argv[2], "payload")) return 60;
  if (!getenv("GENERIC_HOST_VALUE") || strcmp(getenv("GENERIC_HOST_VALUE"), "application")) return 61;
  if (!getenv("CURSIV_WASI_HOST_MODULE")) return 62;
  if (!getenv("PWD") || !strstr(getenv("PWD"), "cursiv-wasi-extension-")) return 63;
#ifdef WITH_THREADS
  pthread_t thread;
  if (pthread_create(&thread, NULL, call, NULL)) return 64;
  if (pthread_join(thread, NULL)) return 65;
#else
  call(NULL);
#endif
  if (__builtin_wasm_memory_grow(0, 1) == (size_t)-1) return 66;
  call(NULL);
  puts("host-extension-contract");
}
`);
      for (const mode of ['defined', 'imported', 'threads']) {
        const output = join(directory, mode + '.wasm');
        const threaded = mode === 'threads';
        const compiled = spawnSync(join(sdk, 'bin/clang'), [
          `--target=wasm32-wasip1${threaded ? '-threads' : ''}`, '-O0',
          ...(threaded ? ['-pthread', '-DWITH_THREADS'] : []),
          ...(mode !== 'defined' ? ['-Wl,--import-memory'] : []),
          '-Wl,--max-memory=67108864', input, '-o', output,
        ], { encoding: 'utf8', timeout: 30000 });
        assert.equal(compiled.status, 0, compiled.stderr || String(compiled.error));
        for (const configuration of ['flag', 'equals', 'environment']) {
          const options = configuration === 'flag' ? ['--host-module', host]
            : configuration === 'equals' ? ['--host-module=' + host] : [];
          const result = spawnSync(process.execPath, [runner, ...options, output, '--host-module', 'payload'], {
            cwd: directory, encoding: 'utf8', timeout: 15000,
            env: { ...process.env, PWD: '/stale-parent-directory', CURSIV_WORKERS: '2',
              CURSIV_WASI_HOST_MODULE: configuration === 'environment' ? host : '/wrong-env-module.mjs' },
          });
          assert.equal(result.status, 0, `${mode}/${configuration}: ${result.stderr || result.error}`);
          assert.equal(result.stdout, 'host-extension-contract\n');
          assert.doesNotMatch(result.stderr, /unrecognized flag/);
        }
      }
      const invalid = join(directory, 'invalid.mjs');
      writeFileSync(invalid, 'export const other = 1;\n');
      const result = spawnSync(process.execPath, [runner, '--host-module', invalid, join(directory, 'defined.wasm')],
        { encoding: 'utf8', timeout: 15000 });
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /must export createHost/);
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });

