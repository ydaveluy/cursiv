// The "Try it online" page: a language picker, one Monaco editor on the example
// documents, and the language's WASI server running in Web Workers.
import { startServer } from './server.js';
import { registerLanguageFeatures } from './lsp.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const state = { phase: 'loading', language: null, document: null, diagnostics: {}, startMs: null, error: null };
window.__demo = state;   // read by the headless check
state.session = () => current;

let monaco, editor, languages, current = null;
const registered = new Map();   // Monaco language id -> { refresh }
const viewStates = new Map();

function setStatus(phase, text) {
  state.phase = phase;
  $('server-state').dataset.phase = phase;
  $('server-text').textContent = text;
  $('restart').hidden = phase !== 'stopped';
}

function loadMonaco() {
  return new Promise((resolve) => {
    // An absolute URL: Monaco's editor worker starts from a blob URL, where a
    // relative path would resolve from the site root.
    window.require.config({ paths: { vs: new URL('vendor/monaco/vs', location.href).href } });
    window.require(['vs/editor/editor.main'], () => resolve(window.monaco));
  });
}

// A Material palette colour as opaque hex, painted over the page background:
// Monaco themes take no CSS variables and no transparency.
const probe = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
function paletteColor(name) {
  const style = getComputedStyle(document.body);
  for (const color of ['--md-default-bg-color', name]) {
    probe.fillStyle = style.getPropertyValue(color).trim();
    probe.fillRect(0, 0, 1, 1);
  }
  return [...probe.getImageData(0, 0, 1, 1).data.slice(0, 3)].map((v) => v.toString(16).padStart(2, '0')).join('');
}

// The editor colours of the docs' code blocks, for the active Material
// scheme ("slate" is the dark one). Rules cover the semantic token types of
// Cursiv servers.
function applyTheme() {
  const c = paletteColor;
  const highlight = `#${c('--md-code-bg-color')}`;
  monaco.editor.defineTheme('cursiv', {
    base: document.body.dataset.mdColorScheme === 'slate' ? 'vs-dark' : 'vs', inherit: true,
    rules: [
      { token: 'keyword', foreground: c('--md-code-hl-keyword-color') },
      { token: 'type', foreground: c('--md-code-hl-constant-color') },
      { token: 'variable', foreground: c('--md-code-hl-name-color') },
      { token: 'property', foreground: c('--md-code-hl-function-color') },
      { token: 'number', foreground: c('--md-code-hl-number-color') },
      { token: 'string', foreground: c('--md-code-hl-string-color') },
      { token: 'comment', foreground: c('--md-code-hl-comment-color'), fontStyle: 'italic' },
      { token: 'operator', foreground: c('--md-code-hl-operator-color') },
    ],
    colors: {
      'editor.background': `#${c('--md-default-bg-color')}`,
      'editor.foreground': `#${c('--md-code-fg-color')}`,
      'editorLineNumber.foreground': `#${c('--md-default-fg-color--lighter')}`,
      'editorLineNumber.activeForeground': `#${c('--md-default-fg-color--light')}`,
      'editor.lineHighlightBackground': highlight,
      'editor.lineHighlightBorder': highlight,
    },
  });
  monaco.editor.setTheme('cursiv');
}

// Comments and strings before the first semantic tokens arrive.
function registerMonacoLanguage(id, extensions) {
  monaco.languages.register({ id, extensions });
  monaco.languages.setMonarchTokensProvider(id, {
    tokenizer: {
      root: [
        [/\/\/.*$/, 'comment'],
        [/\/\*/, 'comment', '@comment'],
        [/"([^"\\]|\\.)*"/, 'string'],
        [/'([^'\\]|\\.)*'/, 'string'],
        [/\d+(\.\d+)?/, 'number'],
      ],
      comment: [[/\*\//, 'comment', '@pop'], [/./, 'comment']],
    },
  });
}

function renderPicker() {
  const picker = $('languages');
  picker.replaceChildren(...languages.map((lang) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = lang.label;
    button.dataset.id = lang.id;
    button.setAttribute('role', 'tab');
    button.addEventListener('click', () => openLanguage(lang.id));
    return button;
  }));
}

function renderTabs() {
  const tabs = $('documents');
  tabs.replaceChildren(...current.documents.map((d) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'doc-tab';
    button.dataset.uri = d.uri;
    button.setAttribute('role', 'tab');
    const name = document.createElement('span');
    name.textContent = d.name;
    const badge = document.createElement('span');
    badge.className = 'badge';
    button.append(name, badge);
    button.addEventListener('click', () => showDocument(d.uri));
    return button;
  }));
  updateTabs();
}

function updateTabs() {
  for (const tab of $('documents').children) {
    const diags = state.diagnostics[tab.dataset.uri] ?? [];
    const errors = diags.filter((d) => (d.severity ?? 1) === 1).length;
    const badge = tab.querySelector('.badge');
    badge.textContent = diags.length ? String(diags.length) : '';
    badge.className = 'badge' + (errors ? ' error' : diags.length ? ' warning' : '');
    tab.setAttribute('aria-selected', String(tab.dataset.uri === state.document));
  }
  for (const b of $('languages').children)
    b.setAttribute('aria-selected', String(b.dataset.id === state.language));
}

