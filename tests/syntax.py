#!/usr/bin/env python3
"""Every combination of the ways to write parameters, variables and endings.

The pseudocode this translator is for has no fixed syntax, so accepting many
spellings is its purpose. Each program must translate to C that compiles.
"""
import itertools
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

os.environ["PSEUDOSCOPE_LANG"] = "en"  # what is compared is in English, wherever the tests run

PLIKE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "bin/pseudoscope")
CC = os.environ.get("CC", "cc")

MODES = ["", "in ", "in: ", "out ", "out: ", "in/out ", "in/out: ", "inout ",
         "IN ", "In/Out: ", "in out "]
TYPES = ["integer", "real", "logical", "character"]
SIZED = ["x[k]: array of {t}", "x[1..k]: array of {t}", "x[k][k]: array of {t}",
         "x[k,k]: array of {t}", "x: array[k] of {t}", "x: array[1..k] of {t}",
         "x: array [1..k, 1..k] of {t}", "x: array[k][k] of {t}",
         "x(k): array of {t}", "x(1..k): array of {t}", "x: array(k) of {t}",
         "x[k][k]: 2d array of {t}", "x: 2d array[k,k] of {t}", "x[k]: {t}",
         "x: array[k] of *{t}", "x: array[k] of array[k] of {t}"]
UNSIZED = ["x: array of {t}", "x[]: array of {t}"]
SCALAR = ["x: {t}", "x {t}", "x :{t}", "*x: {t}", "x: {t}*", "x: {t} **", "x: *{t}"]
ENDINGS = {"procedure": ["end\nend f", "end\nendprocedure", "end f", "endprocedure",
                         "END\nEND F;", "end\nend"],
           "function": ["end\nend f", "end\nendfunction", "end f", "endfunction"]}


def program(params, declarations="", kind="procedure", ret="", end="end f"):
    return f"{kind} f({params}){ret}\n{declarations}begin\n{end}\n"


def programs():
    for mode, form, t in itertools.product(MODES, SIZED + UNSIZED + SCALAR, TYPES):
        x = form.format(t=t)
        yield program(f"in k: integer, {mode}{x}")
        yield program(f"{mode}{x}; in k: integer")
        yield program(f"in: k; {mode}{x}", "var k: integer\n")
    for mode, form, t in itertools.product(MODES, SIZED + UNSIZED + SCALAR, TYPES):
        if form == "x {t}":
            continue
        x = form.format(t=t)
        yield program(f"{mode}x, k", f"var k: integer\nvar {x}\n")
        yield program(f"k, {mode}x", f"    k: integer;\n    {x};\n")
    for var, form, t, sep in itertools.product(["var ", "var: ", "VAR "], SIZED + SCALAR, TYPES,
                                               ["\n", ";\n", "; q: real\n", ";\nvar q: real;\n"]):
        if form == "x {t}":
            continue
        yield program("in k: integer", var + form.format(t=t) + sep)
        if form in SCALAR or "x:" in form:  # otherwise w would have no size
            yield program("in k: integer", var + "w, " + form.format(t=t) + sep)
        else:
            yield program("in k: integer", var + ("w[k][k], " if "2d" in form else "w[k], ") + form.format(t=t) + sep)
    for kind, endings in ENDINGS.items():
        for end in endings:
            yield program("", kind=kind, ret=": real" if kind == "function" else "", end=end)
    for t, end in itertools.product(TYPES, ENDINGS["function"]):
        yield f"{t} function f()\nbegin\n{end}\n"
        yield program("", f"var f: {t}\n", "function", end=end)


def check(text):
    with tempfile.NamedTemporaryFile("w+", suffix=".c") as output:
        result = subprocess.run([PLIKE, "-q", "-", "-o", output.name], input=text,
                                capture_output=True, text=True)
        if result.returncode == 0:
            result = subprocess.run([CC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-Wno-unused", "-Wno-unused-parameter",
                                     "-Wno-uninitialized", "-fsyntax-only",
                                     output.name], capture_output=True, text=True)
    return None if result.returncode == 0 else f"{text}\n{result.stderr}"


cases = sorted(set(programs()))
with ThreadPoolExecutor(os.cpu_count()) as pool:
    failures = [f for f in pool.map(check, cases) if f]
for failure in failures[:5]:
    print(failure)
print(f"syntax: {len(cases)} programs, {len(failures)} failed")
sys.exit(bool(failures))
