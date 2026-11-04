import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir, homedir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { importedMemoryLimits } from './wasi-run.mjs';

const runner = process.env.CURSIV_TEST_WASI_RUNNER ?? fileURLToPath(new URL('./wasi-run.mjs', import.meta.url));
const argument = process.argv.indexOf('--sdk');
const sdk = argument >= 0 ? process.argv[argument + 1]
  : [process.env.WASI_SDK, join(homedir(), 'wasi-sdk-34.0'), '/opt/wasi-sdk']
      .find(path => path && existsSync(join(path, 'bin/clang')));
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
      const fetcher = fileURLToPath(new URL('./fetch-wasi-sdk.sh', import.meta.url));
      const existing = spawnSync('bash', [fetcher, sdk], { encoding: 'utf8', timeout: 5000 });
      assert.equal(existing.status, 0, existing.stderr || String(existing.error));
      assert.match(existing.stdout, /already installed:/);
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
  return { env: { GENERIC_HOST_VALUE: 'application' }, imports: { contract: {
    check(pointer, size) {
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

