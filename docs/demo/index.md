---
description: Edit Cursiv examples in your browser, fix errors and follow references between files.
hide:
  - toc
---

# Try it online

Choose a language and edit its example files. You can switch between files
in the same language and keep your edits. Switching languages, restarting
the server or reloading this page restores the original files.

<section class="cursiv-demo" data-search-exclude>
<!-- The language servers need SharedArrayBuffer, hence cross-origin
     isolation. GitHub Pages cannot send COOP/COEP headers: this service
     worker adds them and reloads the page once. It sits at the site root
     and serves every page: the site search runs in a worker outside this
     directory, which needs the headers too. A worker that an earlier
     version of the site registered for this directory is removed first. -->
<script>
  window.coi = {
    shouldDeregister: () => navigator.serviceWorker.controller.scriptURL !== document.currentScript.src,
  };
</script>
<script src="../coi-serviceworker.js"></script>
<link rel="stylesheet" href="style.css">
<link rel="stylesheet" href="vendor/monaco/monaco.css">
<nav id="languages" class="segmented" role="tablist" aria-label="Example language"></nav>
<div class="workspace">
  <div id="documents" class="doc-tabs" role="tablist" aria-label="Example documents"></div>
  <div id="editor" class="editor"></div>
  <div class="statusbar" aria-live="polite">
    <span id="server-state" class="server" data-phase="loading"><span class="dot"></span><span id="server-text">Loading…</span>
      <button id="restart" type="button" hidden>Restart</button></span>
    <span id="diagnostics" class="diags">
      <span class="err" title="Errors">● <b id="diag-errors">0</b></span>
      <span class="warn" title="Warnings">▲ <b id="diag-warnings">0</b></span>
      <span id="diag-info" class="info"></span>
      <span id="diag-first" class="first"></span>
      <span id="server-message" class="message"></span>
    </span>
    <span class="right"><span id="build-time" title="From the last edit to its diagnostics"></span><span id="cursor">Ln 1, Col 1</span><span id="language-id"></span></span>
  </div>
</div>
<script type="module" src="app.js"></script>
</section>

## Try an editor feature

- **Completion.** In **State machine**, open `trafficlight.statemachine`.
  Delete `PowerOff` from the `initialState` line, leave the space after the
  keyword and press **Ctrl+Space** to see the available states.
- **Diagnostics and formatting.** In **Arithmetics**, open `broken.calc`
  and add the missing `;` after `x * 2`. The error count drops to zero.
  Right-click in the editor and choose **Format Document** to format the file.
- **Cross-file references.** In **Requirements**, open `tests.tst` and hover
  over `ReqId001_tstID` to read its description. Right-click the name and
  choose **Go to Definition** to open its declaration in `requirements.req`,
  or **Rename Symbol** to rename it in both files.
