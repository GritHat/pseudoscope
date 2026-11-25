#!/usr/bin/env python3
"""In every language, at every size, no text may fall outside its place.

    fit.py PSEUDOSCOPE [LANGUAGE ...] [-v]

The interface, asked with PSEUDOSCOPE_FIT, writes down every text it had to
cut, lose or leave out. This walks through its screens (those of tests/ui, and
some more) at several sizes of window, once per language, and reports the
texts made of the words of the interface: those found in the catalogs of
lang/, as opposed to the lines of a program, which are cut wherever they are
too long, in any language.

"cut" is a text shortened with an ellipsis, "off" one that runs out of the
window, "lost" one whose last lines had no room, "wide" a line longer than the
window. A language passes when it has none. The keys left out of the bottom
line where it is too short ("hint") are counted, not refused: the line makes
room for the help, which lists them all.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = ["80x24", "100x30", "124x30", "160x45"]
R, D, TAB = "<right>", "<down>", "<tab>"

# (what to open, keys): the screens the tests of the interface do not reach at every level.
MORE = [
    ("examples", ""), ("examples", "?"), ("examples", D * 3),
    ("examples/basic.pscope", ""), ("examples/basic.pscope", "?"), ("examples/basic.pscope", "<home>" + D + "<enter>"),
    *[("examples/basic.pscope", "<home>" + D + "<enter><enter>" + "T" * n) for n in range(7)],
    ("examples/basic.pscope", "<home>" + D + "r"), ("examples/basic.pscope", "<home>" + D + "r<enter>" + R * 9),
    *[("examples/basic.pscope", f"r{level}" + R * 12) for level in "12345"],
    ("examples/basic.pscope", "r3" + R * 12 + "?"), ("examples/basic.pscope", "r3" + R * 6 + "v" + R * 6 + "f"),
    ("examples/basic.pscope", "r3g"), ("examples/basic.pscope", "r3go"), ("examples/fib.pscope", "r2" + R * 14),
    ("examples/tree.pscope", "r" + R * 3 + "3<enter>" + R * 40), ("examples/list.pscope", "r3" + R * 30),
    ("examples/advanced/stack_class.pscope", ""), ("examples/advanced/stack_class.pscope", "r1" + R * 12 + TAB * 2),
    *[("examples/advanced/shapes.pscope", "<home>" + D * 8 + "<enter><enter>" + "T" * n) for n in range(7)],
    ("examples/advanced/shapes.pscope", "r2" + R * 11), ("examples/advanced/dijkstra.pscope", "t" + D),
    ("examples/advanced/dijkstra.pscope", "t<enter>3" + R * 20),
    *[("examples/systems/login.pscope", "t<enter>" + R * 10 + TAB * n) for n in range(7)],
    ("examples/systems/login.pscope", "<home>" + D * 4), ("examples/systems/login.pscope", "t"),
    ("examples/systems/login.pscope", "t<enter>" + R * 10 + "<enter><enter>" + R * 2), ("examples/systems/login.pscope", "t<enter>g?"),
    *[("examples/systems/election.pscope", "t<enter>" + R * 40 + TAB * n) for n in range(7)],
    *[("examples/systems/stop_and_wait.pscope", "t<enter>" + R * 14 + TAB * n) for n in range(4)],
    ("examples/exercises/two_leaders.pscope", "t" + D), ("examples/exercises/two_leaders.pscope", "t<enter>g" + TAB * 5),
    ("examples/design/shop.pscope", "<home>" + D + "<enter>"), ("examples/design/shop.pscope", "r1" + R * 9),
    *[(path, keys) for path in sorted(glob.glob(os.path.join(ROOT, "examples/diagrams/*")) + glob.glob(os.path.join(ROOT, "examples/uml/*")))
      for keys in ("", R * 3 + D * 2, R * 6 + TAB, "?")],
    ("examples/documents", ""), ("examples/documents", "<enter>" + D), ("examples/documents", "<enter>" + D + "<enter>r" + R * 12),
    ("examples/documents", D + "<enter>"), ("examples/documents", D + "<enter><enter>r2" + R * 5), ("examples/documents", D + "<enter>" + D + "<enter>" + R * 3),
    ("examples/cormen", D), ("examples/cormen/sorting.clrs", ""), ("examples/cormen/sorting.clrs", "<home>r<enter>" + R * 12),
    ("examples/cormen/sorting.clrs", "<home><enter><enter>TT"), ("examples/cormen/sorting.clrs", "r2" + R * 20),
    ("examples/libraries/desenho.por", "rgd"), ("examples/libraries/files.pse", "r" + R * 16), ("examples/libraries/media.alg", "r" + R * 8),
    ("examples/libraries/funciones.psc", "r" + R * 8), ("tests/dialects/basico.psc", "<home>" + D * 2 + "<enter>r<enter>" + R * 14),
    *[(path, "") for path in sorted(glob.glob(os.path.join(ROOT, "tests/errors/*.plike")))],
]


def pieces_of(language):
    """The pieces of text of a language, as they can turn up on the screen."""
    pieces = set()
    path = os.path.join(ROOT, "lang", "pseudoscope.pot" if language == "en" else language + ".po")
    for field, text in re.findall(r'^(msgid(?:_plural)?|msgstr(?:\[\d\])?) "(.*)"$', open(path, encoding="utf-8").read(), re.M):
        if field.startswith("msgid") != (language == "en"):
            continue
        text = text.replace('\\"', '"').replace("\\\\", "\\")
        for piece in re.split(r"%[-+ #0-9.*]*[a-zA-Z]+|\\n|\\00\d", text):
            if len(piece.strip()) >= 6:
                pieces.add(piece.strip())
    return pieces


def scenes():
    for file in sorted(glob.glob(os.path.join(ROOT, "tests/ui/*.ui"))):
        program, _, keys, options, _ = (open(file, encoding="utf-8").read().split("\n") + [""] * 5)[:5]
        options = [o for o in options.split() if not o.startswith("--lang")]
        yield os.path.basename(file)[:-3], os.path.normpath(os.path.join(ROOT, "tests/ui", program)), keys, options
    for i, (path, keys) in enumerate(MORE):
        yield f"{os.path.basename(path)}+{i}", os.path.join(ROOT, path), keys, []


def notes(binary, language, path, keys, options, size, scratch):
    """What did not fit: a set of (what, room, needed, text)."""
    log = os.path.join(scratch, "fit.log")
    if os.path.exists(log):
        os.remove(log)
    done = subprocess.run([binary, "ui", path, f"--size={size}", "--keys=" + keys, f"--lang={language}", *options], cwd=scratch,
                          env=dict(os.environ, PSEUDOSCOPE_FIT=log), capture_output=True, text=True)
    found = set()
    width = int(size.split("x")[0])
    for line in done.stdout.split("\n"):
        if len(line) > width:
            found.add(("wide", width, len(line), line.strip()[:60]))
    if os.path.exists(log):
        for line in open(log, encoding="utf-8", errors="replace"):
            what, _, _, _, room, needed, text = line.rstrip("\n").split("\t", 6)
            found.add((what, int(room), int(needed), text.strip()))
    return found, done.returncode


def main():
    words = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv
    if not words:
        raise SystemExit(__doc__)
    binary = os.path.abspath(words[0])
    every = ["en"] + sorted(os.path.basename(p)[:-3] for p in glob.glob(os.path.join(ROOT, "lang/*.po")))
    wanted = words[1:] or every
    failed = 0
    with tempfile.TemporaryDirectory() as scratch:
        seen = {language: {} for language in every}  # (what, text) -> (size, scene, room, needed), at the smallest size
        known = {language: pieces_of(language) for language in every}
        runs = 0
        for name, path, keys, options in scenes():
            for size in SIZES:
                here = {}
                for language in every:
                    here[language], status = notes(binary, language, path, keys, options, size, scratch)
                    runs += 1
                    if status:
                        print(f"FAIL {name} at {size} in {language}: the interface stopped with status {status}")
                        failed += 1
                for language in every:
                    for what, room, needed, text in here[language]:
                        said_by_all = all(any(t == text for _, _, _, t in here[other]) for other in every)
                        ours = what in ("hint", "wide") or any(piece in text for piece in known[language])
                        if what != "wide" and (said_by_all or not ours):
                            continue  # a line of the program, a name, a value: the same in every language
                        seen[language].setdefault((what, text), (size, name, room, needed))
        for language in wanted:
            found = seen[language]
            hints = [key for key in found if key[0] == "hint"]
            wrong = [key for key in found if key[0] != "hint"]
            for (what, text), (size, name, room, needed) in sorted(found.items(), key=lambda item: (item[0][0], item[1][0], item[0][1])):
                if what != "hint" or verbose:
                    print(f"{language} {what:5s} {size:7s} {room:3d} < {needed:3d}  {text[:90]!r}   [{name}]")
            print(f"fit: {language}: {len(wrong)} texts out of place" if wrong else f"fit: {language}: every text is in its place",
                  f"({len(hints)} keys left out of a short bottom line)" if hints else "")
            failed += len(wrong)
        print(f"fit: {runs} screens drawn, in {len(every)} languages and {len(SIZES)} sizes")
    sys.exit(bool(failed))


main()
