# Showcase site design

A small static website for Network Speed Monitor, published with GitHub Pages from a GitHub Actions workflow at
`https://netspeed.ravibhushan.dev`.

## Intent

The app is a tiny, privacy-first Windows taskbar readout. Today the only place to learn about it is the README.
The site gives it a front door: a regular Windows user should land, understand in seconds what it is, believe it
is small and safe, and leave with the installer. Developers get a clear second path into the source.

The page must not read as generated ("AI slop"). It should feel like a quiet, well-made utility page whose
personality comes from the product itself, not from decoration.

## Decided with Ravi

- **Audience:** both, download first. Regular Windows users are primary; developers are a clear secondary path.
- **Scope:** a landing page plus four supporting pages: Guide, Privacy, Changelog, Verify a download (code signing
  and hashes).
- **Visuals:** a live HTML recreation of the product, not screenshots.
- **Tone:** quiet utility.
- **Domain:** `netspeed.ravibhushan.dev`.
- **Hero direction:** approved from the mockup (`.superpowers/brainstorm/hero.html`, not committed): the hero is a
  Windows desktop. Copy at top left; a full-bleed, magnified Windows 10 taskbar along the bottom of the first
  screen with the readout running live next to the tray clock. Left-click opens a recreated usage report,
  right-click opens the real context menu, and a Dark/Light switch flips the taskbar theme.

## Goals

- First screen shows the product working and offers the download.
- Every claim on the site is true today and checkable (no network connections, no telemetry, the import check in
  the release workflow, unsigned until SignPath approves, winget not yet published, traffic not counted while the
  app is closed).
- The site itself is as private as the app: no analytics, no cookies, no third-party requests (fonts self-hosted).
- Works well at phone width, keyboard accessible, respects reduced motion, readable in light and dark OS themes.
- Cheap to keep current: bumping the release version touches one obvious place.

## Non-goals

- No blog, newsletter, search, comments, or localisation.
- No build step, framework, or package manager. Hand-written HTML, CSS and a little JavaScript.
- No auto-detection of the latest release through the GitHub API (it would add a third-party request).
- No changes to the app itself.

## Anti-goals (failure even if it works)

- Template look: purple/blue gradients, glassmorphism, identical rounded feature cards with soft shadows, emoji
  or icon-grid features, tracked-out ALL-CAPS eyebrows over every heading, `→` on every link, numbered
  01/02/03 markers on things that aren't sequences.
- Marketing copy: "Supercharge", "blazing fast", "seamless", fake testimonials, invented download counts or star
  counts, badges walls.
- Overclaiming: implying it is signed, on winget, or counts traffic while closed.
- Scroll-triggered fade-ins on every section.

## Visual system

**Colour.** Taken from the product, so colour carries meaning everywhere: amber is upload, blue is download.

| Token | Light | Dark | Use |
|---|---|---|---|
| paper | `#f3f3f3` | `#262626` | page background (Windows' own neutral greys, not cream) |
| ink | `#1a1a1a` | `#f2f2f2` | text, primary button |
| muted | `#5f5f5f` | `#a0a0a0` | secondary text |
| rule | `#dcdcdc` | `#333333` | borders, table rules |
| down | `#0067c0` | `#5eb8f0` | download / received; also links and focus ring |
| up | `#b35c00` | `#f0a35e` | upload / sent |

The taskbar replica uses the app's exact overlay palettes from `src/ui/overlay_renderer.cpp` (dark: text
`#ffffff`, up `#f0a35e`, down `#5eb8f0`, muted `#9a9a9a`; light: `#000000`, `#b35c00`, `#0067c0`, `#5f5f5f`).
The report replica uses the palettes in `src/ui/report_paint.cpp`. The page follows the OS colour scheme; the
taskbar's Dark/Light switch is separate and defaults to dark (Windows 10's default taskbar).

**Type.** One family for the site: Atkinson Hyperlegible Next (SIL OFL), self-hosted as woff2 in weights 400, 500,
700 and 800. Chosen for legibility, which suits an honest utility. Headlines are set large and tight (weight 800,
negative tracking) and do the visual work; no accent words in headlines, no all-caps labels. The product replicas
use `"Segoe UI Variable Text", "Segoe UI"` like the real app, falling back to the site face. Numbers use tabular
figures. Body line length stays under ~70 characters.

