// The client side of the session drivers: the server as a child process, the
// messages it sends back, a wait for one of them, and how the server ended.
// LSP frames each message with a Content-Length header; MCP over stdio sends
// one JSON message per line. A wasm server runs through wasi-run.mjs, as ctest
// runs it.
import { spawn } from 'node:child_process';
import { closeSync, openSync, readSync } from 'node:fs';
import { StringDecoder } from 'node:string_decoder';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(new URL('./wasi-run.mjs', import.meta.url));
const wasmMagic = Buffer.from([0x00, 0x61, 0x73, 0x6d]);

// The LSP base protocol: a header block, a blank line, then exactly
// Content-Length bytes. Anything else on the stream is a server bug, and ends
// the session here rather than hanging it.
function frameReader(received) {
  let buffer = Buffer.alloc(0);
  return (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      const headerEnd = buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) return;
      const headers = buffer.subarray(0, headerEnd).toString('ascii');
      const match = /content-length:\s*(\d+)/i.exec(headers);
      if (!match) {
        console.error(`no Content-Length in header block: ${JSON.stringify(headers)}`);
        process.exit(1);
      }
      const start = headerEnd + 4, end = start + Number(match[1]);
      if (buffer.length < end) return;
      received.push(JSON.parse(buffer.subarray(start, end).toString('utf8')));
      buffer = buffer.subarray(end);
    }
  };
}

// One JSON message per line. A StringDecoder, not chunk.toString: a character
// straddling a pipe-read boundary would decode to U+FFFD in both halves.
function lineReader(received) {
  const decoder = new StringDecoder('utf8');
  let buffer = '';
  return (chunk) => {
    buffer += decoder.write(chunk);
    for (let nl; (nl = buffer.indexOf('\n')) !== -1;) {
      const line = buffer.slice(0, nl);
      buffer = buffer.slice(nl + 1);
      if (line.trim()) received.push(JSON.parse(line));
    }
  };
}

// `lines` speaks one message per line instead of LSP frames.
export function startSession(serverPath, args = [], { lines = false } = {}) {
  // The magic number only: a debug server weighs a hundred megabytes.
  const head = Buffer.alloc(4);
  const fd = openSync(serverPath, 'r');
  try {
    readSync(fd, head, 0, 4, 0);
  } finally {
    closeSync(fd);
  }
  const isWasm = head.equals(wasmMagic);
  const child = isWasm
    ? spawn(process.execPath, ['--no-warnings', runner, serverPath, ...args],
            { stdio: ['pipe', 'pipe', 'inherit'] })
    : spawn(serverPath, args, { stdio: ['pipe', 'pipe', 'inherit'] });
  // Every message received, in order; never cleared.
  const received = [];
  child.stdout.on('data', lines ? lineReader(received) : frameReader(received));
  const ended = () => child.exitCode !== null || child.signalCode !== null;
  // A server gone is reported by the wait for its answer, not by a write
  // into its closed pipe.
  child.stdin.on('error', () => {});

  const send = (message) => {
    const body = JSON.stringify({ jsonrpc: '2.0', ...message });
    child.stdin.write(lines ? `${body}\n`
                            : `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
  };

  // The latest message that matches, among those received from index `since`:
  // a notification sent before the client's own last move answers nothing. A
  // server that ends first fails the wait, and so does one past `timeoutMs`.
  const waitFor = (predicate, what, { since = 0, timeoutMs = 20000 } = {}) =>
    new Promise((resolve, reject) => {
      const deadline = Date.now() + timeoutMs;
      const poll = () => {
        for (let i = received.length - 1; i >= since; --i)
          if (predicate(received[i])) return resolve(received[i]);
        if (ended()) return reject(new Error(`server exited before answering ${what}`));
        if (Date.now() > deadline) return reject(new Error(`timed out waiting for ${what}`));
        setTimeout(poll, 10);
      };
      poll();
    });

  // How the server ended: `code 0` is the one clean way. One still running two
  // seconds later is killed.
  const end = () => {
    const said = () => child.exitCode !== null ? `code ${child.exitCode}` : `signal ${child.signalCode}`;
    if (ended()) return Promise.resolve(said());
    return new Promise((resolve) => {
      const timer = setTimeout(() => { child.kill(); resolve('still running after 2 s'); }, 2000);
      child.once('exit', () => { clearTimeout(timer); resolve(said()); });
    });
  };

  return { child, received, send, waitFor, end };
}
