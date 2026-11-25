#!/usr/bin/env python3
"""Mutates the test programs; the translator must never crash or hang.

Usage: fuzz.py PLIKE [ROUNDS] [SEED]. Build PLIKE with sanitizers (make fuzz).
"""
import glob
import os
import random
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

PLIKE = os.path.abspath(sys.argv[1])
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 1

here = os.path.dirname(os.path.abspath(__file__))
corpus = [open(f, "rb").read() for f in sorted(glob.glob(here + "/*/*.plike"))]
tokens = sorted(set(t for text in corpus for t in re.findall(rb"\w+|[^\w\s]+", text)))


def mutate(rng):
    text = rng.choice(corpus)
    parts = re.findall(rb"\w+|\s+|[^\w\s]", text)
    for _ in range(rng.randint(1, 6)):
        i = rng.randrange(len(parts))
        action = rng.randrange(6)
        if action == 0:
            del parts[i]
        elif action == 1:
            parts[i] = rng.choice(tokens)
        elif action == 2:
            parts.insert(i, rng.choice(tokens) + b" ")
        elif action == 3:
            j = rng.randrange(len(parts))
            parts[i], parts[j] = parts[j], parts[i]
        elif action == 4:
            parts.insert(i, bytes([rng.randrange(256)]))
        else:
            del parts[i:i + rng.randrange(40)]
        if not parts:
            parts = [b" "]
    return b"".join(parts)


KEYS = ["<up>", "<down>", "<left>", "<right>", "<right>", "<right>", "<enter>", "<enter>", "<esc>", "<tab>",
        "<space>", "<back>", "<home>", "<end>", "<pgup>", "<pgdn>", "r", "n", "u", "b", "v", "o", "c",
        "0", "1", "2", "3", "4", "?", "5", "x", "g"]


def run_ui(rng, text):
    if rng.randrange(2):
        text = rng.choice(corpus)  # as it is, to get to run it
    keys = "".join(rng.choice(KEYS) for _ in range(rng.randrange(60)))
    size = f"{rng.choice([20, 80, 81, 100, 160])}x{rng.choice([5, 24, 25, 40])}"
    with tempfile.NamedTemporaryFile(suffix=".plike") as program:
        program.write(text)
        program.flush()
        try:
            result = subprocess.run([PLIKE, "ui", program.name, "--keys=r" + keys, "--size=" + size,
                                     rng.choice(["--lang=it", "--lang=en"])], capture_output=True, timeout=120)
        except subprocess.TimeoutExpired:
            return text, "timeout with keys " + keys
    if result.returncode == 0:
        return None
    return text, f"keys {keys} size {size}\n" + result.stderr.decode(errors="replace")[:2500]


def run(i):
    rng = random.Random(SEED * 1000003 + i)
    text = mutate(rng)
    if rng.randrange(4) == 0:
        return run_ui(rng, text)
    args = [PLIKE, rng.choice(["translate", "translate", "exec", "exec", "ast", "tokens"]), "-"]
    args += rng.choice([[], ["-b"], ["--indexing=zero"], ["--assignment=equals"],
                        ["--operators=dotted"], ["--mixed-arrays=false"]])
    if args[1] == "exec":
        args += ["--steps=300000"]
        result = subprocess.run(args, input=text, capture_output=True, timeout=60)
        bad = b"Sanitizer" in result.stderr or b"runtime error:" in result.stderr or result.returncode < 0
        return (text, result.stderr.decode(errors="replace")[:1500]) if bad else None
    with tempfile.NamedTemporaryFile(suffix=".c") as output:
        try:
            result = subprocess.run(args + ["-o", output.name], input=text,
                                    capture_output=True, timeout=20)
        except subprocess.TimeoutExpired:
            return text, "timeout"
        if result.returncode not in (0, 1):
            return text, result.stderr.decode(errors="replace")[:1500]
        # What is accepted must be valid C.
        if result.returncode == 0 and args[1] == "translate":
            result = subprocess.run(["cc", "-std=gnu11", "-w", "-fsyntax-only", output.name],
                                    capture_output=True)
            if result.returncode:
                return text, "invalid C: " + result.stderr.decode(errors="replace")[:800]
    return None


with ThreadPoolExecutor(os.cpu_count()) as pool:
    failures = [f for f in pool.map(run, range(ROUNDS)) if f]
for text, message in failures[:3]:
    sys.stdout.write(text.decode(errors="replace") + "\n--- " + message + "\n\n")
print(f"fuzz: {ROUNDS} programs, {len(failures)} failures")
sys.exit(bool(failures))
