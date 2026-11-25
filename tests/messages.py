#!/usr/bin/env python3
"""Every message of the translator must have an Italian form."""
import glob
import re
import sys

table = set(re.findall(r'^    \{"((?:[^"\\]|\\.)*)",', open("src/util/italian.c").read(), re.M))
missing = []
for path in glob.glob("src/front/*.c") + glob.glob("src/sema/*.c") + glob.glob("src/uml/*.c") + glob.glob("src/front/*.inc"):
    text = open(path).read()
    for call in re.finditer(r"diag_(?:error|warning)\(", text):
        rest = text[call.end():]
        literals = re.match(r'[^;]*?((?:"(?:[^"\\]|\\.)*"\s*)+)', rest)
        message = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', literals.group(1)))
        if message not in table:
            missing.append((path, message))
    for expected in re.findall(r'(?:syntax_error|expect)\(p, [^;]*?"((?:[^"\\]|\\.)*)"', text):
        if not expected.startswith("'") and expected not in table:
            missing.append((path, expected))
for path, message in missing:
    print(f"{path}: no Italian for \"{message}\"")
print(f"messages: {len(table)} translated, {len(missing)} missing")
sys.exit(bool(missing))
