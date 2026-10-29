// Monaco features backed by the language server of the active session. The
// providers are registered once per Monaco language; each call asks
// `session()` for the server currently running, if any.

const toRange = (monaco, r) =>
  new monaco.Range(r.start.line + 1, r.start.character + 1, r.end.line + 1, r.end.character + 1);
const fromPosition = (p) => ({ line: p.lineNumber - 1, character: p.column - 1 });
const fromRange = (r) => ({
  start: { line: r.startLineNumber - 1, character: r.startColumn - 1 },
  end: { line: r.endLineNumber - 1, character: r.endColumn - 1 },
});
const doc = (model) => ({ textDocument: { uri: model.uri.toString() } });

const markdown = (contents) => {
  if (contents == null) return [];
  const one = (c) => typeof c === 'string' ? { value: c }
    : c.kind ? { value: c.value }                               // MarkupContent
    : { value: '```' + (c.language ?? '') + '\n' + c.value + '\n```' };  // MarkedString
  return Array.isArray(contents) ? contents.map(one) : [one(contents)];
};

// LSP kinds are 1-based enums with the same names as Monaco's.
const LSP_COMPLETION_KINDS = [, 'Text', 'Method', 'Function', 'Constructor', 'Field', 'Variable', 'Class',
  'Interface', 'Module', 'Property', 'Unit', 'Value', 'Enum', 'Keyword', 'Snippet', 'Color', 'File',
  'Reference', 'Folder', 'EnumMember', 'Constant', 'Struct', 'Event', 'Operator', 'TypeParameter'];

export function workspaceEdit(monaco, edit) {
  const edits = [];
  const push = (uri, textEdits) => {
    for (const e of textEdits)
      edits.push({ resource: monaco.Uri.parse(uri), versionId: undefined,
                   textEdit: { range: toRange(monaco, e.range), text: e.newText } });
  };
  for (const [uri, textEdits] of Object.entries(edit?.changes ?? {})) push(uri, textEdits);
  for (const change of edit?.documentChanges ?? [])
    if (change.textDocument) push(change.textDocument.uri, change.edits);
  return { edits };
}

const locations = (monaco, result) => {
  if (!result) return [];
  return (Array.isArray(result) ? result : [result]).map((l) => l.targetUri
    ? { uri: monaco.Uri.parse(l.targetUri), range: toRange(monaco, l.targetSelectionRange ?? l.targetRange) }
    : { uri: monaco.Uri.parse(l.uri), range: toRange(monaco, l.range) });
};

