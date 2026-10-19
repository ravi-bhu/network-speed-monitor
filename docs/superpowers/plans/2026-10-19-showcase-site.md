# Showcase Site Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A five-page static site in `site/`, deployed to GitHub Pages by an Actions workflow, whose hero is a live
replica of the Network Speed Monitor taskbar readout.

**Architecture:** Hand-written HTML pages sharing one stylesheet (`site/assets/site.css`). One script
(`site/assets/hero.js`) drives the landing page's taskbar, popups and report replica. A Python checker
(`tools/check_site.py`, standard library only) is the test suite: it enforces links, privacy and copy rules
across every page.

**Tech Stack:** HTML, CSS (custom properties, `color-mix`), vanilla JS (no modules needed), Python 3 stdlib for
the checker, GitHub Actions (`actions/configure-pages@v5`, `actions/upload-pages-artifact@v3`,
`actions/deploy-pages@v4`).

**Spec:** `docs/superpowers/specs/2026-10-19-showcase-site-design.md`

## Global Constraints

- No third-party requests from any page: no external `src`, no external stylesheet, no font CDN, no analytics.
  Outbound `<a href>` links (GitHub, SignPath) are fine.
- Relative URLs only for internal links and assets.
- Font: Atkinson Hyperlegible Next, self-hosted woff2, weights 400/500/700/800, OFL text alongside.
- Colours exactly as the spec's token table; dark page paper `#262626`. Taskbar and report replicas use the app's
  palettes from `src/ui/overlay_renderer.cpp` and `src/ui/report_paint.cpp`.
- Copy rules: sentence case; no `→` in link or button text; no all-caps labels; no middle-dot meta strings; never
  claim signed, on winget, or counting traffic while closed.
- Current version `0.1.0`; installers `NetworkSpeedMonitor-0.1.0-x64-setup.exe` and
  `NetworkSpeedMonitor-0.1.0-arm64-setup.exe` at
  `https://github.com/ravi-bhu/network-speed-monitor/releases/download/v0.1.0/<file>`.
- Every page: `<title>`, `meta description`, `lang="en"`, viewport meta, shared header and footer, visible focus,
  works at 360px wide without horizontal scroll.
- `pages.yml` lands in the last task only.

## Review Focus

- Phone visitor (no right-click, 360px wide): the taskbar still fits in the first screen and the menu is
  reachable via the visible "Show the menu" button. → Task 2 browser check at 375×812.
- Keyboard-only visitor: Tab reaches the readout, Enter opens the report, Shift+F10 / context-menu key opens the
  menu, Esc closes and returns focus to the readout. → Task 2 browser check.
- Dark OS theme: page paper `#262626`, dark taskbar `#202020` visibly separate, report replica stays on its own
  light/dark palette. → Task 2 and Task 5 browser checks.
- Reduced motion: numbers still update, popups don't animate. → Task 2 browser check (emulated via CSS check in
  checker: `prefers-reduced-motion` block present in `site.css`).
- A future release bump: replacing `0.1.0` across `site/` except `changelog.html` leaves all download links
  consistent. → checker test `versions_consistent`.

---

### Task 1: Site checker, shared styles, skeleton

**Files:**
- Create: `tools/check_site.py`
- Create: `site/assets/site.css`, `site/assets/fonts/*.woff2`, `site/assets/fonts/OFL.txt`, `site/favicon.svg`,
  `site/CNAME`, `site/404.html`, `site/index.html` (header, footer, placeholder main)
- Modify: `.gitignore` (add `.superpowers/` is already there; no change unless missing)

**Interfaces:**
- Produces: `python tools/check_site.py` exits 0 and prints `ok: N pages` when all checks pass; non-zero with
  one `path: problem` line per failure.
- Produces CSS: tokens `--paper --ink --muted --rule --down --up` on `:root`, dark overrides under
  `@media (prefers-color-scheme: dark)`; layout classes `.wrap` (max 72rem), `.read` (reading column ~40rem),
  `.aside` (margin note, drops below on narrow), `.site-head`, `.site-foot`, `.btn`, `.btn-primary`.
