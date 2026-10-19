#!/usr/bin/env python3
"""Static checks for the showcase site in site/. Standard library only.

Run from the repo root:  python tools/check_site.py
Prints "ok: N pages" and exits 0 when every check passes; otherwise prints one
"path: problem" line per failure and exits 1.
"""
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

SITE = Path(__file__).resolve().parent.parent / "site"

# Pages that do not exist yet. Remove once Task 4 lands (Task 4 deletes this constant).
PENDING_PAGES = {"guide.html", "privacy.html", "changelog.html", "verify.html"}

VOID = {"meta", "link", "br", "img", "hr", "input", "source", "area", "base", "col", "wbr"}


class Page(HTMLParser):
    """Collects what the checks need from one HTML file."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.refs = []          # (attr, value) for href/src
        self.links = []         # (rel, href) for <link>
        self.html_lang = None
        self.title = None
        self.meta = {}          # name -> content
        self.visible = []       # text outside data-replica / script / style / title
        self.interactive = []   # text inside <a>/<button>, outside data-replica
        self.inline_css = []
        self.raw_attrs = []     # all attribute values (catches url(...) in style="")
        self._stack = []        # open tags: (tag, skip, interactive)
        self._in_title = False
        self._in_style = False

    def handle_starttag(self, tag, attrs):
        a = {k: (v if v is not None else "") for k, v in attrs}
        if tag == "html":
            self.html_lang = a.get("lang")
        if a.get("id"):
            self.ids.add(a["id"])
        for k in ("href", "src"):
            if k in a:
                self.refs.append((k, a[k]))
        if tag == "link":
            self.links.append((a.get("rel", ""), a.get("href", "")))
        if tag == "meta" and a.get("name"):
            self.meta[a["name"].lower()] = a.get("content", "")
        if tag == "title":
            self._in_title = True
        if tag == "style":
            self._in_style = True
        self.raw_attrs.extend(v for v in a.values() if v)
        if tag in VOID:
            return
        parent_skip = self._stack[-1][1] if self._stack else False
        skip = parent_skip or "data-replica" in a or tag in ("script", "style", "title")
        self._stack.append((tag, skip, tag in ("a", "button")))

    def handle_startendtag(self, tag, attrs):
        # Self-closing syntax (<svg/>, <path/>): record attributes but do not push.
        self.handle_starttag(tag, attrs)
        if tag not in VOID and self._stack and self._stack[-1][0] == tag:
            self._stack.pop()

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False
        if tag == "style":
            self._in_style = False
        for i in range(len(self._stack) - 1, -1, -1):
            if self._stack[i][0] == tag:
                del self._stack[i:]
                break

    def handle_data(self, data):
        if self._in_title:
            self.title = (self.title or "") + data
        if self._in_style:
            self.inline_css.append(data)
        if not self._stack or self._stack[-1][1]:
            return
        self.visible.append(data)
        if any(s[2] for s in self._stack):
            self.interactive.append(data)


def parse(path):
    p = Page()
    p.feed(path.read_text(encoding="utf-8"))
    p.close()
    return p


def is_external(url):
    return bool(re.match(r"^(?:[a-z][a-z0-9+.-]*:|//)", url, re.I))


def is_remote(url):
    return bool(re.match(r"^(?:https?:)?//", url, re.I))


def rel(path):
    return path.relative_to(SITE.parent).as_posix()


def internal_links_resolve(pages):
    problems = []
    for path, p in pages.items():
        for attr, val in p.refs:
            if not val or is_external(val):
                continue
            target, _, frag = val.partition("#")
            target = target.partition("?")[0]
            if target:
                if target in PENDING_PAGES:
                    continue
                dest = (path.parent / target).resolve()
                if not dest.is_file() or SITE not in dest.parents:
                    problems.append(f"{rel(path)}: {attr}={val!r} does not resolve to a file under site/")
                    continue
            else:
                dest = path
            if frag:
                other = pages.get(dest)
                if other is None and dest.suffix == ".html":
                    other = parse(dest)
                if other is not None and frag not in other.ids:
                    problems.append(f"{rel(path)}: {attr}={val!r} has no matching id #{frag}")
    return problems


CSS_URL = re.compile(r"url\(\s*['\"]?\s*((?:https?:)?//[^)'\"\s]+)", re.I)


def no_third_party_requests(pages):
    problems = []
    for path, p in pages.items():
        for attr, val in p.refs:
            if attr == "src" and is_remote(val):
                problems.append(f"{rel(path)}: src={val!r} is a third-party request")
        for r, href in p.links:
            if re.search(r"\b(stylesheet|preload|icon|preconnect|prefetch)\b", r, re.I) and is_remote(href):
                problems.append(f"{rel(path)}: <link rel={r!r} href={href!r}> is a third-party request")
        css = "\n".join(p.inline_css + p.raw_attrs)
        for m in CSS_URL.finditer(css):
            problems.append(f"{rel(path)}: CSS url({m.group(1)}) is a third-party request")
    for css in sorted((SITE / "assets").rglob("*.css")):
        for m in CSS_URL.finditer(css.read_text(encoding="utf-8")):
            problems.append(f"{rel(css)}: CSS url({m.group(1)}) is a third-party request")
    return problems


def page_basics(pages):
    problems = []
    for path, p in pages.items():
        if p.html_lang != "en":
            problems.append(f'{rel(path)}: <html> needs lang="en"')
        if not (p.title or "").strip():
            problems.append(f"{rel(path)}: missing <title>")
        if not p.meta.get("description", "").strip():
            problems.append(f'{rel(path)}: missing meta name="description"')
        if "viewport" not in p.meta:
            problems.append(f'{rel(path)}: missing meta name="viewport"')
    return problems


def copy_rules(pages):
    # Text inside any element carrying data-replica is skipped by the parser:
    # product replicas reproduce the real app's strings verbatim.
    problems = []
    for path, p in pages.items():
        if "→" in "".join(p.interactive):
            problems.append(f"{rel(path)}: arrow character inside link or button text")
        if " · " in "".join(p.visible):
            problems.append(f"{rel(path)}: middle dot separator (space, U+00B7, space) in visible text")
    return problems


VERSION_PATTERNS = (
    re.compile(r"NetworkSpeedMonitor-(\d+\.\d+\.\d+)-"),
    re.compile(r"/releases/download/v(\d+\.\d+\.\d+)/"),
)


def versions_consistent(pages):
    found = {}
    for path in pages:
        if path.name == "changelog.html":
            continue
        text = path.read_text(encoding="utf-8")
        for pat in VERSION_PATTERNS:
            for m in pat.finditer(text):
                found.setdefault(m.group(1), set()).add(rel(path))
    if len(found) > 1:
        detail = "; ".join(f"{v} in {', '.join(sorted(f))}" for v, f in sorted(found.items()))
        return [f"site/: inconsistent release versions: {detail}"]
    return []


def reduced_motion(pages):
    css = SITE / "assets" / "site.css"
    if not css.is_file():
        return [f"{rel(css)}: missing"]
    if "prefers-reduced-motion" not in css.read_text(encoding="utf-8"):
        return [f"{rel(css)}: no prefers-reduced-motion rule"]
    return []


CHECKS = (internal_links_resolve, no_third_party_requests, page_basics,
          copy_rules, versions_consistent, reduced_motion)


def main():
    files = sorted(SITE.glob("*.html")) if SITE.is_dir() else []
    if not files:
        print("site/: no pages")
        return 1
    pages = {f.resolve(): parse(f) for f in files}
    problems = [m for check in CHECKS for m in check(pages)]
    if problems:
        print("\n".join(problems))
        return 1
    print(f"ok: {len(pages)} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
