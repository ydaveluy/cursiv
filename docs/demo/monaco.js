// The page's editor: Monaco's core API and the features the page can start,
// without Monaco's languages, bundled into vendor/monaco/ by
// tools/build-demo.sh.
export * from 'monaco-editor/editor';
import 'monaco-editor/features/anchorSelect/register';
import 'monaco-editor/features/bracketMatching/register';
import 'monaco-editor/features/caretOperations/register';
import 'monaco-editor/features/clipboard/register';
import 'monaco-editor/features/codeAction/register';
import 'monaco-editor/features/codicon/register';
import 'monaco-editor/features/contextmenu/register';
import 'monaco-editor/features/cursorUndo/register';
import 'monaco-editor/features/dnd/register';
import 'monaco-editor/features/documentSymbols/register';
import 'monaco-editor/features/dropOrPasteInto/register';
import 'monaco-editor/features/find/register';
import 'monaco-editor/features/folding/register';
import 'monaco-editor/features/fontZoom/register';
import 'monaco-editor/features/format/register';
import 'monaco-editor/features/gotoError/register';
import 'monaco-editor/features/gotoLine/register';
import 'monaco-editor/features/gotoSymbol/register';
import 'monaco-editor/features/hover/register';
import 'monaco-editor/features/indentation/register';
import 'monaco-editor/features/inlineProgress/register';
import 'monaco-editor/features/inPlaceReplace/register';
import 'monaco-editor/features/insertFinalNewLine/register';
import 'monaco-editor/features/inspectTokens/register';
import 'monaco-editor/features/iPadShowKeyboard/register';
import 'monaco-editor/features/lineSelection/register';
import 'monaco-editor/features/linesOperations/register';
import 'monaco-editor/features/links/register';
import 'monaco-editor/features/longLinesHelper/register';
import 'monaco-editor/features/multicursor/register';
import 'monaco-editor/features/quickCommand/register';
import 'monaco-editor/features/quickHelp/register';
import 'monaco-editor/features/quickOutline/register';
import 'monaco-editor/features/referenceSearch/register';
import 'monaco-editor/features/rename/register';
import 'monaco-editor/features/semanticTokens/register';
import 'monaco-editor/features/smartSelect/register';
import 'monaco-editor/features/snippet/register';
import 'monaco-editor/features/stickyScroll/register';
import 'monaco-editor/features/toggleHighContrast/register';
import 'monaco-editor/features/toggleTabFocusMode/register';
import 'monaco-editor/features/tokenization/register';
import 'monaco-editor/features/unicodeHighlighter/register';
import 'monaco-editor/features/unusualLineTerminators/register';
import 'monaco-editor/features/wordHighlighter/register';
import 'monaco-editor/features/wordOperations/register';
import 'monaco-editor/features/wordPartOperations/register';
// Parts of features that no `features/*/register` entry of Monaco 0.57 loads:
// the completion widget, whole-document semantic tokens, caret moves and
// paste handling.
import 'monaco-editor/editor/contrib/suggest/browser/suggestController';
import 'monaco-editor/editor/contrib/semanticTokens/browser/documentSemanticTokens';
import 'monaco-editor/editor/contrib/caretOperations/browser/caretOperations';
import 'monaco-editor/editor/contrib/dropOrPasteInto/browser/copyPasteContribution';

// The editor worker, bundled beside this module: same origin, as the page's
// cross-origin isolation requires.
self.MonacoEnvironment = {
  getWorker: (_id, label) => new Worker(new URL('editor.worker.js', import.meta.url), { type: 'module', name: label }),
};
