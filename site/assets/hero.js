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

  // ---- Report replica (reusable): fills every [data-report-example] with the report's four tabs ----

  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

  // Example data for a fixed month, so every number agrees with the others on any day. Plausible for one
  // home PC; not real data. Volumes are in GB (1 GB = 1e9 bytes) as [received, sent].
  const EXAMPLE = {
    year: 2026, monthIndex: 9, // October 2026 (months count from 0)
    today: 23, weekStart: 19,  // "today" is Friday 23 October; the week started on Monday 19 October
    // Internet traffic per day, 1 October to today. The rest of the month has no traffic yet.
    days: [
      [1.42, 0.16], [1.88, 0.21], [3.10, 0.19], [3.46, 0.22], [1.25, 0.14], [1.61, 0.31], [0.94, 0.08],
      [1.70, 0.18], [2.05, 0.24], [2.87, 0.17], [3.92, 0.25], [1.33, 0.29], [1.58, 0.15], [1.47, 0.14],
      [1.76, 0.33], [6.21, 0.63], [3.35, 0.21], [2.98, 0.19], [1.52, 0.27], [1.81, 0.30], [1.39, 0.14],
      [2.14, 0.22], [1.12, 0.11],
    ],
    // Each adapter's own traffic this month: name, type (IDS_KIND_*), received, sent. Wi-Fi plus Ethernet is
    // the internet total; the VPN runs over the Wi-Fi, so its traffic counts twice (IDS_NOTE_RAW).
    adapters: [["Wi-Fi", "Wi-Fi", 44.37, 4.41], ["Ethernet", "Ethernet", 6.49, 0.72], ["Work VPN", "Virtual", 2.84, 0.61]],
    // The Wi-Fi adapter's traffic by network; adds up to the Wi-Fi row.
    networks: [["Home-5G", 39.12, 3.58], ["Office", 4.78, 0.76], ["Café guest", 0.47, 0.07]],
  };
  const GB = 1e9;
  const totals = (pairs) => pairs.reduce((t, [rx, tx]) => ({ rx: t.rx + rx * GB, tx: t.tx + tx * GB }), { rx: 0, tx: 0 });
  const sum = (t) => t.rx + t.tx;

  // ChooseAxis (report_paint.cpp): the smallest 1/2/2.5/5 x 10^k step covering max in at most 4 steps.
  function chooseAxis(max, maxSteps = 4) {
    const raw = max / maxSteps;
    for (let decade = 1; ; decade *= 10) {
      const m = [1, 2, 2.5, 5].find((k) => k * decade >= raw && Number.isInteger(k * decade));
      if (m) { const step = m * decade; return { step, steps: Math.max(1, Math.ceil(max / step)) }; }
    }
  }

  const TABS = ["Summary", "Timeline", "Adapters", "Wi-Fi networks"]; // IDS_TAB_*
  let reports = 0;

  function renderReport(root) {
    const id = "report-" + reports++;
    const { year, monthIndex: month } = EXAMPLE;
    const days = EXAMPLE.days.map(([rx, tx], i) => ({ day: i + 1, rx: rx * GB, tx: tx * GB }));
    const today = days[EXAMPLE.today - 1];
    const week = totals(EXAMPLE.days.slice(EXAMPLE.weekStart - 1, EXAMPLE.today));
    const monthTotal = totals(EXAMPLE.days);
    const busiest = days.reduce((a, d) => (sum(d) > sum(a) ? d : a)); // the earliest wins a tie
    const adapters = EXAMPLE.adapters.map(([name, kind, rx, tx]) => ({ name, kind, rx: rx * GB, tx: tx * GB }));
    const networks = EXAMPLE.networks.map(([name, rx, tx]) => ({ name, rx: rx * GB, tx: tx * GB }));
    for (const list of [adapters, networks]) list.sort((a, b) => sum(b) - sum(a)); // by Total, largest first

    const dateOf = (day) => new Date(year, month, day);
    const longDate = (day) => dateOf(day).toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" });
    const lastDay = new Date(year, month + 1, 0).getDate();

    // IDS_RX_TX "↓ %s · ↑ %s", with a spoken version.
    const detail = (t) =>
      `<span class="r-muted" aria-hidden="true">↓ ${volume(t.rx)} · ↑ ${volume(t.tx)}</span>` +
      `<span class="vh">Received ${volume(t.rx)}, sent ${volume(t.tx)}</span>`;

    // Summary
    const card = (title, t, on) =>
      `<div class="r-card${on ? " is-on" : ""}"><span class="r-muted">${esc(title)}</span>` +
      `<span class="r-value">${volume(sum(t))}</span>${detail(t)}</div>`;
    const row = (label, value, muted) =>
      `<div class="r-row"><span${muted ? ' class="r-muted"' : ""}>${label}</span><span>${value}</span></div>`;
    const panel = (title, rows) => `<div class="r-panel"><p class="r-panel-title">${esc(title)}</p>${rows.join("")}</div>`;
    const summary =
      `<div class="r-cards">${card("Today", today)}${card("This week", week)}` +
      `${card("This month", monthTotal)}${card("This month", monthTotal, true)}</div>` +
      `<div class="r-panels">` +
      panel("Busiest day", [row(esc(longDate(busiest.day)), volume(sum(busiest))), row(detail(busiest), "", true)]) +
      panel("Top adapters", adapters.slice(0, 3).map((a) => row(esc(a.name), volume(sum(a))))) +
      panel("Top Wi-Fi networks", networks.slice(0, 3).map((n) => row(esc(n.name), volume(sum(n))))) +
      `</div>` +
      `<p class="r-note">Usage is recorded only while Network Speed Monitor is running.</p>`;

    // Timeline: one bar per day of the month (Granularity::Day), received at the bottom, sent stacked on top.
    // Hovering a bar shows the app's tooltip text (BarTitle, then IDS_TIP_RECEIVED / SENT / TOTAL).
    const axis = chooseAxis(sum(busiest));
    const axisMax = axis.step * axis.steps;
    const pct = (v) => (100 * v / axisMax).toFixed(3) + "%";
    let grid = "", yLabels = "", bars = "", xLabels = "";
    for (let k = 0; k <= axis.steps; k++) {
      const at = pct(axis.step * k);
      grid += `<i class="r-grid${k ? "" : " r-base"}" style="bottom:${at}"></i>`;
      yLabels += `<span style="bottom:${at}">${volume(axis.step * k)}</span>`;
    }
    for (let day = 1; day <= lastDay; day++) {
      const d = days[day - 1] || { rx: 0, tx: 0 };
      const tip = `${longDate(day)}\nReceived: ${volume(d.rx)}\nSent: ${volume(d.tx)}\nTotal: ${volume(sum(d))}`;
      bars += `<span class="r-slot" title="${esc(tip)}">` + (sum(d) > 0
        ? `<span class="r-col" style="height:${pct(sum(d))}"><span class="r-tx" style="flex-grow:${d.tx}"></span><span class="r-rx" style="flex-grow:${d.rx}"></span></span>`
        : "") + `</span>`;
      // AxisLabels: "MMM d" on the first bar, then the day number. One label a week is a fixed simplification
      // of LabelStep, which picks 1, 2, 7, 14 or 28 days from the bar and label widths.
      const label = day === 1 ? dateOf(day).toLocaleDateString(undefined, { month: "short", day: "numeric" }) : String(day);
      xLabels += `<span>${(day - 1) % 7 === 0 ? esc(label) : ""}</span>`;
    }
    const monthName = dateOf(1).toLocaleDateString(undefined, { month: "long", year: "numeric" });
    const timeline =
      `<div class="r-legend"><span><i class="r-sw r-rx"></i>Received</span><span><i class="r-sw r-tx"></i>Sent</span></div>` +
      `<div class="r-chart" role="img" aria-label="${esc(`Daily usage for ${monthName}, example data. Busiest day ${longDate(busiest.day)}, ${volume(sum(busiest))}.`)}">` +
      `<div class="r-y">${yLabels}</div>` +
      `<div class="r-plot">${grid}${bars}</div>` +
      `<div class="r-x">${xLabels}</div></div>`;

    // Adapters and Wi-Fi networks: list views sorted by Total, largest first (the default TableSort).
    const table = (label, cols, rows) =>
      `<div class="r-table-wrap"><table class="r-table" aria-label="${esc(label)}"><thead><tr>` +
      cols.map((c, i) => {
        const sorted = i === cols.length - 1;
        return `<th scope="col"${c.num ? ' class="r-num"' : ""}${sorted ? ' aria-sort="descending"' : ""}>` +
          `${sorted ? '<i class="r-sort" aria-hidden="true"></i>' : ""}${esc(c.name)}</th>`;
      }).join("") +
      `</tr></thead><tbody>` +
      rows.map((r) => `<tr>${cols.map((c) => `<td${c.num ? ' class="r-num"' : ""}>${esc(c.get(r))}</td>`).join("")}</tr>`).join("") +
      `</tbody></table></div>`;
    const nameCol = { name: "Name", get: (r) => r.name };
    const numberCols = [
      { name: "Received", num: true, get: (r) => volume(r.rx) },
      { name: "Sent", num: true, get: (r) => volume(r.tx) },
      { name: "Total", num: true, get: (r) => volume(sum(r)) },
    ];
    const rawNote = `<p class="r-raw">Each row is that adapter's or network's own traffic, so rows can add up to more than your internet total (a VPN and the Wi-Fi it runs over both count).</p>`;
    const adaptersTab = rawNote +
      `<p class="r-check" aria-hidden="true"><i></i>Show hidden adapters</p>` +
      table("Adapters", [nameCol, { name: "Type", get: (r) => r.kind }, ...numberCols], adapters);
    const wifiTab = rawNote + table("Wi-Fi networks", [nameCol, ...numberCols], networks);

    const pages = [summary, timeline, adaptersTab, wifiTab];
    root.innerHTML =
      `<p class="vh">Example data.</p>` +
      `<div class="r-bar"><span>Period:</span><span class="r-combo">This month</span>` +
      `<span class="r-range">${esc(dateOf(1).toLocaleDateString())} – ${esc(dateOf(lastDay).toLocaleDateString())}</span>` +
      `<span class="r-btns" aria-hidden="true"><span class="r-btn">Export CSV…</span><span class="r-btn">Clear all history…</span></span></div>` +
      `<div class="r-tabs" role="tablist" aria-label="Report">` +
      TABS.map((t, i) => `<button type="button" role="tab" id="${id}-tab${i}" aria-controls="${id}-page${i}" ` +
        `aria-selected="${i === 0}" tabindex="${i === 0 ? 0 : -1}">${esc(t)}</button>`).join("") +
      `</div><div class="r-pages">` +
      pages.map((p, i) => `<div class="r-page" role="tabpanel" id="${id}-page${i}" aria-labelledby="${id}-tab${i}" tabindex="0"${i ? " hidden" : ""}>${p}</div>`).join("") +
      `</div>`;

    // Tabs: click, or Left/Right/Home/End with a roving tabindex; selection follows focus.
    const tabs = [...root.querySelectorAll('[role="tab"]')];
    const panels = [...root.querySelectorAll('[role="tabpanel"]')];
    const select = (n) => tabs.forEach((t, i) => {
      t.setAttribute("aria-selected", String(i === n));
      t.tabIndex = i === n ? 0 : -1;
      panels[i].hidden = i !== n;
    });
    tabs.forEach((t, i) => t.addEventListener("click", () => select(i)));
    root.querySelector('[role="tablist"]').addEventListener("keydown", (e) => {
      const i = tabs.indexOf(document.activeElement);
      const n = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
      if (i < 0 || n === undefined) return;
      e.preventDefault();
      const next = (n + tabs.length) % tabs.length;
      select(next);
      tabs[next].focus();
    });
  }
  document.querySelectorAll("[data-report-example]").forEach(renderReport);

  // ---- The simulated readout ----

  const readout = $("readout");
  if (!readout) return;
  const menu = $("menu"), report = $("report-popup"), note = $("note"), taskbar = $("taskbar");
  const showMenuBtn = $("show-menu"), controls = document.querySelector(".stage-controls");
  const lines = readout.querySelector(".lines");

  // Header + hero fill the first screen: tell the CSS how tall the header really is (the nav wraps).
  const hero = document.querySelector(".hero"), head = document.querySelector(".site-head");
  const setHead = () => hero.style.setProperty("--head", Math.ceil(hero.getBoundingClientRect().top + scrollY) + "px");
  setHead();
  if (window.ResizeObserver) new ResizeObserver(setHead).observe(head);

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
  let bits = false; // Units: Bytes (the default) or Bits
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
    const speed = bits ? fmtBits : fmt;
    $("v-up").textContent = speed(tx[tx.length - 1]);
    $("v-down").textContent = speed(rx[rx.length - 1]);
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

  // Line the note's right edge up with the readout's and point its leader line at the readout's middle.
  // The note stays 16 px from either screen edge and clear of the controls when they share its row.
  function lead() {
    const bar = taskbar.getBoundingClientRect(), r = readout.getBoundingClientRect(), c = controls.getBoundingClientRect();
    const w = note.getBoundingClientRect().width; // set by max-width and the grid, not by the margin
    const leftBound = c.bottom <= bar.top ? Math.max(16, c.right + 24) : 16;
    const right = Math.max(16, Math.min(bar.right - r.right, bar.right - leftBound - w));
    note.style.setProperty("--note-right", right + "px");
    note.style.setProperty("--lead-x", bar.right - right - (r.left + r.width / 2) + "px");
    placeMenu();
  }
  // The menu opens right-aligned to the readout, like a menu at the pointer, but stays on screen.
  function placeMenu() {
    if (menu.hidden) return;
    const r = readout.getBoundingClientRect(), bar = taskbar.getBoundingClientRect();
    const right = Math.min(bar.right - r.right, bar.width - menu.offsetWidth - 8);
    menu.style.setProperty("--pop-right", Math.max(8, right) + "px");
    for (const s of subs) if (!s.list.hidden) placeSub(s);
  }
  function layout() { sizeText(); lead(); }
  addEventListener("resize", () => { setHead(); lead(); });
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
  let opener = readout; // focus goes back here when a popup closes

  // Submenus (Adapter, Units): beside the menu, on its left like the real one near the screen edge, or on
  // its right, with the first item level with the parent item; above the menu when neither side has room (phones).
  const subs = menuItems.filter((b) => b.hasAttribute("aria-haspopup"))
    .map((item) => ({ item, list: $(item.getAttribute("aria-controls")) }));
  const subItems = (list) => [...list.querySelectorAll('[role^="menuitem"]')];
  function placeSub({ item, list }) {
    const bar = taskbar.getBoundingClientRect(), m = menu.getBoundingClientRect(), it = item.getBoundingClientRect();
    const w = list.offsetWidth, h = list.offsetHeight;
    let left = m.left - bar.left - w + 3, top = it.top - bar.top - 5; // 4 px menu padding + 1 px border
    if (left < 8) left = m.right - bar.left - 3;
    if (left + w > bar.width - 8) {
      left = Math.min(Math.max(8, it.left - bar.left + 24), bar.width - w - 8);
      top = m.top - bar.top - h + 1;
    }
    list.style.left = left + "px";
    list.style.top = top + "px";
  }
  function closeSubs(except) {
    for (const s of subs) {
      if (s === except || s.list.hidden) continue;
      s.list.hidden = true;
      s.item.setAttribute("aria-expanded", "false");
    }
  }
  function openSub(s, focusFirst) {
    closeSubs(s);
    s.list.hidden = false;
    s.item.setAttribute("aria-expanded", "true");
    placeSub(s);
    if (focusFirst) subItems(s.list)[0].focus({ preventScroll: true });
  }

  function show(el, from) {
    if (from) opener = from;
    if (isOpen(el)) return; // a second open (contextmenu after Shift+F10, long-press) changes nothing
    for (const p of [menu, report]) if (p !== el) p.hidden = true;
    closeSubs();
    el.hidden = false;
    if (el === menu) placeMenu();
    el.classList.remove("opening"); void el.offsetWidth; el.classList.add("opening");
    note.style.visibility = "hidden";
    sync();
    const first = el === menu ? menuItems[0] : el.querySelector("button");
    if (first) first.focus({ preventScroll: true });
  }
  function hideAll() {
    closeSubs();
    menu.hidden = report.hidden = true;
    note.style.visibility = "";
    sync();
  }
  function sync() {
    readout.setAttribute("aria-expanded", String(isOpen(report)));
    showMenuBtn.setAttribute("aria-expanded", String(isOpen(menu)));
  }
  function closeToOpener() { hideAll(); opener.focus({ preventScroll: true }); }

  // Long-press (touch has no right-click): 500 ms down without moving more than 10 px opens the menu.
  let pressTimer = 0, pressX = 0, pressY = 0, swallowClick = false;
  const cancelPress = () => { clearTimeout(pressTimer); pressTimer = 0; };
  readout.addEventListener("pointerdown", (e) => {
    swallowClick = false;
    cancelPress();
    if (e.button !== 0) return;
    pressX = e.clientX; pressY = e.clientY;
    pressTimer = setTimeout(() => { pressTimer = 0; swallowClick = true; show(menu, readout); }, 500);
  });
  readout.addEventListener("pointermove", (e) => {
    if (pressTimer && Math.hypot(e.clientX - pressX, e.clientY - pressY) > 10) cancelPress();
  });
  for (const type of ["pointerup", "pointercancel", "pointerleave"]) readout.addEventListener(type, cancelPress);

  readout.addEventListener("click", (e) => {
    if (swallowClick && e.detail !== 0) { swallowClick = false; return; } // the long-press already opened the menu
    swallowClick = false;
    if (isOpen(report)) hideAll(); else show(report, readout);
  });
  readout.addEventListener("contextmenu", (e) => { e.preventDefault(); cancelPress(); show(menu, readout); });
  readout.addEventListener("keydown", (e) => {
    if (e.key === "ContextMenu" || (e.key === "F10" && e.shiftKey)) { e.preventDefault(); show(menu, readout); }
  });
  showMenuBtn.addEventListener("click", () => { if (isOpen(menu)) hideAll(); else show(menu, showMenuBtn); });

  // Menu keyboard: arrows, Home and End move; Right (or Enter) opens a submenu; Tab leaves the menu.
  menu.addEventListener("keydown", (e) => {
    const i = menuItems.indexOf(document.activeElement);
    const go = (n) => { e.preventDefault(); closeSubs(); menuItems[(n + menuItems.length) % menuItems.length].focus(); };
    const sub = subs.find((s) => s.item === document.activeElement);
    if (e.key === "ArrowDown") go(i + 1);
    else if (e.key === "ArrowUp") go(i < 0 ? -1 : i - 1);
    else if (e.key === "Home") go(0);
    else if (e.key === "End") go(-1);
    else if (e.key === "ArrowRight" && sub) { e.preventDefault(); openSub(sub, true); }
    else if (e.key === "Tab") { e.preventDefault(); closeToOpener(); }
  });
  // Submenu keyboard: the same moves; Left or Esc goes back to the parent item.
  for (const s of subs) {
    s.item.addEventListener("click", () => openSub(s, true));
    s.item.addEventListener("pointerenter", (e) => { if (e.pointerType === "mouse") openSub(s, false); });
    s.list.addEventListener("keydown", (e) => {
      const items = subItems(s.list), i = items.indexOf(document.activeElement);
      const go = (n) => { e.preventDefault(); items[(n + items.length) % items.length].focus(); };
      if (e.key === "ArrowDown") go(i + 1);
      else if (e.key === "ArrowUp") go(i < 0 ? -1 : i - 1);
      else if (e.key === "Home") go(0);
      else if (e.key === "End") go(-1);
      else if (e.key === "ArrowLeft" || e.key === "Escape") {
        e.preventDefault(); e.stopPropagation(); // Esc closes only the submenu
        closeSubs(); s.item.focus();
      } else if (e.key === "Tab") { e.preventDefault(); closeToOpener(); }
    });
    // Choosing moves the check mark and closes the menus; Bits switches the readout to bits per second.
    s.list.addEventListener("click", (e) => {
      const pick = e.target.closest('[role="menuitemradio"]');
      if (!pick) return;
      subItems(s.list).forEach((b) => b.setAttribute("aria-checked", String(b === pick)));
      if (pick.dataset.units) { bits = pick.dataset.units === "bits"; draw(); }
      closeToOpener();
    });
  }
  // Pointing at another item of the menu closes an open submenu, as in Windows.
  for (const b of menuItems) {
    if (!b.hasAttribute("aria-haspopup")) b.addEventListener("pointerenter", (e) => { if (e.pointerType === "mouse") closeSubs(); });
  }

  $("m-report").addEventListener("click", () => show(report));
  $("m-settings").addEventListener("click", closeToOpener);
  $("m-exit").addEventListener("click", closeToOpener);
  $("r-close").addEventListener("click", closeToOpener);
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
    if (e.key === "Escape" && (isOpen(menu) || isOpen(report))) closeToOpener();
  });
  document.addEventListener("pointerdown", (e) => {
    if (!e.target.closest(".popup, #readout, #show-menu")) hideAll();
  });
})();