function updateDiagnosticsStatus() {
  const diags = state.diagnostics[state.document] ?? [];
  const count = (s) => diags.filter((d) => (d.severity ?? 1) === s).length;
  const errors = count(1), warnings = count(2), others = diags.length - errors - warnings;
  $('diag-errors').textContent = String(errors);
  $('diag-warnings').textContent = String(warnings);
  $('diag-info').textContent = others ? `${others} info` : '';
  $('diagnostics').title = diags.map((d) => `${d.range.start.line + 1}:${d.range.start.character + 1} ${d.message}`).join('\n') || 'No problems';
  const first = diags[0];
  $('diag-first').textContent = first ? `${first.range.start.line + 1}:${first.range.start.character + 1}  ${first.message}` : '';
}

function showDocument(uri) {
  const d = current?.documents.find((x) => x.uri === uri);
  if (!d) return;
  if (state.document && editor.getModel()) viewStates.set(state.document, editor.saveViewState());
  editor.setModel(d.model);
  if (viewStates.has(uri)) editor.restoreViewState(viewStates.get(uri));
  state.document = uri;
  $('language-id').textContent = d.languageId;
  params.set('lang', current.lang.id);
  params.set('doc', d.name);
  history.replaceState(null, '', `?${params}`);
  updateTabs();
  updateDiagnosticsStatus();
  editor.focus();
}

function stopCurrent() {
  if (!current) return;
  current.disposed = true;
  current.server?.dispose();
  for (const d of current.documents) d.model.dispose();
  current = null;
  state.diagnostics = {};
}

async function openLanguage(id, wantedDoc) {
  const lang = languages.find((l) => l.id === id) ?? languages[0];
  if (current?.lang.id === lang.id && current.server?.running) return;
  stopCurrent();
  state.language = lang.id;
  state.document = null;
  const session = { lang, documents: [], server: null, capabilities: null, disposed: false };
  current = session;
  updateTabs();

  // Every example document of the language, opened at once, in a folder the
  // server scans: references between them resolve as in a workspace.
  const texts = await Promise.all(lang.documents.map(async (d) => (await fetch(d.path)).text()));
  if (session.disposed) return;
  session.documents = lang.documents.map((d, i) => {
    const uri = `file:///demo/${lang.id}/${d.name}`;
    const model = monaco.editor.createModel(texts[i], d.languageId, monaco.Uri.parse(uri));
    return { ...d, uri, model, version: 1 };
  });
  renderTabs();
  const initial = session.documents.find((d) => d.name === wantedDoc) ?? session.documents[0];
  showDocument(initial.uri);

  setStatus('starting', `Starting the ${lang.label.toLowerCase()} server…`);
  const t0 = performance.now();
  let server;
  try {
    const enc = new TextEncoder();
    server = await startServer(lang.server, {
      env: params.has('workers') ? { CURSIV_WORKERS: params.get('workers') } : {},
      // The example folder, scanned by the server as in an editor workspace.
      root: '/demo',
      files: Object.fromEntries(session.documents.map((d) => [`/demo/${lang.id}/${d.name}`, enc.encode(texts[session.documents.indexOf(d)])])),
      onLog: (text) => console.warn(`[${lang.id}]`, text.trimEnd()),
      onExit: (reason) => {
        if (session.disposed) return;
        setStatus('stopped', `Server stopped: ${reason}`);
        state.error = reason;
      },
    });
  } catch (error) {
    state.error = String(error.message ?? error);
    setStatus('stopped', `The server could not start: ${state.error}`);
    return;
  }
  if (session.disposed) { server.dispose(); return; }
  session.server = server;

  server.on('textDocument/publishDiagnostics', (p) => {
    if (session.disposed) return;
    const d = session.documents.find((x) => x.uri === p.uri);
    if (!d) return;
    state.diagnostics[p.uri] = p.diagnostics;
    monaco.editor.setModelMarkers(d.model, 'cursiv', p.diagnostics.map((diag) => ({
      severity: [, monaco.MarkerSeverity.Error, monaco.MarkerSeverity.Warning,
                 monaco.MarkerSeverity.Info, monaco.MarkerSeverity.Hint][diag.severity ?? 1],
      message: diag.message, source: diag.source, code: diag.code == null ? undefined : String(diag.code),
      startLineNumber: diag.range.start.line + 1, startColumn: diag.range.start.character + 1,
      endLineNumber: diag.range.end.line + 1, endColumn: diag.range.end.character + 1,
    })));
    updateTabs();
    if (p.uri === state.document) updateDiagnosticsStatus();
  });
  server.on('window/showMessage', (p) => console.info(`[${lang.id}]`, p.message));
  server.on('window/logMessage', (p) => console.debug(`[${lang.id}]`, p.message));

  const init = await server.request('initialize', {
    processId: null, rootUri: `file:///demo/${lang.id}`,
    workspaceFolders: [{ uri: `file:///demo/${lang.id}`, name: lang.id }],
    clientInfo: { name: 'cursiv-demo' },
    capabilities: {
      general: { positionEncodings: ['utf-16'] },
      textDocument: {
        synchronization: { didSave: false },
        publishDiagnostics: { relatedInformation: false },
        completion: { completionItem: { snippetSupport: true, documentationFormat: ['markdown', 'plaintext'] } },
        hover: { contentFormat: ['markdown', 'plaintext'] },
        definition: { linkSupport: true },
        references: {}, documentHighlight: {}, formatting: {}, rangeFormatting: {}, foldingRange: {},
        documentSymbol: { hierarchicalDocumentSymbolSupport: true },
        rename: { prepareSupport: true },
        codeAction: { codeActionLiteralSupport: { codeActionKind: { valueSet: ['', 'quickfix', 'refactor', 'source'] } } },
        semanticTokens: { requests: { full: true }, tokenTypes: [], tokenModifiers: [], formats: ['relative'] },
      },
      workspace: { workspaceFolders: true, configuration: false, workspaceEdit: { documentChanges: true } },
    },
  }).catch((error) => { state.error = error.message; return null; });
  if (!init || session.disposed) return;
  session.capabilities = init.capabilities;
  server.notify('initialized', {});

  for (const d of session.documents) {
    server.notify('textDocument/didOpen', { textDocument: { uri: d.uri, languageId: d.languageId, version: d.version, text: d.model.getValue() } });
    d.model.onDidChangeContent(() => {
      if (session.disposed) return;
      server.notify('textDocument/didChange', { textDocument: { uri: d.uri, version: ++d.version },
                                                 contentChanges: [{ text: d.model.getValue() }] });
    });
  }
  // Features are registered once the documents are open, then reused.
  for (const languageId of lang.languages) {
    if (!registered.has(languageId))
      registered.set(languageId, registerLanguageFeatures(monaco, languageId, () => current, (uri) => state.diagnostics[uri]));
    registered.get(languageId).refresh();
  }
  state.startMs = Math.round(performance.now() - t0);
  setStatus('ready', `Server ready · ${(server.size / 1048576).toFixed(1)} MB module · started in ${state.startMs} ms`);
}

