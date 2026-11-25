#!/usr/bin/env python3
"""The translations to the other languages must behave like the program.

Usage: targets.py PSEUDOSCOPE [LANGUAGE...] [-k WORD] [-v]

Each program of tests/programs, tests/dialects and tests/targets is translated,
run with the tools of the language, and its output compared with NAME.out.
NAME.not lists the languages that cannot say it: they must refuse, not write
something wrong. Languages whose tools are not installed are skipped.
"""
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
verbose = "-v" in args
word = args[args.index("-k") + 1] if "-k" in args else ""
args = [a for i, a in enumerate(args) if a != "-v" and a != "-k" and (i == 0 or args[i - 1] != "-k")]
tool = os.path.abspath(args[0])
LANGUAGES = ["cpp", "java", "rust", "ts", "js", "python"]
wanted = args[1:] or LANGUAGES


def node_version():
    try:
        text = subprocess.run(["node", "--version"], capture_output=True, text=True).stdout
        return tuple(int(n) for n in re.findall(r"\d+", text)[:2])
    except OSError:
        return (0, 0)


def available(language):
    if language == "cpp":
        return shutil.which(os.environ.get("CXX", "c++")) is not None
    if language == "java":
        return shutil.which("java") is not None and shutil.which("javac") is not None
    if language == "rust":
        return shutil.which("rustc") is not None or os.path.exists(os.path.expanduser("~/.cargo/bin/rustc"))
    if language == "python":
        return shutil.which("python3") is not None
    if language == "ts" and shutil.which("tsc") is None:  # then Node must be able to take the types off by itself
        probe = subprocess.run(["node", "--experimental-strip-types", "-e", "const x: number = 1"], capture_output=True)
        return probe.returncode == 0
    return node_version() >= (14, 0)


def same(expected, got):
    """Equal, but for the last digits of the numbers with decimals: the other
    languages compute with more of them than C's float."""
    if expected == got:
        return True
    a, b = expected.split("\n"), got.split("\n")
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if x == y:
            continue
        p, q = x.split(), y.split()
        if len(p) != len(q):
            return False
        for s, t in zip(p, q):
            if s == t:
                continue
            try:
                u, v = float(s), float(t)
            except ValueError:
                return False
            if "." not in s or abs(u - v) > 1e-4 * max(1.0, abs(u), abs(v)):
                return False
    return True


def ends(path, options):
    """Some systems go on for ever: there is nothing to compare."""
    base = path[: -len(".plike")]
    stdin = open(base + ".in") if os.path.exists(base + ".in") else subprocess.DEVNULL
    try:
        result = subprocess.run([tool, "exec", "--steps=3000000", *options, path], stdin=stdin, capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        return False
    return result.returncode == 0


# The examples that some language cannot say, and why.
REFUSED = {
}


def cases():
    found = []
    for folder, pattern in (("programs", r"\.plike$"), ("targets", r"\.plike$"), ("dialects", r"\.(psc|alg|por|pse)$")):
        path = os.path.join(here, folder)
        for name in sorted(os.listdir(path)) if os.path.isdir(path) else []:
            if not re.search(pattern, name):
                continue
            stem = name if folder == "dialects" else name[: -len(".plike")]
            base = os.path.join(path, stem)
            options = open(base + ".opts").read().split() if os.path.exists(base + ".opts") else []
            if os.path.exists(base + ".status") or "-b" in options or "--bounds-check" in options:
                continue  # how a program fails is not the same everywhere
            if word and word not in name:
                continue
            refused = open(base + ".not").read().split() if os.path.exists(base + ".not") else []
            test = os.path.exists(base + ".test")
            found.append((folder, name, os.path.join(path, name), base, options, refused, test))
    # the examples: their tests, and what their main prints
    examples = os.path.join(here, "..", "examples")
    for folder in ["."] + sorted(d for d in os.listdir(examples) if os.path.isdir(os.path.join(examples, d))):
        for name in sorted(os.listdir(os.path.join(examples, folder))):
            path = os.path.normpath(os.path.join(examples, folder, name))
            if not name.endswith(".plike") or (word and word not in name):
                continue
            text = open(path).read()
            options = ["--indexing=zero"] if "plike run --indexing=zero" in text else []
            refused = REFUSED.get(os.path.join(folder, name).lstrip("./"), "").split()
            label = "examples" if folder == "." else "examples/" + folder
            if re.search(r"^\s*test\s+\"", text, re.M):
                found.append((label, name + " tests", path, path[: -len(".plike")], options, refused, True))
            if re.search(r"^\s*procedure\s+main\b", text, re.M | re.I) and ends(path, options):
                found.append((label, name, path, path[: -len(".plike")], options, refused, False))
    return found


def here_it_does(path, base, test, options):
    """What the program does without being translated: (output, exit status)."""
    stdin = open(base + ".in") if os.path.exists(base + ".in") else subprocess.DEVNULL
    result = subprocess.run([tool, "test" if test else "exec", *options, path], stdin=stdin, capture_output=True, text=True,
                            timeout=120)
    return result.stdout, result.returncode


def run(job):
    language, (folder, name, path, base, options, refused, test) = job
    label = f"{folder}/{name} ({language})"
    command = [tool, "test" if test else "run", "--to=" + language, *options, path]
    stdin = open(base + ".in") if os.path.exists(base + ".in") else subprocess.DEVNULL
    if folder.startswith("examples"):  # compared with what happens here
        expected, status = here_it_does(path, base, test, options)
        try:
            result = subprocess.run(command, stdin=stdin, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            return label, "timeout"
        if language in refused:
            ok = result.returncode == 1 and "error:" in result.stderr and "compiler failed" not in result.stderr
            return label, None if ok else "should have been refused:\n" + (result.stderr or result.stdout)[:1500]
        if result.returncode != status:
            return label, f"exit status {result.returncode}, here {status}\n" + result.stderr[:3000] + result.stdout[:2000]
        if not same(expected, result.stdout):
            return label, "here:\n" + expected[:1200] + "\ntranslated:\n" + result.stdout[:1200]
        return label, None
    try:
        result = subprocess.run(command, stdin=stdin, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return label, "timeout"
    if language in refused:
        ok = result.returncode == 1 and "error:" in result.stderr and "compiler failed" not in result.stderr
        return label, None if ok else "should have been refused:\n" + (result.stderr or result.stdout)[:1500]
    expected = open(base + (".test" if test else ".out")).read()
    if result.returncode != (1 if test and "✗" in expected else 0):
        return label, f"exit status {result.returncode}\n" + result.stderr[:3000] + result.stdout[:2000]
    if not same(expected, result.stdout):
        return label, "expected:\n" + expected[:1200] + "\ngot:\n" + result.stdout[:1200]
    return label, None


languages = [l for l in wanted if available(l)]
for missing in [l for l in wanted if l not in languages]:
    print(f"targets: {missing} skipped: its tools are not installed")
jobs = [(l, c) for c in cases() for l in languages]
failed = 0
with ThreadPoolExecutor(os.cpu_count()) as pool:
    for label, problem in pool.map(run, jobs):
        if problem:
            failed += 1
            print("FAIL " + label)
            print("    " + problem.replace("\n", "\n    ")[:4000 if verbose else 900])
print(f"targets: {len(jobs)} translations run ({', '.join(languages)}), {failed} failed")
sys.exit(bool(failed))
