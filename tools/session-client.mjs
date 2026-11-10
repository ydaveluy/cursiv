// The client side of the session drivers: the server as a child process, its
// messages, LSP-framed or one JSON per line, and how it ended. A wasm server
// runs through wasi-run.mjs.
import { spawn } from 'node:child_process';
import { constants } from 'node:buffer';
import { closeSync, openSync, readSync } from 'node:fs';
import { StringDecoder } from 'node:string_decoder';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(new URL('./wasi-run.mjs', import.meta.url));
const wasmMagic = Buffer.from([0x00, 0x61, 0x73, 0x6d]);

// How long a server gets to end on its own, then after SIGTERM.
const graceMs = 2000;

// The body length a header block gives: one decimal Content-Length.
function contentLength(block) {
  let length = null;
  for (const line of block.split('\r\n')) {
    const header = /^([^:\s]+):[ \t]*(.*?)[ \t]*$/.exec(line);
    if (!header) throw new Error(`malformed header line ${JSON.stringify(line)}`);
    if (header[1].toLowerCase() !== 'content-length') continue;
    if (length !== null) throw new Error('two Content-Length headers');
    if (!/^\d+$/.test(header[2])) throw new Error(`bad Content-Length ${JSON.stringify(header[2])}`);
    length = Number(header[2]);
    if (!Number.isSafeInteger(length) || length > constants.MAX_LENGTH)
      throw new Error(`Content-Length ${header[2]} is past what a buffer holds`);
  }
  if (length === null) throw new Error(`no Content-Length in header block ${JSON.stringify(block)}`);
  return length;
}

function parsed(text) {
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`a message that is not JSON: ${error.message}`);
  }
}

// The LSP base protocol: headers, a blank line, then Content-Length bytes,
// whose chunks are joined once the last one comes. `finish` names what the
// stream ended inside, if anything.
function frameReader(deliver) {
  let chunks = [];
  let size = 0;
  let start = -1, end = -1;   // the body's place, once its header is read
  const joined = () => (chunks.length === 1 ? chunks[0] : Buffer.concat(chunks, size));
  return {
    feed(chunk) {
      chunks.push(chunk);
      size += chunk.length;
      for (;;) {
        if (end < 0) {
          const buffer = joined();
          chunks = [buffer];
          const headerEnd = buffer.indexOf('\r\n\r\n');
          if (headerEnd < 0) return;
          start = headerEnd + 4;
          end = start + contentLength(buffer.subarray(0, headerEnd).toString('ascii'));
        }
        if (size < end) return;
        const buffer = joined();
        deliver(parsed(buffer.subarray(start, end).toString('utf8')));
        chunks = [buffer.subarray(end)];
        size -= end;
        start = end = -1;
      }
    },
    finish: () => size ? `the output ended inside a message (${size} bytes left)` : null,
  };
}

// One JSON message per line. A StringDecoder, not chunk.toString: a character
// straddling a pipe-read boundary would decode to U+FFFD in both halves.
function lineReader(deliver) {
  const decoder = new StringDecoder('utf8');
  let buffer = '';
  return {
    feed(chunk) {
      buffer += decoder.write(chunk);
      for (let nl; (nl = buffer.indexOf('\n')) !== -1;) {
        const line = buffer.slice(0, nl);
        buffer = buffer.slice(nl + 1);
        if (line.trim()) deliver(parsed(line));
      }
    },
    finish() {
      buffer += decoder.end();
      return buffer.trim() ? `the output ended inside a line: ${JSON.stringify(buffer)}` : null;
    },
  };
}

// `lines` speaks one message per line instead of LSP frames.
export function startSession(serverPath, args = [], { lines = false } = {}) {
  // Only the magic number is read: a debug server is a hundred megabytes.
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

  // The first session failure, such as a server that cannot start, stops
  // reading or breaks the protocol: every wait fails on it, and so does `end`.
  let fault = null;
  const fail = (what) => { fault ??= what; };
  child.on('error', (error) => fail(`the server failed: ${error.message}`));
  child.stdin.on('error', (error) => fail(`writing to the server failed: ${error.message}`));

  // Reading stops at the first fault.
  const reader = (lines ? lineReader : frameReader)((message) => received.push(message));
  child.stdout.on('data', (chunk) => {
    if (fault !== null) return;
    try {
      reader.feed(chunk);
    } catch (error) {
      fail(error.message);
    }
  });
  child.stdout.on('end', () => {
    if (fault === null) {
      const left = reader.finish();
      if (left !== null) fail(left);
    }
  });

  // The process is gone and its output read to the end, or it never started.
  let closed = false;
  const whenClosed = new Promise((resolve) => child.once('close', () => { closed = true; resolve(); }));

  const send = (message) => {
    const body = JSON.stringify({ jsonrpc: '2.0', ...message });
    child.stdin.write(lines ? `${body}\n`
                            : `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
  };

  // The latest matching message received from index `since` on. The wait fails
  // when the server ends, on a session fault, or after `timeoutMs`.
  const waitFor = (predicate, what, { since = 0, timeoutMs = 20000 } = {}) =>
    new Promise((resolve, reject) => {
      const deadline = Date.now() + timeoutMs;
      const poll = () => {
        try {
          for (let i = received.length - 1; i >= since; --i)
            if (predicate(received[i])) return resolve(received[i]);
        } catch (error) {
          return reject(error);
        }
        if (closed) return reject(new Error(`server exited before answering ${what}`));
        if (fault !== null) return reject(new Error(`${fault}, before answering ${what}`));
        if (Date.now() > deadline) return reject(new Error(`timed out waiting for ${what}`));
        setTimeout(poll, 10);
      };
      poll();
    });

  // Sends a request under a fresh id, which replaces any id in `message`, and
  // returns its response, failing on an error response.
  let lastId = 0;
  const request = async (message, what, options) => {
    const id = ++lastId;
    send({ ...message, id });
    const answer = await waitFor((m) => m.id === id && !('method' in m), what, options);
    if (answer.error) throw new Error(`${what}: ${answer.error.code}: ${answer.error.message}`);
    return answer;
  };

  // How the server ended, `code 0` when clean. One still running after
  // `graceMs` gets SIGTERM, then SIGKILL; `end` resolves once it is gone.
  const end = async () => {
    const closes = () =>
      Promise.race([whenClosed.then(() => true), delay(graceMs, false, { ref: false })]);
    let how;
    if (await closes()) {
      how = child.exitCode !== null ? `code ${child.exitCode}` : `signal ${child.signalCode}`;
    } else {
      child.kill('SIGTERM');
      if (!await closes()) {
        child.kill('SIGKILL');
        await whenClosed;
      }
      how = `still running after ${graceMs / 1000} s, ended by ${child.signalCode ?? `code ${child.exitCode}`}`;
    }
    return fault === null ? how : `${how}; ${fault}`;
  };

  return { child, received, send, waitFor, request, end };
}
