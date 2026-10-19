// Landing hero: a simulated taskbar readout, its context menu and the usage report.
// Exposes nothing global. The numbers are made up in the browser; nothing is measured or sent.
(() => {
  "use strict";
  const $ = (id) => document.getElementById(id);

  // ---- Number formatting: the rules of src/core/units.cpp ----

  // 1000-based; 0 decimals for the base unit, else 2 below 10, 1 below 100, 0 above; re-rounds when
  // rounding crosses a unit boundary (999.6 KB/s -> 1.00 MB/s) or 10/100 (9.996 -> 10.0).
  function scale(v, units, sep = " ") {
    if (!(v > 0) || !Number.isFinite(v)) v = 0;
    let i = 0;
    while (v >= 1000 && i < units.length - 1) { v /= 1000; i++; }
    const decimalsFor = (i, x) => (i === 0 ? 0 : x < 10 ? 2 : x < 100 ? 1 : 0);
    const roundTo = (x, d) => { const p = 10 ** d; return Math.round(x * p) / p; };
    let d = decimalsFor(i, v);
    let r = roundTo(v, d);
    if (r >= 1000 && i < units.length - 1) {
      v = r / 1000; i++;
      d = decimalsFor(i, v); r = roundTo(v, d);
    } else if (decimalsFor(i, r) < d) {
      d = decimalsFor(i, r); r = roundTo(v, d);
    }
    return r.toFixed(d) + sep + units[i];
  }
  const fmt = (bytesPerSec) => scale(bytesPerSec, ["B/s", "KB/s", "MB/s", "GB/s"]); // FormatSpeed, bytes
  const fmtBits = (bytesPerSec) => scale(bytesPerSec * 8, ["bps", "Kbps", "Mbps", "Gbps"]); // FormatSpeed, bits
  const volume = (bytes) => scale(bytes, ["B", "KB", "MB", "GB", "TB"]); // FormatVolume

  // ---- Report replica (reusable): fills every [data-report-example] with the Summary tab ----

  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

  // Example totals in bytes for the current month. Plausible for one home PC; not real data.
  const EXAMPLE = {
    today: { rx: 2.61e9, tx: 0.254e9 },
    week: { rx: 13.1e9, tx: 1.12e9 },
    month: { rx: 45.02e9, tx: 3.71e9 },
    busiest: { daysAgo: 3, rx: 6.21e9, tx: 0.63e9 },
    adapters: [["Wi-Fi", 42.3e9], ["Ethernet", 6.4e9], ["vEthernet (WSL)", 1.18e9]],
    networks: [["HomeNet-5G", 38.9e9], ["Office", 2.95e9], ["Library guest", 0.412e9]],
  };

  function renderReport(root) {
    const now = new Date();
    const date = (d) => d.toLocaleDateString();
    const monthFirst = new Date(now.getFullYear(), now.getMonth(), 1);
    const monthLast = new Date(now.getFullYear(), now.getMonth() + 1, 0);
    const busiest = new Date(now.getFullYear(), now.getMonth(), Math.max(1, now.getDate() - EXAMPLE.busiest.daysAgo));
    const longDate = busiest.toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" });

    // IDS_RX_TX "↓ %s · ↑ %s", with a spoken version.
    const detail = (t) =>
      `<span class="r-muted" aria-hidden="true">↓ ${volume(t.rx)} · ↑ ${volume(t.tx)}</span>` +
      `<span class="vh">Received ${volume(t.rx)}, sent ${volume(t.tx)}</span>`;
    const card = (title, t, on) =>
      `<div class="r-card${on ? " is-on" : ""}"><span class="r-muted">${esc(title)}</span>` +
      `<span class="r-value">${volume(t.rx + t.tx)}</span>${detail(t)}</div>`;
    const row = (label, value, muted) =>
      `<div class="r-row"><span${muted ? ' class="r-muted"' : ""}>${label}</span><span>${value}</span></div>`;
    const panel = (title, rows) => `<div class="r-panel"><p class="r-panel-title">${esc(title)}</p>${rows.join("")}</div>`;
    const b = EXAMPLE.busiest;

    root.innerHTML =
      `<p class="vh">Example data.</p>` +
      `<div class="r-bar"><span>Period:</span><span class="r-combo">This month</span>` +
      `<span class="r-range">${esc(date(monthFirst))} – ${esc(date(monthLast))}</span>` +
      `<span class="r-btns" aria-hidden="true"><span class="r-btn">Export CSV…</span><span class="r-btn">Clear all history…</span></span></div>` +
      `<div class="r-tabs" aria-hidden="true"><span class="is-on">Summary</span><span>Timeline</span><span>Adapters</span><span>Wi-Fi networks</span></div>` +
      `<div class="r-page">` +
      `<div class="r-cards">${card("Today", EXAMPLE.today)}${card("This week", EXAMPLE.week)}` +
      `${card("This month", EXAMPLE.month)}${card("This month", EXAMPLE.month, true)}</div>` +
      `<div class="r-panels">` +
      panel("Busiest day", [row(esc(longDate), volume(b.rx + b.tx)), row(detail(b), "", true)]) +
      panel("Top adapters", EXAMPLE.adapters.map(([n, v]) => row(esc(n), volume(v)))) +
      panel("Top Wi-Fi networks", EXAMPLE.networks.map(([n, v]) => row(esc(n), volume(v)))) +
      `</div>` +
      `<p class="r-note">Usage is recorded only while Network Speed Monitor is running.</p>` +
      `</div>`;
  }
  document.querySelectorAll("[data-report-example]").forEach(renderReport);

  // ---- The simulated readout ----

  const readout = $("readout");
  if (!readout) return;
  const menu = $("menu"), report = $("report"), note = $("note"), tray = $("tray"), taskbar = $("taskbar");
  const showMenuBtn = $("show-menu");
  const lines = readout.querySelector(".lines");

  // A believable minute of traffic: idle trickle, page loads, a download, a video call.
  const scenes = [
    { len: 8, rx: [2e3, 18e3], tx: [1e3, 6e3] },
    { len: 5, rx: [300e3, 2.4e6], tx: [20e3, 90e3], spiky: true },
    { len: 14, rx: [4.2e6, 6.1e6], tx: [60e3, 140e3] },
    { len: 6, rx: [10e3, 60e3], tx: [3e3, 12e3] },
    { len: 16, rx: [700e3, 1.3e6], tx: [240e3, 420e3] },
  ];
  let scene = 0, left = scenes[0].len;
  const rnd = (a, b) => a + Math.random() * (b - a);
  function sample() {
    if (--left < 0) { scene = (scene + 1) % scenes.length; left = scenes[scene].len; }
    const s = scenes[scene];
    const k = s.spiky && Math.random() < 0.5 ? 0.15 : 1;
    return { rx: rnd(...s.rx) * k, tx: rnd(...s.tx) * k };
  }

  const SAMPLES = 60; // kGraphSamples
  const rx = [], tx = [];
  for (let i = 0; i < SAMPLES; i++) { const s = sample(); rx.push(s.rx); tx.push(s.tx); }

  // DrawSeries: newest sample at the right edge, both series against one max (never below 1000).
  function draw() {
    const max = Math.max(1000, ...rx, ...tx);
    const step = 54 / (SAMPLES - 1), top = 0.75, bottom = 27.25; // stroke 1.5, half inset
    const pts = (arr) => arr.map((v, i) => {
      const x = 54 - (arr.length - 1 - i) * step;
      const y = bottom - Math.min(Math.max(v / max, 0), 1) * (bottom - top);
      return x.toFixed(2) + "," + y.toFixed(2);
    }).join(" ");
    $("g-rx").setAttribute("points", pts(rx));
    $("g-tx").setAttribute("points", pts(tx));
    $("v-up").textContent = fmt(tx[tx.length - 1]);
    $("v-down").textContent = fmt(rx[rx.length - 1]);
  }

  function clock() {
    const d = new Date();
    $("clock-t").textContent = d.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
    $("clock-d").textContent = d.toLocaleDateString();
  }

  // WidestTextWidth: the text box is as wide as the widest value the formatter can print, in either
  // unit mode, so the lines stay right-aligned in a box that never changes width while ticking.
  function sizeText() {
    const letters = readout.classList.contains("letters");
    const probe = document.createElement("span");
    probe.style.cssText = "position:absolute;visibility:hidden;white-space:pre";
    lines.append(probe);
    let widest = 0;
    for (const prefix of letters ? ["U: ", "D: "] : ["↑ ", "↓ "]) {
      for (let k = 1; k <= 1e9; k *= 1000) {
        for (const v of [8.88, 88.8, 888]) {
          for (const text of [fmt(v * k), fmtBits((v * k) / 8)]) {
            probe.textContent = prefix + text;
            widest = Math.max(widest, probe.getBoundingClientRect().width);
          }
        }
      }
    }
    probe.remove();
    const em = parseFloat(getComputedStyle(lines).fontSize);
    if (widest > 0 && em > 0) lines.style.minWidth = (Math.ceil(widest) + 2) / em + "em";
  }

  draw(); clock();
  setInterval(() => {
    const s = sample();
    rx.push(s.rx); tx.push(s.tx); rx.shift(); tx.shift();
    draw(); clock();
  }, 1000);

  // Point the annotation's leader line at the middle of the readout, and right-align the menu to it.
  function lead() {
    const t = tray.getBoundingClientRect(), r = readout.getBoundingClientRect();
    // Right-align the note to the readout, but never closer than 1rem to the viewport edge.
    const right = Math.max(t.right - r.right, 16 - (innerWidth - t.right));
    note.style.right = right + "px";
    note.style.setProperty("--lead-x", t.right - right - (r.left + r.width / 2) + "px");
    placeMenu();
  }
  // The menu opens right-aligned to the readout, like a menu at the pointer, but stays on screen.
  function placeMenu() {
    if (menu.hidden) return;
    const r = readout.getBoundingClientRect(), bar = taskbar.getBoundingClientRect();
    const right = Math.min(bar.right - r.right, bar.width - menu.offsetWidth - 8);
    menu.style.setProperty("--pop-right", Math.max(8, right) + "px");
  }
  function layout() { sizeText(); lead(); }
  addEventListener("resize", lead);
  layout();
  if (document.fonts) document.fonts.ready.then(layout);

  // ---- Taskbar colour: the report follows it ----

  const themeButtons = document.querySelectorAll("[data-theme]");
  themeButtons.forEach((b) => b.addEventListener("click", () => {
    const light = b.dataset.theme === "light";
    themeButtons.forEach((o) => o.setAttribute("aria-pressed", String(o === b)));
    taskbar.classList.toggle("taskbar-light", light);
    report.classList.toggle("report-dark", !light);
  }));

  // ---- Popups, like the real app: left-click opens the report, right-click opens the menu ----

  const menuItems = [...menu.querySelectorAll('[role^="menuitem"]')];
  const isOpen = (el) => !el.hidden;

  function show(el) {
    if (isOpen(el)) return; // a second open (contextmenu after Shift+F10, long-press) changes nothing
    for (const p of [menu, report]) if (p !== el) p.hidden = true;
    el.hidden = false;
    if (el === menu) placeMenu();
    el.classList.remove("opening"); void el.offsetWidth; el.classList.add("opening");
    note.style.visibility = "hidden";
    sync();
    const first = el === menu ? menuItems[0] : el.querySelector("button");
    if (first) first.focus({ preventScroll: true });
  }
  function hideAll() {
    menu.hidden = report.hidden = true;
    note.style.visibility = "";
    sync();
  }
  function sync() {
    readout.setAttribute("aria-expanded", String(isOpen(report)));
    showMenuBtn.setAttribute("aria-expanded", String(isOpen(menu)));
  }
  function closeToReadout() { hideAll(); readout.focus({ preventScroll: true }); }

  // Long-press (touch has no right-click): 500 ms down without moving more than 10 px opens the menu.
  let pressTimer = 0, pressX = 0, pressY = 0, swallowClick = false;
  const cancelPress = () => { clearTimeout(pressTimer); pressTimer = 0; };
  readout.addEventListener("pointerdown", (e) => {
    swallowClick = false;
    cancelPress();
    if (e.button !== 0) return;
    pressX = e.clientX; pressY = e.clientY;
    pressTimer = setTimeout(() => { pressTimer = 0; swallowClick = true; show(menu); }, 500);
  });
  readout.addEventListener("pointermove", (e) => {
    if (pressTimer && Math.hypot(e.clientX - pressX, e.clientY - pressY) > 10) cancelPress();
  });
  for (const type of ["pointerup", "pointercancel", "pointerleave"]) readout.addEventListener(type, cancelPress);

  readout.addEventListener("click", (e) => {
    if (swallowClick && e.detail !== 0) { swallowClick = false; return; } // the long-press already opened the menu
    swallowClick = false;
    if (isOpen(report)) hideAll(); else show(report);
  });
  readout.addEventListener("contextmenu", (e) => { e.preventDefault(); cancelPress(); show(menu); });
  readout.addEventListener("keydown", (e) => {
    if (e.key === "ContextMenu" || (e.key === "F10" && e.shiftKey)) { e.preventDefault(); show(menu); }
  });
  showMenuBtn.addEventListener("click", () => { if (isOpen(menu)) hideAll(); else show(menu); });

  // Menu keyboard: arrows, Home and End move; Tab leaves the menu.
  menu.addEventListener("keydown", (e) => {
    const i = menuItems.indexOf(document.activeElement);
    const go = (n) => { e.preventDefault(); menuItems[(n + menuItems.length) % menuItems.length].focus(); };
    if (e.key === "ArrowDown") go(i + 1);
    else if (e.key === "ArrowUp") go(i < 0 ? -1 : i - 1);
    else if (e.key === "Home") go(0);
    else if (e.key === "End") go(-1);
    else if (e.key === "Tab") { e.preventDefault(); closeToReadout(); }
  });

  $("m-report").addEventListener("click", () => show(report));
  $("m-settings").addEventListener("click", closeToReadout);
  $("m-exit").addEventListener("click", closeToReadout);
  $("r-close").addEventListener("click", closeToReadout);
  $("m-graph").addEventListener("click", (e) => {
    const on = e.currentTarget.getAttribute("aria-checked") !== "true";
    e.currentTarget.setAttribute("aria-checked", String(on));
    readout.querySelector("svg").style.display = on ? "" : "none";
    lead();
  });
  $("m-letters").addEventListener("click", (e) => {
    const on = e.currentTarget.getAttribute("aria-checked") !== "true";
    e.currentTarget.setAttribute("aria-checked", String(on));
    readout.classList.toggle("letters", on);
    // IDS_LABEL_UP / IDS_LABEL_DOWN, in the muted colour; arrows in the series colour.
    readout.querySelector(".a-up").textContent = on ? "U:" : "↑";
    readout.querySelector(".a-down").textContent = on ? "D:" : "↓";
    layout();
  });

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && (isOpen(menu) || isOpen(report))) closeToReadout();
  });
  document.addEventListener("pointerdown", (e) => {
    if (!e.target.closest(".popup, #readout, #show-menu")) hideAll();
  });
})();
