#!/usr/bin/env node
// The stack frame each function of a wasm module reserves, read off the
// disassembly: a frame past the limit fails the run. The wasm stack is a
// fixed reservation, so a frame too large traps instead of growing it.
//
// Usage: node tools/wasm-frames.mjs <llvm-objdump> <limit-bytes> <module>...
// Exits 77 (skipped) when no disassembler was found.

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createInterface } from 'node:readline';
import * as path from 'node:path';

const [objdump, limitArg, ...modules] = process.argv.slice(2);
if (!objdump || objdump.endsWith('-NOTFOUND') || !existsSync(objdump)) {
    console.log('wasm-frames: no llvm-objdump found; the stack-frame lane is skipped');
    process.exit(77);
}
const limit = Number(limitArg);

// The prologue that moves the stack pointer down by a constant: the same
// global read, lowered, written back.
async function frames(module) {
    const dis = spawn(objdump, ['-d', module], { stdio: ['ignore', 'pipe', 'inherit'] });
    const lines = createInterface({ input: dis.stdout });
    const found = [];
    const window = [];
    let fn = '';
    for await (const line of lines) {
        const head = /^([0-9a-f]+) <(.*)>:/.exec(line);
        if (head) {
            fn = head[2] || `0x${head[1]}`;
            window.length = 0;
            continue;
        }
        window.push(line);
        if (window.length > 5) window.shift();
        if (window.length < 5) continue;
        const get = /\tglobal\.get\t(\d+)\s*$/.exec(window[0]);
        const size = /\ti32\.const\t(\d+)\s*$/.exec(window[1]);
        if (!get || !size || !/\ti32\.sub\s*$/.test(window[2])) continue;
        const set = window.slice(3).some(l => l.endsWith(`\tglobal.set\t${get[1]}`));
        if (set) found.push({ fn, bytes: Number(size[1]) });
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
}
process.exit(over === 0 ? 0 : 1);