- Produces markup convention: header and footer HTML is copied verbatim into every page (no includes); the
  current page's nav link has `aria-current="page"`.

- [ ] **Step 1: Write `tools/check_site.py` with these checks, each a function returning a list of problems**
  - `internal_links_resolve`: every relative `href`/`src` (strip `#fragment`) points to an existing file under
    `site/`; every `#fragment` on the same or another page matches an `id`.
  - `no_third_party_requests`: no `src=`, `<link rel=stylesheet|preload|icon>` or CSS `url()` pointing at
    `http:`/`https:`/`//`.
  - `page_basics`: each `.html` has `lang="en"`, `<title>`, `meta name="description"`, viewport meta.
  - `copy_rules`: no `→` inside `<a>` or `<button>` text; no ` · ` in visible text.
  - `versions_consistent`: every `NetworkSpeedMonitor-X.Y.Z-` and `/releases/download/vX.Y.Z/` in pages other
    than `changelog.html` uses one single version.
  - `reduced_motion`: `site.css` contains `prefers-reduced-motion`.
- [ ] **Step 2: Run it against an empty `site/`** — Run: `python tools/check_site.py` — Expected: non-zero,
  `site/: no pages`.
- [ ] **Step 3: Fetch fonts** from the `@fontsource/atkinson-hyperlegible-next` package (latin subset woff2 for
  400/500/700/800, plus its licence) into `site/assets/fonts/`; write `@font-face` rules with
  `font-display: swap`.