**Layout.** Left-aligned throughout. The hero fills the first screen (header, copy, taskbar pinned at the bottom).
Below it, one reading column (~40rem) with an optional right margin column for short asides (for example "Check
it yourself" steps); asides drop below their paragraph on narrow screens. Content sections are separated by
spacing and a heading, not boxes. Supporting pages share the header and footer and use the same reading column.

**Motion.** Only the readout ticking and the graph drawing (the one orchestrated moment), plus responses to the
visitor's own actions (popups opening). Under `prefers-reduced-motion`, the readout still updates its numbers but
popups don't animate.

## Pages

### Landing (`index.html`)

1. **Hero** as in the approved mockup. Headline: "Your internet speed, next to the clock." Primary button
   "Download for Windows" links straight to the x64 installer of the current release; "Read the source on
   GitHub" beside it; a fine-print line with version, licence and supported Windows/architectures. The simulated
   readout is labelled as simulated for screen readers.
2. **What it does.** Short prose plus a definition list covering: the readout (upload top, download bottom,
   every second, one-minute graph, follows taskbar theme), Automatic adapter mode vs pinning an adapter, bytes or
   bits and arrows or U/D letters, all taskbars or primary only, starts with Windows (can turn off).
3. **The report.** A larger, static-but-clickable replica of the report window: tabs Summary, Timeline,
   Adapters, Wi-Fi networks switch content; period picker shown; Export CSV mentioned. Example data, clearly
   example. Mention history retention (hourly 90 days, then daily) and that traffic isn't counted while the app
   is closed.
4. **Privacy.** Lead statement: it makes no network connections. Then the specifics from `PRIVACY.md`, and a
   "check it yourself" aside: the release workflow fails if the exe imports Winsock, WinHTTP, WinINet or URLMon,
   and how a reader can look at imports themselves. Link to the full Privacy page.
5. **Install.** x64 and ARM64 installer links, Windows 10/11, per-user, no admin. Honest note: unsigned until
   code signing is approved, so SmartScreen may warn; link to Verify a download. winget shown as "coming once the
   package is published", not as a working command. Uninstall asks about deleting history.
6. **For developers.** One short paragraph: native C++ and Win32, MIT licence, how to build (link to README
   Build section), contributing link.
7. **Footer.** Links to all pages, GitHub, licence, "This site sets no cookies and makes no third-party
   requests."

### Guide (`guide.html`)

How to use it, from the README and app strings: the readout and its menu (Adapter, Units, Show graph, Use
letters, Open report…, Settings…, Exit), the report tabs and periods, renaming/hiding adapters, clearing
history, CSV export columns and the `series` explanation, the formula-injection note, starting with Windows,
uninstalling. Section links at the top.

### Privacy (`privacy.html`)

The content of `PRIVACY.md`, adapted to the site, plus one paragraph on the site's own privacy.

### Changelog (`changelog.html`)

Release notes per version, newest first. Starts with v0.1.0 (first release; x64 and ARM64 installers). Each entry
links to its GitHub release.

### Verify a download (`verify.html`)

Why releases are unsigned for now and the SignPath application; how to compare the installer's SHA256 with
`SHA256SUMS.txt` (PowerShell `Get-FileHash` command shown in a copyable block); what the SmartScreen warning
looks like and what "More info → Run anyway" means; the code signing policy and team roles from the README.

### `404.html`

Short, plain, links home.

## Where things live

- `site/` holds the whole site: the five pages and `404.html`, `assets/site.css`, `assets/hero.js` (readout,
  popups, report replica), `assets/fonts/` (woff2 plus the OFL licence text), `favicon.svg`, and a `CNAME` file
  containing `netspeed.ravibhushan.dev`.
- The current version and installer file names appear in the HTML. Every place is marked so a release bump is a
  find-and-replace of the version string within `site/`; the README notes this in a short "Website" section.
- `.github/workflows/pages.yml` deploys `site/` with the official Pages actions (`configure-pages`,
  `upload-pages-artifact`, `deploy-pages`) on pushes to `main` that touch `site/**` or the workflow, plus manual
  runs. Minimal permissions (`pages: write`, `id-token: write`, `contents: read`), one concurrent deployment.

## Manual steps for Ravi (outside the repo)

1. Repo Settings → Pages → Source: **GitHub Actions**.
2. DNS: a `CNAME` record `netspeed` → `ravi-bhu.github.io`.
3. Repo Settings → Pages → Custom domain: `netspeed.ravibhushan.dev`, then **Enforce HTTPS** once the certificate
   is issued. (With Actions deploys the `CNAME` file is not what sets the domain; the setting is.)

## Calls made by Claude (check these)

- **Deploy last.** `pages.yml` is added as the final step, so commits of a half-built site never deploy. The
  work happens on this session's branch and reaches `main` in one merge.
- **Relative URLs everywhere**, so the site also works at `ravi-bhu.github.io/network-speed-monitor/` before the
  custom domain is set.
- **Version bumps** are a find-and-replace within `site/` *excluding* `changelog.html`, which gets a new entry by
  hand. The README "Website" note says so.
- **The report replica matches the real window** (`src/ui/report_window.cpp`, `report_paint.cpp`) where the
  mockup simplified it, e.g. the default period is "This month".
- **Touch:** phones have no right-click, so the hero offers a visible "Show the menu" control next to the
  taskbar theme switch (and long-press on the readout).
- **Dark page theme:** the page's dark paper is lifted to `#262626` so the `#202020` taskbar still reads as a
  separate strip.
- **CI:** `ci.yml` gets `paths-ignore` for `site/**` and the Pages workflow, so site-only pushes don't run the
  Windows build.
- **"Windows 10 and 11"** is stated as the README states it, although the Windows 11 VM checks in
  `docs/manual-verification.md` are still open.

## Left to the builder

- Exact copy beyond the headline, within the tone and honesty rules above.
- The report replica's example numbers and chart shapes.
- Icon drawing details for the taskbar tray and favicon (generic shapes; no Microsoft logos beyond a generic
  four-pane start glyph).
- Spacing scale and breakpoints, as long as the hero taskbar stays visible in the first screen on common laptop
  and phone sizes.
