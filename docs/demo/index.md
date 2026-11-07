---
description: Write documents in the cursiv++ example languages in your browser, with their language servers compiled to WebAssembly.
hide:
  - toc
---

# Try it online

Write documents in the example languages. Each language server is compiled
to WebAssembly and runs in this page: diagnostics, completion, hover, go to
definition, rename and formatting all come from it.

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
    </span>
    <span class="right"><span id="cursor">Ln 1, Col 1</span><span id="language-id"></span></span>
  </div>
</div>
<script type="module" src="app.js"></script>
</section>
