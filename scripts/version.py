#!/usr/bin/env python3
"""TrimFast's version, kept in one place and checked everywhere.

The version lives in CMakeLists.txt (`project(TrimFast VERSION X.Y.Z ...)`). Everything else is
derived from it at build time (the program, `--version`, the package names) or written by this
script (the places that cannot read it):

    packaging/haiku/trimfast.rdef                     major / middle / minor, development or final
    packaging/linux/io.github.taoman26.TrimFast.metainfo.xml  the <releases> list
    CHANGELOG.md                                      "## [X.Y.Z] - date" and, if present, the
                                                      link references at its end ([Unreleased]: .../compare/vX.Y.Z...HEAD)

    scripts/version.py show                       print the current version
    scripts/version.py check                      verify that all of the above agree (exit 1 if not)
    scripts/version.py set 0.2.0 [--date YYYY-MM-DD] [--force]
                                                  make X.Y.Z the current version

`set` turns the "Unreleased" part of the changelog into the new release, adds the release to the
AppStream data, and refuses to go backwards (--force) or to release without notes (write them under
"Unreleased" first).
Versions are MAJOR.MINOR.PATCH (numbers only: CMake needs that). Before 1.0.0 the minor number
may bring incompatible changes. See docs/releasing.md.
"""
import argparse
import datetime
import os
import re
import sys

ROOT = os.environ.get("TRIMFAST_ROOT") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CMAKE = "CMakeLists.txt"
RDEF = "packaging/haiku/trimfast.rdef"
METAINFO = "packaging/linux/io.github.taoman26.TrimFast.metainfo.xml"
CHANGELOG = "CHANGELOG.md"

VERSION_RE = r"(\d+)\.(\d+)\.(\d+)"
DATE_RE = r"\d{4}-\d{2}-\d{2}"
# "[Unreleased]: https://github.com/<owner>/<repo>/compare/v1.2.3...HEAD"
UNRELEASED_LINK_RE = r"^\[Unreleased\]: (\S+)/compare/v(%s)\.\.\.HEAD[ \t]*$" % r"\d+\.\d+\.\d+"


class Problem(Exception):
    pass


def path(rel):
    return os.path.join(ROOT, rel)


def read(rel):
    try:
        with open(path(rel), encoding="utf-8") as f:
            return f.read()
    except OSError as e:
        raise Problem("cannot read %s: %s" % (rel, e))


def write(rel, text):
    with open(path(rel), "w", encoding="utf-8") as f:
        f.write(text)


def parse(v):
    m = re.fullmatch(VERSION_RE, v)
    if not m:
        raise Problem("'%s' is not a version: use MAJOR.MINOR.PATCH, numbers only (e.g. 0.2.0)" % v)
    return tuple(int(x) for x in m.groups())


# ---- readers ------------------------------------------------------------------------------
def cmake_version():
    m = re.search(r"^project\(TrimFast VERSION (%s)\b" % VERSION_RE, read(CMAKE), re.M)
    if not m:
        raise Problem("no 'project(TrimFast VERSION x.y.z' in %s" % CMAKE)
    return m.group(1)


def rdef_version():
    text = read(RDEF)
    parts = []
    for key in ("major", "middle", "minor"):
        m = re.search(r"^\s*%s\s*=\s*(\d+)\s*,?" % key, text, re.M)
        if not m:
            raise Problem("no '%s = N' in %s" % (key, RDEF))
        parts.append(m.group(1))
    return ".".join(parts)


def metainfo_releases():
    """[(version, date)] in file order (newest first)."""
    found = re.findall(r'<release\s+version="([^"]*)"\s+date="([^"]*)"', read(METAINFO))
    for version, date in found:
        parse(version)
        if not re.fullmatch(DATE_RE, date):
            raise Problem("release %s in %s has a bad date '%s'" % (version, METAINFO, date))
    return found


def changelog_sections():
    """[(version or 'Unreleased', date or None, body)] in file order."""
    text = read(CHANGELOG)
    pattern = re.compile(r"^## \[([^\]]+)\](?: - (%s))?[ \t]*$" % DATE_RE, re.M)
    marks = list(pattern.finditer(text))
    out = []
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out.append((m.group(1), m.group(2), text[m.end():end].strip()))
    return out


# ---- commands -----------------------------------------------------------------------------
def check():
    problems = []
    try:
        current = cmake_version()
    except Problem as e:
        return [str(e)]

    try:
        if rdef_version() != current:
            problems.append("%s says %s, %s says %s" % (RDEF, rdef_version(), CMAKE, current))
    except Problem as e:
        problems.append(str(e))

    try:
        releases = metainfo_releases()
        if not releases:
            problems.append("%s lists no releases" % METAINFO)
        elif releases[0][0] != current:
            problems.append("newest release in %s is %s, but the version is %s" % (METAINFO, releases[0][0], current))
        versions = [parse(v) for v, _ in releases]
        if versions != sorted(versions, reverse=True):
            problems.append("releases in %s are not newest-first" % METAINFO)
    except Problem as e:
        problems.append(str(e))

    try:
        sections = changelog_sections()
        names = [s[0] for s in sections]
        if not names or names[0] != "Unreleased":
            problems.append('%s must start with a "## [Unreleased]" section' % CHANGELOG)
        released = [s for s in sections if s[0] != "Unreleased"]
        if not released:
            problems.append("%s has no released version" % CHANGELOG)
        else:
            version, date, body = released[0]
            if version != current:
                problems.append("newest release in %s is %s, but the version is %s" % (CHANGELOG, version, current))
            if not date:
                problems.append("release %s in %s has no date (## [x.y.z] - YYYY-MM-DD)" % (version, CHANGELOG))
            if not body:
                problems.append("release %s in %s has no notes" % (version, CHANGELOG))
            link = re.search(UNRELEASED_LINK_RE, read(CHANGELOG), re.M)
            if link and link.group(2) != current:
                problems.append("[Unreleased] in %s compares from v%s, but the version is %s" % (CHANGELOG, link.group(2), current))
            meta = dict(metainfo_releases())
            if date and meta.get(version) not in (None, date):
                problems.append("release %s is dated %s in %s but %s in %s" % (version, date, CHANGELOG, meta[version], METAINFO))
    except Problem as e:
        problems.append(str(e))
    return problems


