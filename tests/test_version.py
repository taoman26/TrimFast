#!/usr/bin/env python3
"""Tests for scripts/version.py, run on a scratch copy of the files it manages."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCRIPT = os.path.join(REPO, "scripts", "version.py")
FILES = ["CMakeLists.txt", "packaging/haiku/trimfast.rdef",
         "packaging/linux/io.github.taoman26.TrimFast.metainfo.xml", "CHANGELOG.md"]


class VersionScript(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="trimfast-version-")
        for rel in FILES:
            dst = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(os.path.join(REPO, rel), dst)

    @property
    def cur(self):
        """The version of the scratch copy, as a string and as numbers (tests must not hard-code it)."""
        m = re.search(r"project\(TrimFast VERSION (\d+)\.(\d+)\.(\d+)", self.read("CMakeLists.txt"))
        self.numbers = tuple(int(x) for x in m.groups())
        return "%d.%d.%d" % self.numbers

    def next_minor(self):
        self.cur  # (reads self.numbers)
        a, b, _ = self.numbers
        return "%d.%d.0" % (a, b + 1)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def run_script(self, *args):
        env = dict(os.environ, TRIMFAST_ROOT=self.root)
        p = subprocess.run([sys.executable, SCRIPT, *args], env=env, capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr

    def read(self, rel):
        with open(os.path.join(self.root, rel), encoding="utf-8") as f:
            return f.read()

    def write(self, rel, text):
        with open(os.path.join(self.root, rel), "w", encoding="utf-8") as f:
            f.write(text)

    def add_unreleased_notes(self, text="### Added\n\n- Something new.\n"):
        log = self.read("CHANGELOG.md")
        self.write("CHANGELOG.md", log.replace("## [Unreleased]\n", "## [Unreleased]\n\n" + text, 1))

    # ---- the repository itself ----
    def test_the_real_files_are_consistent(self):
        rc, out = self.run_script("check")
        self.assertEqual(rc, 0, out)
        self.assertIn("consistent", out)

    def test_show_prints_the_cmake_version(self):
        rc, out = self.run_script("show")
        self.assertEqual(rc, 0)
        self.assertRegex(out.strip(), r"^\d+\.\d+\.\d+$")

    # ---- set ----
    def test_set_updates_everything(self):
        old = self.cur
        new = self.next_minor()
        a, b, _ = self.numbers
        self.add_unreleased_notes()
        rc, out = self.run_script("set", new, "--date", "2026-11-01")
        self.assertEqual(rc, 0, out)

        self.assertIn("project(TrimFast VERSION %s " % new, self.read("CMakeLists.txt"))
        rdef = self.read("packaging/haiku/trimfast.rdef")
        self.assertRegex(rdef, r"major\s*=\s*%d," % a)
        self.assertRegex(rdef, r"middle\s*=\s*%d," % (b + 1))
        self.assertRegex(rdef, r"minor\s*=\s*0,")
        self.assertIn("B_APPV_DEVELOPMENT" if a == 0 else "B_APPV_FINAL", rdef)

        meta = self.read("packaging/linux/io.github.taoman26.TrimFast.metainfo.xml")
        self.assertLess(meta.index('version="%s"' % new), meta.index('version="%s"' % old))  # newest first
        self.assertIn('<release version="%s" date="2026-11-01"/>' % new, meta)

        log = self.read("CHANGELOG.md")
        self.assertIn("## [%s] - 2026-11-01" % new, log)
        # The notes moved under the new release; a fresh empty Unreleased heads the file.
        self.assertLess(log.index("## [Unreleased]"), log.index("## [%s]" % new))
        self.assertLess(log.index("## [%s]" % new), log.index("- Something new."))
        self.assertLess(log.index("- Something new."), log.index("## [%s]" % old))
        between = log[log.index("## [Unreleased]"):log.index("## [%s]" % new)]
        self.assertEqual(between.strip(), "## [Unreleased]")

        rc, out = self.run_script("check")
        self.assertEqual(rc, 0, out)

    # ---- link references at the end of the changelog ----
    def test_set_moves_the_changelog_links(self):
        old = self.cur
        new = self.next_minor()
        log = self.read("CHANGELOG.md")
        self.assertRegex(log, r"(?m)^\[Unreleased\]: \S+/compare/v%s\.\.\.HEAD$" % re.escape(old))
        base = re.search(r"(?m)^\[Unreleased\]: (\S+)/compare/", log).group(1)
        self.add_unreleased_notes()
        rc, out = self.run_script("set", new, "--date", "2026-11-01")
        self.assertEqual(rc, 0, out)
        log = self.read("CHANGELOG.md")
        self.assertIn("[Unreleased]: %s/compare/v%s...HEAD" % (base, new), log)
        self.assertIn("[%s]: %s/compare/v%s...v%s" % (new, base, old, new), log)  # new version vs the previous
        self.assertNotIn("compare/v%s...HEAD" % old, log)                         # the stale link is gone
        self.assertEqual(log.count("[Unreleased]:"), 1)
        rc, out = self.run_script("check")
        self.assertEqual(rc, 0, out)

    def test_set_works_without_link_references(self):
        self.cur
        log = re.sub(r"(?m)^\[[^\]]+\]: .*\n?", "", self.read("CHANGELOG.md"))
        self.write("CHANGELOG.md", log)
        self.add_unreleased_notes()
        rc, out = self.run_script("set", self.next_minor(), "--date", "2026-11-01")
        self.assertEqual(rc, 0, out)
        self.assertNotIn("compare/", self.read("CHANGELOG.md"))

    def test_check_catches_a_stale_unreleased_link(self):
        cur = self.cur
        log = self.read("CHANGELOG.md")
        stale = log.replace("compare/v%s...HEAD" % cur, "compare/v0.0.1...HEAD", 1)
        self.assertNotEqual(stale, log)
        self.write("CHANGELOG.md", stale)
        rc, out = self.run_script("check")
        self.assertEqual(rc, 1)
        self.assertIn("Unreleased", out)

    def test_next_major_is_final(self):
        self.cur
        self.add_unreleased_notes()
        rc, out = self.run_script("set", "%d.0.0" % (self.numbers[0] + 1), "--date", "2027-01-01")
        self.assertEqual(rc, 0, out)
        rdef = self.read("packaging/haiku/trimfast.rdef")
        self.assertIn("B_APPV_FINAL", rdef)
        self.assertNotIn("B_APPV_DEVELOPMENT", rdef)

    def test_a_zero_major_is_development(self):
        self.cur
        if self.numbers[0] != 0:
            self.skipTest("the repository is past 1.0.0")
        self.assertIn("B_APPV_DEVELOPMENT", self.read("packaging/haiku/trimfast.rdef"))

    def test_release_without_notes_is_refused_and_nothing_changes(self):
        self.cur
        before = {rel: self.read(rel) for rel in FILES}
        rc, out = self.run_script("set", self.next_minor())
        self.assertEqual(rc, 1)
        self.assertIn("empty", out)
        self.assertEqual({rel: self.read(rel) for rel in FILES}, before)
        rc, out = self.run_script("check")  # ... and the files are still consistent
        self.assertEqual(rc, 0, out)

    def test_going_backwards_or_staying_is_refused(self):
        current = self.cur
        self.add_unreleased_notes()
        rc, out = self.run_script("set", "0.0.1")  # older than any real version
        self.assertEqual(rc, 1)
        self.assertIn("older", out)
        rc, out = self.run_script("set", current)
        self.assertEqual(rc, 1)
        self.assertIn("already", out)

    def test_bad_versions_and_dates_are_refused(self):
        current = self.cur
        self.add_unreleased_notes()
        for bad in ("1.2", "v1.2.3", "1.2.3-rc1", "one.two.three", ""):
            rc, out = self.run_script("set", bad)
            self.assertEqual(rc, 1, bad)
        new = self.next_minor()
        rc, out = self.run_script("set", new, "--date", "2026-13-45")
        self.assertEqual(rc, 1)
        rc, out = self.run_script("set", new, "--date", "tomorrow")
        self.assertEqual(rc, 1)
        self.assertIn("project(TrimFast VERSION %s " % current, self.read("CMakeLists.txt"))  # untouched

    # ---- check ----
    def test_check_catches_each_kind_of_drift(self):
        cur = self.cur
        a, b, c = self.numbers
        other = "%d.%d.%d" % (a, b, c + 9)
        rdef_minor = re.search(r"^\s*minor\s*=\s*\d+", self.read("packaging/haiku/trimfast.rdef"), re.M).group()
        cases = {
            "cmake": ("CMakeLists.txt", "VERSION " + cur, "VERSION " + other),
            "rdef": ("packaging/haiku/trimfast.rdef", rdef_minor, re.sub(r"\d+$", str(c + 5), rdef_minor)),
            "metainfo": ("packaging/linux/io.github.taoman26.TrimFast.metainfo.xml",
                         'release version="%s"' % cur, 'release version="%s"' % other),
            "changelog": ("CHANGELOG.md", "## [%s]" % cur, "## [%s]" % other),
        }
        for name, (rel, old, new) in cases.items():
            with self.subTest(name):
                original = self.read(rel)
                self.assertIn(old, original)
                self.write(rel, original.replace(old, new, 1))
                rc, out = self.run_script("check")
                self.assertEqual(rc, 1, out)
                self.write(rel, original)
                rc, out = self.run_script("check")
                self.assertEqual(rc, 0, out)

    def test_check_wants_a_date_and_notes_on_the_release(self):
        log = self.read("CHANGELOG.md")
        self.write("CHANGELOG.md", re.sub(r"(## \[%s\]) - \d{4}-\d{2}-\d{2}" % re.escape(self.cur), r"\1", log, count=1))
        rc, out = self.run_script("check")
        self.assertEqual(rc, 1)
        self.assertIn("no date", out)

    def test_check_wants_an_unreleased_section(self):
        log = self.read("CHANGELOG.md")
        self.write("CHANGELOG.md", log.replace("## [Unreleased]\n", "", 1))
        rc, out = self.run_script("check")
        self.assertEqual(rc, 1)
        self.assertIn("Unreleased", out)

    def test_missing_files_are_reported_not_crashed(self):
        os.remove(os.path.join(self.root, "CHANGELOG.md"))
        rc, out = self.run_script("check")
        self.assertEqual(rc, 1)
        self.assertIn("CHANGELOG.md", out)
        self.assertNotIn("Traceback", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