async function main() {
  if (!window.require) {
    // A documentation build without tools/build-demo.sh (a preview).
    setStatus('unavailable', 'This build has no editor: tools/build-demo.sh adds it with the language servers.');
    return;
  }
  if (!crossOriginIsolated) {
    // coi-serviceworker reloads the page once it controls it.
    setStatus('starting', window.isSecureContext && 'serviceWorker' in navigator
      ? 'Enabling cross-origin isolation (the page reloads once)…'
      : 'This page needs cross-origin isolation: open it over https or on localhost, outside private browsing.');
    return;
  }
  setStatus('loading', 'Loading the editor…');
  [monaco, languages] = await Promise.all([loadMonaco(), fetch('languages.json').then((r) => r.json())]);
  applyTheme();
  // The palette toggle of the site header switches the editor theme too.
  new MutationObserver(applyTheme).observe(document.body, { attributeFilter: ['data-md-color-scheme'] });
  const extensions = {};
  for (const lang of languages)
    for (const d of lang.documents) (extensions[d.languageId] ??= new Set()).add(d.name.slice(d.name.lastIndexOf('.')));
  for (const [id, exts] of Object.entries(extensions)) registerMonacoLanguage(id, [...exts]);

  editor = monaco.editor.create($('editor'), {
    theme: 'cursiv', automaticLayout: true, minimap: { enabled: false },
    fontSize: 14, lineHeight: 21,
    fontFamily: getComputedStyle(document.body).getPropertyValue('--md-code-font-family').trim() || 'monospace',
    'semanticHighlighting.enabled': true, wordBasedSuggestions: 'off', renderLineHighlight: 'line',
    scrollBeyondLastLine: false, fixedOverflowWidgets: true, padding: { top: 12 }, tabSize: 4,
    quickSuggestions: { other: true, comments: false, strings: false },
  });
  state.editor = editor;
  // The code font of the site loads on first use: measure it again then.
  document.fonts.addEventListener('loadingdone', () => monaco.editor.remeasureFonts());
  editor.onDidChangeCursorPosition((e) => { $('cursor').textContent = `Ln ${e.position.lineNumber}, Col ${e.position.column}`; });
  // Go to definition in another example document switches tabs.
  monaco.editor.registerEditorOpener({
    openCodeEditor(_source, resource, selection) {
      const uri = resource.toString();
      if (!current?.documents.some((d) => d.uri === uri)) return false;
      showDocument(uri);
      if (selection) {
        const range = monaco.Range.isIRange(selection) ? selection : monaco.Range.fromPositions(selection);
        editor.setSelection(range);
        editor.revealRangeInCenter(range);
      }
      return true;
    },
  });
  renderPicker();
  $('restart').addEventListener('click', () => { const doc = current?.documents.find((d) => d.uri === state.document)?.name; const id = state.language; stopCurrent(); openLanguage(id, doc); });
  await openLanguage(params.get('lang') ?? languages[0].id, params.get('doc'));
}

main().catch((error) => {
  state.error = String(error?.stack ?? error);
  setStatus('stopped', `Error: ${error.message ?? error}`);
});