- [ ] **Step 4: Write `site.css`, `favicon.svg` (the mockup's brand mark), `CNAME` (`netspeed.ravibhushan.dev`),
  `404.html`, and `index.html` with header, footer and empty `<main>`.** Footer text includes "This site sets no
  cookies and makes no third-party requests."
- [ ] **Step 5: Run the checker** — Expected: `ok: 2 pages`.
- [ ] **Step 6: Commit** `feat(site): skeleton, shared styles and site checker`

### Task 2: Landing hero

**Files:**
- Create: `site/assets/hero.js`
- Modify: `site/index.html`, `site/assets/site.css`

**Interfaces:**
- Consumes: Task 1 tokens and header.
- Produces: `hero.js` exposes nothing global; it wires elements by id: `taskbar`, `tray`, `readout`, `v-up`,
  `v-down`, `g-rx`, `g-tx`, `note`, `menu`, `report`, `show-menu`, theme buttons `[data-theme]`.
  `fmt(bytesPerSec)` inside it follows `src/core/units.cpp` `FormatSpeed` (1000-based, B/s KB/s MB/s GB/s,
  0/2/1/0 decimals rule including the boundary re-rounding).

- [ ] **Step 1: Port the approved mockup's hero** (`.superpowers/brainstorm/hero.html`) into `index.html`,
  `site.css` and `hero.js`, keeping its structure, copy and behaviour.
- [ ] **Step 2: Apply the spec's calls:** visible "Show the menu" button beside the theme switch; long-press
  (500 ms) on the readout opens the menu; Shift+F10 and the ContextMenu key open it; Esc closes popups and returns
  focus to the readout; arrow keys move within the menu; dark-scheme page paper `#262626`.
- [ ] **Step 3: Make the report popup match the real window**: title "Network usage – Network Speed Monitor",
  tabs Summary / Timeline / Adapters / Wi-Fi networks, "Period:" picker showing "This month", buttons
  "Export CSV…" and "Clear all history…", Received/Sent totals in the report palette, the coverage note text
  verbatim from `IDS_NOTE_COVERAGE`.
- [ ] **Step 4: Run the checker** — Expected: `ok`.
- [ ] **Step 5: Browser check** with the `site` launch config at 1366×768, 1024×640, 375×812, dark scheme:
  taskbar visible in first screen; click, right-click, Show the menu, Esc, Tab/Enter all behave; no horizontal
  scroll (`document.documentElement.scrollWidth === innerWidth`); no console errors.
- [ ] **Step 6: Commit** `feat(site): live taskbar hero`

### Task 3: Landing sections

**Files:**
- Modify: `site/index.html`, `site/assets/site.css`, `site/assets/hero.js` (report tabs)

**Interfaces:**
- Consumes: Task 2 report markup; reuse the same report component for the larger in-page replica, with tab
  buttons that switch panels (`role="tablist"`, arrow-key navigation).

- [ ] **Step 1: Write sections in order, each with an `id`:** `what` (What it does, definition list), `report`
  (in-page report replica with working tabs and example data; retention and not-counted-while-closed
  sentences), `privacy` (lead "It makes no network connections." plus specifics from `PRIVACY.md` and a
  "Check it yourself" aside naming ws2_32, winhttp, wininet, urlmon and `dumpbin /imports`), `install` (both
  installer links, unsigned/SmartScreen note linking `verify.html`, winget as not yet published, uninstall
  note), `developers` (C++/Win32, MIT, links to README build section and CONTRIBUTING).
- [ ] **Step 2: Run the checker** — Expected: `ok`.
- [ ] **Step 3: Browser check** full-page at 1366 and 375 wide, light and dark: asides drop below at narrow
  width, tabs switch by click and arrow keys.
- [ ] **Step 4: Commit** `feat(site): landing page sections`

### Task 4: Supporting pages

**Files:**
- Create: `site/guide.html`, `site/privacy.html`, `site/changelog.html`, `site/verify.html`

**Interfaces:**
- Consumes: header/footer markup and `.read`/`.aside` classes from Task 1.

- [ ] **Step 1: Write `guide.html`** from README and `res/strings.rc`: on-page contents links, the menu items
  verbatim, report tabs and periods, rename/hide/clear, CSV columns and `series` explanation, the leading-`'`
  note, start with Windows, uninstall.
- [ ] **Step 2: Write `privacy.html`** from `PRIVACY.md` plus a "This website" section.
- [ ] **Step 3: Write `changelog.html`** with the v0.1.0 entry linking
  `https://github.com/ravi-bhu/network-speed-monitor/releases/tag/v0.1.0`.
- [ ] **Step 4: Write `verify.html`**: why unsigned, `Get-FileHash .\NetworkSpeedMonitor-0.1.0-x64-setup.exe
  -Algorithm SHA256` in a copyable block (Copy button), comparing with `SHA256SUMS.txt`, SmartScreen
  "More info" then "Run anyway", signing policy and roles from README.
- [ ] **Step 5: Run the checker** — Expected: `ok: 6 pages`.
- [ ] **Step 6: Browser check** each page at 375 wide and in dark scheme; Copy button copies.
- [ ] **Step 7: Commit** `feat(site): guide, privacy, changelog and verify pages`

### Task 5: Deploy workflow and docs

**Files:**
- Create: `.github/workflows/pages.yml`
- Modify: `.github/workflows/ci.yml` (`paths-ignore: ['site/**', '.github/workflows/pages.yml']` on push and
  pull_request), `README.md` (short "Website" section: where the site lives, running the checker, release bump
  rule excluding `changelog.html`, the three manual Pages/DNS steps)

- [ ] **Step 1: Write `pages.yml`**: triggers `push` to `main` with `paths: ['site/**',
  '.github/workflows/pages.yml', 'tools/check_site.py']` and `workflow_dispatch`; permissions `contents: read`,
  `pages: write`, `id-token: write`; `concurrency: { group: pages, cancel-in-progress: false }`; job runs
  `python tools/check_site.py` before upload, uploads `site/`, deploys with environment `github-pages`.
- [ ] **Step 2: Edit `ci.yml` and README.**
- [ ] **Step 3: Verify** — Run: `python tools/check_site.py` (ok) and a YAML parse of both workflows
  (`python -c "import yaml,sys;[yaml.safe_load(open(p)) for p in sys.argv[1:]]" ...` if PyYAML is present,
  otherwise careful read).
- [ ] **Step 4: Final browser pass** of all six pages; then commit `ci: deploy the site to GitHub Pages`.