export function registerLanguageFeatures(monaco, languageId, session, diagnosticsOf) {
  const L = monaco.languages;
  // Calls the active server; any failure means "no answer", as in an editor.
  const call = async (method, params, token) => {
    const s = session();
    if (!s?.server?.running) return null;
    try {
      const result = await s.server.request(method, params, token);
      return token?.isCancellationRequested ? null : result;
    } catch {
      return null;
    }
  };
  const caps = () => session()?.capabilities ?? {};

  L.registerCompletionItemProvider(languageId, {
    triggerCharacters: caps().completionProvider?.triggerCharacters,
    async provideCompletionItems(model, position, _context, token) {
      const result = await call('textDocument/completion', { ...doc(model), position: fromPosition(position) }, token);
      const items = Array.isArray(result) ? result : result?.items ?? [];
      const word = model.getWordUntilPosition(position);
      const fallback = new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn);
      return {
        incomplete: !Array.isArray(result) && !!result?.isIncomplete,
        suggestions: items.map((item) => {
          const edit = item.textEdit;
          const range = !edit ? fallback
            : edit.range ? toRange(monaco, edit.range)
            : { insert: toRange(monaco, edit.insert), replace: toRange(monaco, edit.replace) };
          return {
            label: item.label,
            kind: L.CompletionItemKind[LSP_COMPLETION_KINDS[item.kind] ?? 'Text'],
            detail: item.detail,
            documentation: item.documentation && markdown(item.documentation)[0],
            insertText: edit?.newText ?? item.insertText ?? item.label,
            insertTextRules: item.insertTextFormat === 2 ? L.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
            sortText: item.sortText, filterText: item.filterText, range,
          };
        }),
      };
    },
  });

  L.registerHoverProvider(languageId, {
    async provideHover(model, position, token) {
      const hover = await call('textDocument/hover', { ...doc(model), position: fromPosition(position) }, token);
      if (!hover) return null;
      return { contents: markdown(hover.contents), range: hover.range && toRange(monaco, hover.range) };
    },
  });

  L.registerDefinitionProvider(languageId, {
    async provideDefinition(model, position, token) {
      return locations(monaco, await call('textDocument/definition', { ...doc(model), position: fromPosition(position) }, token));
    },
  });

  L.registerReferenceProvider(languageId, {
    async provideReferences(model, position, context, token) {
      return locations(monaco, await call('textDocument/references',
        { ...doc(model), position: fromPosition(position), context: { includeDeclaration: context.includeDeclaration } }, token));
    },
  });

  L.registerDocumentHighlightProvider(languageId, {
    async provideDocumentHighlights(model, position, token) {
      const result = await call('textDocument/documentHighlight', { ...doc(model), position: fromPosition(position) }, token);
      return (result ?? []).map((h) => ({ range: toRange(monaco, h.range), kind: (h.kind ?? 1) - 1 }));
    },
  });

  L.registerRenameProvider(languageId, {
    async resolveRenameLocation(model, position, token) {
      const result = await call('textDocument/prepareRename', { ...doc(model), position: fromPosition(position) }, token);
      if (!result) return { range: undefined, text: '', rejectReason: 'This element cannot be renamed.' };
      if (result.start) return { range: toRange(monaco, result), text: model.getValueInRange(toRange(monaco, result)) };
      if (result.range) return { range: toRange(monaco, result.range), text: result.placeholder };
      const word = model.getWordAtPosition(position);
      return word && { range: new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn), text: word.word };
    },
    async provideRenameEdits(model, position, newName, token) {
      const edit = await call('textDocument/rename', { ...doc(model), position: fromPosition(position), newName }, token);
      return edit ? workspaceEdit(monaco, edit) : { edits: [], rejectReason: 'The server did not rename this element.' };
    },
  });

  const formatting = (method) => async (model, rangeOrOptions, optionsOrToken, maybeToken) => {
    const isRange = method === 'textDocument/rangeFormatting';
    const options = isRange ? optionsOrToken : rangeOrOptions;
    const token = isRange ? maybeToken : optionsOrToken;
    const params = { ...doc(model), options: { tabSize: options.tabSize, insertSpaces: options.insertSpaces } };
    if (isRange) params.range = fromRange(rangeOrOptions);
    const edits = await call(method, params, token);
    return (edits ?? []).map((e) => ({ range: toRange(monaco, e.range), text: e.newText }));
  };
  L.registerDocumentFormattingEditProvider(languageId, {
    provideDocumentFormattingEdits: formatting('textDocument/formatting'),
  });
  L.registerDocumentRangeFormattingEditProvider(languageId, {
    provideDocumentRangeFormattingEdits: formatting('textDocument/rangeFormatting'),
  });

  L.registerCodeActionProvider(languageId, {
    async provideCodeActions(model, range, _context, token) {
      const uri = model.uri.toString();
      const lsp = fromRange(range);
      const before = (a, b) => a.line < b.line || (a.line === b.line && a.character <= b.character);
      const diagnostics = (diagnosticsOf(uri) ?? []).filter((d) => before(d.range.start, lsp.end) && before(lsp.start, d.range.end));
      const result = await call('textDocument/codeAction', { ...doc(model), range: lsp, context: { diagnostics } }, token);
      const actions = (result ?? []).filter((a) => a.edit).map((a) => ({
        title: a.title, kind: a.kind, isPreferred: a.isPreferred,
        edit: workspaceEdit(monaco, a.edit),
      }));
      return { actions, dispose() {} };
    },
  });

  L.registerFoldingRangeProvider(languageId, {
    async provideFoldingRanges(model, _context, token) {
      const result = await call('textDocument/foldingRange', doc(model), token);
      return (result ?? []).map((f) => ({ start: f.startLine + 1, end: f.endLine + 1 }));
    },
  });

  L.registerDocumentSymbolProvider(languageId, {
    async provideDocumentSymbols(model, token) {
      const result = await call('textDocument/documentSymbol', doc(model), token);
      const convert = (s) => s.location
        ? { name: s.name, detail: '', kind: s.kind - 1, tags: [], range: toRange(monaco, s.location.range),
            selectionRange: toRange(monaco, s.location.range) }
        : { name: s.name, detail: s.detail ?? '', kind: s.kind - 1, tags: [], range: toRange(monaco, s.range),
            selectionRange: toRange(monaco, s.selectionRange), children: (s.children ?? []).map(convert) };
      return (result ?? []).map(convert);
    },
  });

  // Fired when a server becomes ready: tokens asked before it are stale.
  const tokensChanged = new monaco.Emitter();
  L.registerDocumentSemanticTokensProvider(languageId, {
    onDidChange: tokensChanged.event,
    getLegend: () => caps().semanticTokensProvider?.legend ?? { tokenTypes: [], tokenModifiers: [] },
    async provideDocumentSemanticTokens(model, _lastResultId, token) {
      const result = await call('textDocument/semanticTokens/full', doc(model), token);
      return result ? { data: new Uint32Array(result.data), resultId: result.resultId } : null;
    },
    releaseDocumentSemanticTokens() {},
  });
  return { refresh: () => tokensChanged.fire() };
}
