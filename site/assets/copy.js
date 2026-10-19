/* Copy buttons for code blocks (<div class="code"><pre><code>…</code></pre><button class="copy" hidden>).
   The buttons stay hidden without JavaScript. Without the Clipboard API (or if it refuses), the command is
   selected instead, so Ctrl+C copies it. One shared status element announces the result. */
(function () {
  var status = document.getElementById("copy-status");

  function say(text) {
    if (!status) return;
    status.textContent = "";
    setTimeout(function () { status.textContent = text; }, 50); // a change, so it is announced again
  }

  function select(code) {
    var range = document.createRange();
    range.selectNodeContents(code);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }

  document.querySelectorAll(".code").forEach(function (block) {
    var code = block.querySelector("code");
    var button = block.querySelector(".copy");
    if (!code || !button) return;
    var timer;

    function show(label) {
      button.textContent = label;
      clearTimeout(timer);
      timer = setTimeout(function () { button.textContent = "Copy"; }, 2000);
    }

    function fallback() {
      select(code);
      show("Press Ctrl+C");
      say("Command selected. Press Ctrl+C to copy it.");
    }

    button.addEventListener("click", function () {
      var text = code.textContent;
      if (!navigator.clipboard || !navigator.clipboard.writeText) { fallback(); return; }
      navigator.clipboard.writeText(text).then(function () {
        show("Copied");
        say("Command copied.");
      }, fallback);
    });
    button.hidden = false;
  });
})();
