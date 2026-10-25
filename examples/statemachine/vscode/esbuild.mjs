// The extension is bundled into ONE CommonJS file: `vscode` is injected by the
// editor and stays external, the language client is bundled in, so the folder
// VS Code loads needs no node_modules of its own.
import { rmSync } from 'node:fs';
import esbuild from 'esbuild';

const production = process.argv.includes('--production');
const watch = process.argv.includes('--watch');

if (!watch) {
  rmSync('out', { recursive: true, force: true });
}

const context = await esbuild.context({
  entryPoints: ['src/extension.ts'],
  bundle: true,
  format: 'cjs',
  platform: 'node',
  target: 'node18', // VS Code 1.86 supports native servers; WASI needs 1.101.
  outfile: 'out/extension.js',
  external: ['vscode'],
  sourcemap: !production,
  minify: production,
  logLevel: 'info'
});

if (watch) {
  await context.watch();
} else {
  await context.rebuild();
  await context.dispose();
}
