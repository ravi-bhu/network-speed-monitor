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

  // ---- Report replica (reusable): fills every [data-report-example] with the report window ----

  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

  // Example history for one home PC, not real data: every day from 1 July 2026 to "today", Friday 23 October
  // 2026, with today's traffic up to 6 PM. October is written out; July to September come from a seeded
  // generator, so every visitor sees the same numbers and every total agrees with the others.
  const GB = 1e9;
  const ZERO = { rx: 0, tx: 0 };
  const dayNo = (y, m, d) => Math.round(Date.UTC(y, m, d) / 864e5); // whole days since 1970-01-01
  const dateOf = (n) => { const u = new Date(n * 864e5); return new Date(u.getUTCFullYear(), u.getUTCMonth(), u.getUTCDate()); };
  const FIRST = dayNo(2026, 6, 1), TODAY = dayNo(2026, 9, 23), NOW_HOUR = 18;
  const WEEK = dayNo(2026, 9, 19); // this week started on Monday 19 October (the app follows the locale's first day)
  const OCTOBER = [ // internet traffic per day in GB, [received, sent]
    [1.42, 0.16], [1.88, 0.21], [3.10, 0.19], [3.46, 0.22], [1.25, 0.14], [1.61, 0.31], [0.94, 0.08],
    [1.70, 0.18], [2.05, 0.24], [2.87, 0.17], [3.92, 0.25], [1.33, 0.29], [1.58, 0.15], [1.47, 0.14],
    [1.76, 0.33], [6.21, 0.63], [3.35, 0.21], [2.98, 0.19], [1.52, 0.27], [1.81, 0.30], [1.39, 0.14],
    [2.14, 0.22], [1.12, 0.11],
  ];
  const HISTORY = new Map(); // day number -> { rx, tx } in bytes
  {
    let seed = 2026;
    const rand = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 2 ** 32; };
    for (let n = FIRST; n < dayNo(2026, 9, 1); n++) {
      const weekend = [0, 6].includes(dateOf(n).getDay());
      let rx = (weekend ? 2.6 : 1.6) * (0.55 + rand() * 0.9);
      if (rand() < 0.06) rx += 2 + rand() * 2.5; // a big download now and then
      const tx = rx * (0.07 + rand() * 0.08);
      HISTORY.set(n, { rx: Math.round(rx * 100) / 100 * GB, tx: Math.round(tx * 100) / 100 * GB });
    }
    OCTOBER.forEach(([rx, tx], i) => HISTORY.set(dayNo(2026, 9, 1) + i, { rx: rx * GB, tx: tx * GB }));
  }
  const dayTotals = (n) => HISTORY.get(n) || ZERO;
  const sum = (t) => t.rx + t.tx;
  const add = (a, b) => ({ rx: a.rx + b.rx, tx: a.tx + b.tx });
  function totalsOf(from, to) { let t = ZERO; for (let n = from; n <= to; n++) t = add(t, dayTotals(n)); return t; }

  // A day by the hour: quiet at night, busiest in the evening. Today stops at 6 PM.
  const PROFILE = [.5, .3, .2, .15, .15, .2, .5, 1, 1.4, 1.5, 1.5, 1.4, 1.5, 1.4, 1.3, 1.4, 1.7, 2, 2.5, 2.9, 3, 2.6, 1.8, 1];
  function hoursOf(n) {
    const last = n === TODAY ? NOW_HOUR : 23, d = dayTotals(n);
    const weight = PROFILE.slice(0, last + 1).reduce((a, b) => a + b, 0);
    return PROFILE.map((p, h) => (h > last ? ZERO : { rx: d.rx * p / weight, tx: d.tx * p / weight }));
  }

  // Each adapter's and Wi-Fi network's share of the internet traffic, from an October where Wi-Fi plus Ethernet
  // made up the internet total and the VPN ran over the Wi-Fi (so its traffic counts twice, IDS_NOTE_RAW).
  const OCT_TOTAL = totalsOf(dayNo(2026, 9, 1), TODAY);
  const share = (rx, tx) => ({ rx: rx * GB / OCT_TOTAL.rx, tx: tx * GB / OCT_TOTAL.tx });
  const ADAPTERS = [["Wi-Fi", "Wi-Fi", share(44.37, 4.41)], ["Ethernet", "Ethernet", share(6.49, 0.72)], ["Work VPN", "Virtual", share(2.84, 0.61)]];
  const NETWORKS = [["Home-5G", share(39.12, 3.58)], ["Office", share(4.78, 0.76)], ["Café guest", share(0.47, 0.07)]];
  const split = (t, s) => ({ rx: t.rx * s.rx, tx: t.tx * s.tx });

  // The period list, as the app fills it (IDS_PERIOD_*), and ResolvePeriod's ranges as [first day, last day].
  const PERIODS = [
    ["today", "Today"], ["yesterday", "Yesterday"], ["thisWeek", "This week"], ["lastWeek", "Last week"],
    ["thisMonth", "This month"], ["lastMonth", "Last month"], ["last30", "Last 30 days"], ["thisYear", "This year"],
    ["allTime", "All time"], ["custom", "Custom range…"],
  ];
  const RANGES = {
    today: [TODAY, TODAY], yesterday: [TODAY - 1, TODAY - 1],
    thisWeek: [WEEK, WEEK + 6], lastWeek: [WEEK - 7, WEEK - 1],
    thisMonth: [dayNo(2026, 9, 1), dayNo(2026, 9, 31)], lastMonth: [dayNo(2026, 8, 1), dayNo(2026, 8, 30)],
    last30: [TODAY - 29, TODAY], thisYear: [dayNo(2026, 0, 1), dayNo(2026, 11, 31)],
    allTime: [FIRST, TODAY, true], // open: from the first day with data
  };
  // PickGranularity: hours for one day within the last 90, days up to 92 days, months beyond (and for All time).
  const granularityOf = ([from, to, open]) =>
    open ? "month" : from === to && from >= TODAY - 90 ? "hour" : to - from < 92 ? "day" : "month";

  function barsOf([from, to], g) {
    if (g === "hour") return hoursOf(from).map((t, h) => ({ ...t, start: new Date(2000, 0, 1, h), day: from }));
    if (g === "day") {
      const out = [];
      for (let n = from; n <= to; n++) out.push({ ...dayTotals(n), start: dateOf(n) });
      return out;
    }
    const out = [], end = dateOf(to);
    for (let m = new Date(dateOf(from).getFullYear(), dateOf(from).getMonth(), 1); m <= end; m = new Date(m.getFullYear(), m.getMonth() + 1, 1)) {
      const a = dayNo(m.getFullYear(), m.getMonth(), 1), b = dayNo(m.getFullYear(), m.getMonth() + 1, 0);
      out.push({ ...totalsOf(Math.max(a, from), Math.min(b, to)), start: m });
    }
    return out;
  }

  const shortDate = (n) => dateOf(n).toLocaleDateString();
  const longDate = (d) => d.toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" });
  const timeOf = (h, minutes) => new Date(2000, 0, 1, h).toLocaleTimeString([], minutes ? { hour: "numeric", minute: "2-digit" } : { hour: "numeric" });

  // AxisLabels and BarTitle (report_window.cpp).
  function axisLabel(bars, i, g) {
    const d = bars[i].start;
    if (g === "hour") return timeOf(d.getHours());
    if (g === "day") {
      if (bars.length <= 8) return d.toLocaleDateString(undefined, { weekday: "short" }) + " " + d.getDate();
      return i === 0 || d.getDate() === 1 ? d.toLocaleDateString(undefined, { month: "short", day: "numeric" }) : String(d.getDate());
    }
    const spansYears = bars[0].start.getFullYear() !== bars[bars.length - 1].start.getFullYear();
    return d.toLocaleDateString(undefined, spansYears && (i === 0 || d.getMonth() === 0) ? { month: "short", year: "numeric" } : { month: "short" });
  }
  function barTitle(bar, g) {
    if (g === "hour") { // IDS_BAR_HOUR "%s, %s – %s"
      const h = bar.start.getHours();
      return `${longDate(dateOf(bar.day))}, ${timeOf(h, true)} – ${timeOf((h + 1) % 24, true)}`;
    }
    return g === "day" ? longDate(bar.start) : bar.start.toLocaleDateString(undefined, { month: "long", year: "numeric" });
  }
  // LabelStep (report_paint.cpp): the smallest step whose labels don't run into each other.
  const STEPS = { hour: [1, 2, 3, 4, 6, 12, 24], day: [1, 2, 7, 14, 28], month: [1, 2, 3, 6, 12, 24] };
  function labelStep(g, slot, labelW) {
    for (const s of STEPS[g]) if (s * slot >= labelW) return s;
    let s = STEPS[g][STEPS[g].length - 1];
    while (s * slot < labelW) s *= 2;
    return s;
  }
  const measure = document.createElement("canvas").getContext("2d");

  // ChooseAxis (report_paint.cpp): the smallest 1/2/2.5/5 x 10^k step covering max in at most 4 steps.
  function chooseAxis(max, maxSteps = 4) {
    const raw = Math.max(max, 1) / maxSteps;
    for (let decade = 1; ; decade *= 10) {
      const m = [1, 2, 2.5, 5].find((k) => k * decade >= raw && Number.isInteger(k * decade));
      if (m) { const step = m * decade; return { step, steps: Math.max(1, Math.ceil(max / step)) }; }
    }
  }

  const TABS = ["Summary", "Timeline", "Adapters", "Wi-Fi networks"]; // IDS_TAB_*
  const NONE = "No usage recorded in this period."; // IDS_NO_USAGE
  let reports = 0;

  function renderReport(root) {
    const id = "report-" + reports++;
    let period = "thisMonth", custom = null;

    root.innerHTML =
      `<p class="vh">Example data.</p>` +
      `<div class="r-bar"><label for="${id}-period">Period:</label>` +
      `<span class="r-select"><select id="${id}-period">` +
      PERIODS.map(([v, name]) => `<option value="${v}"${v === period ? " selected" : ""}>${esc(name)}</option>`).join("") +
      `</select></span><span class="r-range"></span>` +
      `<span class="r-btns" aria-hidden="true"><span class="r-btn">Export CSV…</span><span class="r-btn">Clear all history…</span></span></div>` +
      // The Custom range dialog (IDD_RANGE), shown over the report like the app's modal dialog.
      `<form class="r-dialog" role="dialog" aria-labelledby="${id}-dlg" hidden><p class="r-dialog-title" id="${id}-dlg">Custom range</p>` +
      `<label>From: <input type="date" name="from" required></label><label>To: <input type="date" name="to" required></label>` +
      `<span class="r-dialog-btns"><button type="submit" class="r-btn">OK</button><button type="button" class="r-btn" data-cancel>Cancel</button></span></form>` +
      `<div class="r-tabs" role="tablist" aria-label="Report">` +
      TABS.map((t, i) => `<button type="button" role="tab" id="${id}-tab${i}" aria-controls="${id}-page${i}" ` +
        `aria-selected="${i === 0}" tabindex="${i === 0 ? 0 : -1}">${esc(t)}</button>`).join("") +
      `</div><div class="r-pages">` +
      TABS.map((t, i) => `<div class="r-page" role="tabpanel" id="${id}-page${i}" aria-labelledby="${id}-tab${i}" tabindex="0"${i ? " hidden" : ""}></div>`).join("") +
      `</div>`;

    const select = root.querySelector("select"), rangeEl = root.querySelector(".r-range");
    const tabs = [...root.querySelectorAll('[role="tab"]')], panels = [...root.querySelectorAll('[role="tabpanel"]')];

    function update() {
      const range = period === "custom" ? custom : RANGES[period];
      const [from, to, open] = range;
      const g = granularityOf(range);
      const rangeText = open ? "" : from === to ? shortDate(from) : `${shortDate(from)} – ${shortDate(to)}`; // IDS_RANGE_FORMAT
      const periodName = period === "custom" ? rangeText : PERIODS.find(([v]) => v === period)[1];
      rangeEl.textContent = rangeText;

      const total = totalsOf(from, to);
      const empty = sum(total) === 0;
      let busiest = null;
      for (let n = from; n <= to; n++) if (sum(dayTotals(n)) > 0 && (!busiest || sum(dayTotals(n)) > sum(dayTotals(busiest)))) busiest = n;
      const byTotal = (a, b) => sum(b.t) - sum(a.t); // the default sort: Total, largest first
      const adapters = ADAPTERS.map(([name, kind, s]) => ({ name, kind, t: split(total, s) })).sort(byTotal);
      const networks = NETWORKS.map(([name, s]) => ({ name, t: split(total, s) })).sort(byTotal);

      // IDS_RX_TX "↓ %s · ↑ %s", with a spoken version.
      const detail = (t) =>
        `<span class="r-muted" aria-hidden="true">↓ ${volume(t.rx)} · ↑ ${volume(t.tx)}</span>` +
        `<span class="vh">Received ${volume(t.rx)}, sent ${volume(t.tx)}</span>`;

      // Summary: Today, This week and This month, then the chosen period, highlighted.
      const card = (title, t, on) =>
        `<div class="r-card${on ? " is-on" : ""}"><span class="r-muted">${esc(title)}</span>` +
        `<span class="r-value">${volume(sum(t))}</span>${detail(t)}</div>`;
      const row = (label, value, muted) =>
        `<div class="r-row"><span${muted ? ' class="r-muted"' : ""}>${label}</span><span>${value}</span></div>`;
      const panel = (title, rows) =>
        `<div class="r-panel"><p class="r-panel-title">${esc(title)}</p>${empty ? row(esc(NONE), "", true) : rows.join("")}</div>`;
      panels[0].innerHTML =
        `<div class="r-cards">${card("Today", dayTotals(TODAY))}${card("This week", totalsOf(WEEK, WEEK + 6))}` +
        `${card("This month", totalsOf(...RANGES.thisMonth))}${card(periodName, total, true)}</div>` +
        `<div class="r-panels">` +
        panel("Busiest day", busiest ? [row(esc(longDate(dateOf(busiest))), volume(sum(dayTotals(busiest)))), row(detail(dayTotals(busiest)), "", true)] : []) +
        panel("Top adapters", adapters.map((a) => row(esc(a.name), volume(sum(a.t))))) +
        panel("Top Wi-Fi networks", networks.map((n) => row(esc(n.name), volume(sum(n.t))))) +
        `</div>` +
        `<p class="r-note">Usage is recorded only while Network Speed Monitor is running.</p>`;

      // Timeline: received at the bottom of each bar, sent stacked on top; hover for the app's tooltip text.
      const legend = `<div class="r-legend"><span><i class="r-sw r-rx"></i>Received</span><span><i class="r-sw r-tx"></i>Sent</span></div>`;
      if (empty) {
        panels[1].innerHTML = legend + `<p class="r-note">${esc(NONE)}</p>`;
      } else {
        const bars = barsOf(range, g);
        const axis = chooseAxis(Math.max(...bars.map(sum)));
        const axisMax = axis.step * axis.steps;
        const pct = (v) => (100 * v / axisMax).toFixed(3) + "%";
        const labels = bars.map((b, i) => axisLabel(bars, i, g));
        const style = getComputedStyle(root);
        measure.font = `${style.fontSize} ${style.fontFamily}`;
        const labelW = Math.max(...labels.map((l) => measure.measureText(l).width)) + 8;
        const plotW = (root.clientWidth || 600) - 6 * parseFloat(style.fontSize); // less the Y labels and padding
        const step = labelStep(g, plotW / bars.length, labelW);
        let grid = "", yLabels = "", cols = "", xLabels = "";
        for (let k = 0; k <= axis.steps; k++) {
          const at = pct(axis.step * k);
          grid += `<i class="r-grid${k ? "" : " r-base"}" style="bottom:${at}"></i>`;
          yLabels += `<span style="bottom:${at}">${volume(axis.step * k)}</span>`;
        }
        bars.forEach((b, i) => {
          const tip = `${barTitle(b, g)}\nReceived: ${volume(b.rx)}\nSent: ${volume(b.tx)}\nTotal: ${volume(sum(b))}`;
          cols += `<span class="r-slot" title="${esc(tip)}">` + (sum(b) > 0
            ? `<span class="r-col" style="height:${pct(sum(b))}"><span class="r-tx" style="flex-grow:${b.tx}"></span><span class="r-rx" style="flex-grow:${b.rx}"></span></span>`
            : "") + `</span>`;
          xLabels += `<span>${i % step === 0 ? esc(labels[i]) : ""}</span>`;
        });
        const by = { hour: "hour", day: "day", month: "month" }[g];
        panels[1].innerHTML = legend +
          `<div class="r-chart" role="img" aria-label="${esc(`Usage by ${by}, ${periodName || "All time"}, example data. Total ${volume(sum(total))}.`)}">` +
          `<div class="r-y">${yLabels}</div><div class="r-plot">${grid}${cols}</div><div class="r-x">${xLabels}</div></div>`;
      }

      // Adapters and Wi-Fi networks: list views sorted by Total.
      const table = (label, columns, rows) => empty ? `<p class="r-note">${esc(NONE)}</p>` :
        `<div class="r-table-wrap"><table class="r-table" aria-label="${esc(label)}"><thead><tr>` +
        columns.map((c, i) => {
          const sorted = i === columns.length - 1;
          return `<th scope="col"${c.num ? ' class="r-num"' : ""}${sorted ? ' aria-sort="descending"' : ""}>` +
            `${sorted ? '<i class="r-sort" aria-hidden="true"></i>' : ""}${esc(c.name)}</th>`;
        }).join("") +
        `</tr></thead><tbody>` +
        rows.map((r) => `<tr>${columns.map((c) => `<td${c.num ? ' class="r-num"' : ""}>${esc(c.get(r))}</td>`).join("")}</tr>`).join("") +
        `</tbody></table></div>`;
      const nameCol = { name: "Name", get: (r) => r.name };
      const numberCols = [
        { name: "Received", num: true, get: (r) => volume(r.t.rx) },
        { name: "Sent", num: true, get: (r) => volume(r.t.tx) },
        { name: "Total", num: true, get: (r) => volume(sum(r.t)) },
      ];
      const rawNote = `<p class="r-raw">Each row is that adapter's or network's own traffic, so rows can add up to more than your internet total (a VPN and the Wi-Fi it runs over both count).</p>`;
      panels[2].innerHTML = rawNote + `<p class="r-check" aria-hidden="true"><i></i>Show hidden adapters</p>` +
        table("Adapters", [nameCol, { name: "Type", get: (r) => r.kind }, ...numberCols], adapters);
      panels[3].innerHTML = rawNote + table("Wi-Fi networks", [nameCol, ...numberCols], networks);
    }

    // Choosing a period redraws the report; Custom range… asks for the dates first, starting from the range
    // shown now, and swaps them if To comes before From, as the app's dialog does.
    const dialog = root.querySelector(".r-dialog"), [fromIn, toIn] = dialog.querySelectorAll("input");
    const iso = (n) => { const d = dateOf(n); return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`; };
    const fromIso = (s) => { const [y, m, d] = s.split("-").map(Number); return dayNo(y, m - 1, d); };
    let previous = period;
    function closeDialog(apply) {
      if (apply) {
        let a = fromIso(fromIn.value), b = fromIso(toIn.value);
        if (b < a) [a, b] = [b, a];
        custom = [a, b];
        period = previous = "custom";
        update();
      } else {
        select.value = period = previous;
      }
      dialog.hidden = true;
      select.focus({ preventScroll: true });
    }
    select.addEventListener("change", () => {
      if (select.value !== "custom") { period = previous = select.value; update(); return; }
      const [a, b] = period === "custom" ? custom : RANGES[period];
      fromIn.value = iso(a); toIn.value = iso(Math.min(b, TODAY));
      dialog.hidden = false;
      fromIn.focus({ preventScroll: true });
    });
    dialog.addEventListener("submit", (e) => { e.preventDefault(); closeDialog(true); });
    dialog.querySelector("[data-cancel]").addEventListener("click", () => closeDialog(false));
    dialog.addEventListener("keydown", (e) => {
      if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); closeDialog(false); } // Esc closes only the dialog
    });

    // Tabs: click, or Left/Right/Home/End with a roving tabindex; selection follows focus.
    const selectTab = (n) => tabs.forEach((t, i) => {
      t.setAttribute("aria-selected", String(i === n));
      t.tabIndex = i === n ? 0 : -1;
      panels[i].hidden = i !== n;
    });
    tabs.forEach((t, i) => t.addEventListener("click", () => selectTab(i)));
    root.querySelector('[role="tablist"]').addEventListener("keydown", (e) => {
      const i = tabs.indexOf(document.activeElement);
      const n = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
      if (i < 0 || n === undefined) return;
      e.preventDefault();
      const next = (n + tabs.length) % tabs.length;
      selectTab(next);
      tabs[next].focus();
    });

    update();
    // The axis labels depend on the window's width: redraw when it changes (or when the hero popup opens).
    let frame = 0;
    const relayout = () => { cancelAnimationFrame(frame); frame = requestAnimationFrame(update); };
    addEventListener("resize", relayout);
    root.addEventListener("report:layout", relayout);
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

  // ---- Taskbar colour (the report follows it) and Windows version ----

  const themeButtons = document.querySelectorAll("[data-theme]");
  themeButtons.forEach((b) => b.addEventListener("click", () => {
    const light = b.dataset.theme === "light";
    themeButtons.forEach((o) => o.setAttribute("aria-pressed", String(o === b)));
    taskbar.classList.toggle("taskbar-light", light);
    report.classList.toggle("report-dark", !light);
  }));

  const osButtons = document.querySelectorAll("[data-os]");
  function setOs(os) {
    osButtons.forEach((o) => o.setAttribute("aria-pressed", String(o.dataset.os === os)));
    taskbar.classList.toggle("win11", os === "11");
    lead();
  }
  osButtons.forEach((b) => b.addEventListener("click", () => setOs(b.dataset.os)));
  // Start on the visitor's own version where the browser says it (Chromium's client hints: Windows 11 reports
  // platform version 13 or later). Read locally; nothing is sent anywhere.
  if (navigator.userAgentData && navigator.userAgentData.platform === "Windows") {
    navigator.userAgentData.getHighEntropyValues(["platformVersion"]).then((v) => {
      if (parseInt(v.platformVersion, 10) >= 13) setOs("11");
    }).catch(() => {});
  }

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
    else el.querySelector("[data-report-example]").dispatchEvent(new Event("report:layout")); // measured while hidden
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