def set_version(new, date, force):
    parse(new)
    current = cmake_version()
    if new == current and not force:
        raise Problem("the version is already %s" % current)
    if parse(new) < parse(current) and not force:
        raise Problem("%s is older than the current %s (use --force)" % (new, current))
    if not re.fullmatch(DATE_RE, date):
        raise Problem("the date must look like YYYY-MM-DD")
    try:
        datetime.date.fromisoformat(date)
    except ValueError:
        raise Problem("'%s' is not a real date" % date)

    # Changelog first: it can refuse (nothing to release), and nothing is written before that.
    log = read(CHANGELOG)
    m = re.search(r"^## \[Unreleased\][ \t]*\n", log, re.M)
    if not m:
        raise Problem('%s has no "## [Unreleased]" section' % CHANGELOG)
    rest = log[m.end():]
    nxt = re.search(r"^## \[", rest, re.M)
    notes = (rest[:nxt.start()] if nxt else rest).strip()
    if not notes:
        raise Problem("the Unreleased section of %s is empty: write the release notes first" % CHANGELOG)
    if re.search(r"^## \[%s\]" % re.escape(new), log, re.M):
        raise Problem("%s already has a section for %s" % (CHANGELOG, new))
    tail = rest[nxt.start():] if nxt else ""
    log = log[:m.end()] + "\n## [%s] - %s\n\n%s\n\n%s" % (new, date, notes, tail)
    # Link references: "[Unreleased]" now compares from the new version, and the new version gets a
    # link comparing it with the one before. (Only when the changelog has them.)
    link = re.search(UNRELEASED_LINK_RE, log, re.M)
    if link:
        base, before = link.group(1), link.group(2)
        log = log.replace(link.group(0), "[Unreleased]: %s/compare/v%s...HEAD\n[%s]: %s/compare/v%s...v%s" %
                          (base, new, new, base, before, new), 1)
    log = re.sub(r"\n{3,}", "\n\n", log).rstrip("\n") + "\n"

    major, minor, patch = parse(new)
    rdef = read(RDEF)
    for key, value in (("major", major), ("middle", minor), ("minor", patch)):
        rdef, n = re.subn(r"^(\s*%s\s*=\s*)\d+(\s*,?)" % key, r"\g<1>%d\g<2>" % value, rdef, count=1, flags=re.M)
        if n != 1:
            raise Problem("no '%s = N' in %s" % (key, RDEF))
    rdef, n = re.subn(r"B_APPV_(?:DEVELOPMENT|FINAL)", "B_APPV_DEVELOPMENT" if major == 0 else "B_APPV_FINAL", rdef, count=1)
    if n != 1:
        raise Problem("no 'variety = B_APPV_...' in %s" % RDEF)

    meta = read(METAINFO)
    if '<release version="%s"' % new not in meta:
        meta, n = re.subn(r"(<releases>[ \t]*\n)", r'\g<1>    <release version="%s" date="%s"/>\n' % (new, date), meta, count=1)
        if n != 1:
            raise Problem("no <releases> element in %s" % METAINFO)

    cm, n = re.subn(r"^(project\(TrimFast VERSION )%s" % VERSION_RE, r"\g<1>%s" % new, read(CMAKE), count=1, flags=re.M)
    if n != 1:
        raise Problem("no 'project(TrimFast VERSION x.y.z' in %s" % CMAKE)

    write(CHANGELOG, log)
    write(RDEF, rdef)
    write(METAINFO, meta)
    write(CMAKE, cm)
    return current


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("show")
    sub.add_parser("check")
    s = sub.add_parser("set")
    s.add_argument("version")
    s.add_argument("--date", default=datetime.date.today().isoformat())
    s.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "show":
            print(cmake_version())
        elif args.cmd == "check":
            problems = check()
            for p in problems:
                print("version: " + p, file=sys.stderr)
            if problems:
                return 1
            print("version %s: consistent" % cmake_version())
        else:
            old = set_version(args.version, args.date, args.force)
            print("%s -> %s (%s). Next: scripts/version.py check; build the packages (docs/releasing.md)." % (old, args.version, args.date))
            problems = check()
            for p in problems:
                print("version: " + p, file=sys.stderr)
            return 1 if problems else 0
    except Problem as e:
        print("version: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
