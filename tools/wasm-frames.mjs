#!/usr/bin/env node
// Fails when a function of a wasm module reserves a stack frame past the limit,
// or when no frame of a module can be read off its disassembly: the wasm stack
// never grows.
// Usage: node tools/wasm-frames.mjs <llvm-objdump> <limit-bytes> <module>...
// Exits 2 on a bad command line, 77 (skipped) when no disassembler was found.

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createInterface } from 'node:readline';
import * as path from 'node:path';

const [objdump, limitArg, ...modules] = process.argv.slice(2);
const limit = Number(limitArg);
if (!/^[1-9]\d*$/.test(limitArg ?? '') || !Number.isSafeInteger(limit) || modules.length === 0) {
    console.error('usage: wasm-frames.mjs <llvm-objdump> <limit-bytes> <module>...\n' +
        '  <limit-bytes> is a positive integer, and at least one module is checked');
    process.exit(2);
}
if (!objdump || objdump.endsWith('-NOTFOUND') || !existsSync(objdump)) {
    console.log('wasm-frames: no llvm-objdump found; the stack-frame lane is skipped');
    process.exit(77);
}

// The prologue that moves the stack pointer down by a constant: a global read,
// lowered, and the value followed through the locals that hold it until it is
// written back to that global.
async function frames(module) {
    const dis = spawn(objdump, ['-d', module], { stdio: ['ignore', 'pipe', 'inherit'] });
    const lines = createInterface({ input: dis.stdout });
    const found = [];
    const recent = [];
    let fn = '';
    let flow = null;
    for await (const line of lines) {
        const head = /^([0-9a-f]+) <(.*)>:/.exec(line);
        if (head) {
            fn = head[2] || `0x${head[1]}`;
            recent.length = 0;
            flow = null;
            continue;
        }
        const op = /\t([a-z0-9_.]+)(?:\t(\d+))?\s*$/.exec(line);
        if (!op) continue;
        const [, name, arg] = op;
        if (flow) {
            if (name === 'global.set' && arg === flow.global && flow.onStack) {
                found.push({ fn, bytes: flow.bytes });
                flow = null;
            } else if (name === 'local.tee' && flow.onStack) {
                flow.locals.add(arg);
            } else if (name === 'local.set' && flow.onStack) {
                flow.locals.add(arg);
                flow.onStack = false;
            } else if (name === 'local.get' && !flow.onStack && flow.locals.has(arg)) {
                flow.onStack = true;
            } else {
                flow = null;
            }
            if (flow) continue;
            recent.length = 0;
        }
        recent.push({ name, arg });
        if (recent.length > 3) recent.shift();
        const [get, size, sub] = recent;
        if (recent.length === 3 && get.name === 'global.get' &&
            size.name === 'i32.const' && sub.name === 'i32.sub')
            flow = { global: get.arg, bytes: Number(size.arg), onStack: true, locals: new Set() };
    }
    const code = await new Promise(resolve => dis.on('close', resolve));
    if (code !== 0) throw new Error(`${objdump} -d ${module} exited ${code}`);
    return found;
}

let over = 0;
for (const module of modules) {
    const found = await frames(module);
    const largest = found.reduce((a, b) => (b.bytes > a.bytes ? b : a), { fn: '-', bytes: 0 });
    const past = found.filter(f => f.bytes > limit);
    over += past.length;
    console.log(`${path.basename(module)}: ${found.length} frames, largest ${largest.bytes} bytes` +
        ` (${largest.fn}), limit ${limit}`);
    for (const f of past) console.log(`  FAIL ${f.fn}: ${f.bytes} bytes`);
    // A server reserves frames: reading none is a prologue this check misses.
    if (found.length === 0) {
        ++over;
        console.log('  FAIL no frame read: a prologue of a form this check does not follow');
    }
}
process.exit(over === 0 ? 0 : 1);
