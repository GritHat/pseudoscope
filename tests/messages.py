#!/usr/bin/env python3
"""Every text the program shows must be in every catalog of lang/.

    messages.py              reports what a catalog lacks, and what it has that no longer exists
    messages.py --update     rewrites the catalogs in the order of the sources: new texts get an
                             empty translation, the ones no longer used go; makes the template too
"""
import glob
import os
import re
import sys

STRING = r'"(?:[^"\\]|\\.)*"'
RUN = rf"((?:{STRING}\s*)+)"


def text(run):
    """What a run of adjacent C strings says, written as in the source."""
    return "".join(s[1:-1] for s in re.findall(STRING, run))


def extract():
    """(context, text, plural) -> files, in the order they are met."""
    found = {}

    def add(path, context, one, plural=""):
        found.setdefault((context, one, plural), [])
        if path not in found[context, one, plural]:
            found[context, one, plural].append(path)

    for path in sorted(glob.glob("src/**/*.c", recursive=True) + glob.glob("src/**/*.inc", recursive=True) + glob.glob("src/**/*.h", recursive=True)):
        source = open(path, encoding="utf-8").read()
        for m in re.finditer(rf"\b(?:T|N_|tr)\(\s*{RUN}", source):
            add(path, "", text(m.group(1)))
        for m in re.finditer(rf"\bTC\(\s*{RUN},\s*{RUN}", source):
            add(path, text(m.group(1)), text(m.group(2)))
        for m in re.finditer(rf"\bTN\(\s*{RUN},\s*{RUN}", source):
            add(path, "", text(m.group(1)), text(m.group(2)))
        # the messages of the translator and of the machine are said in the language chosen where they are shown
        for m in re.finditer(rf"\bdiag_(?:error|warning)\([^;\"]*?{RUN}", source):
            add(path, "", text(m.group(1)))
        for m in re.finditer(rf"\b(?:fail|warn)\(vm,\s*{RUN}|\bstep\(options,\s*{RUN}", source):
            add(path, "", text(m.group(1) or m.group(2)))
        for m in re.finditer(rf"\b(?:syntax_error|expect)\(p, [^;\"]*?{RUN}", source):
            if re.search(r"[A-Za-z]", re.sub(r"'[^']*'", "", text(m.group(1)))):  # more than the name of a symbol
                add(path, "", text(m.group(1)))
    return found


def read(path):
    """A catalog: its header, and (context, text, plural) -> the translations."""
    header, entries = "", {}
    fields, into = {}, None

    def close():
        nonlocal header
        if "msgid" in fields:
            if fields["msgid"] == "":
                header = fields.get("msgstr", "")
            else:
                key = (fields.get("msgctxt", ""), fields["msgid"], fields.get("msgid_plural", ""))
                entries[key] = (fields.get("msgstr", fields.get("msgstr[0]", "")), fields.get("msgstr[1]", ""))
        fields.clear()

    if not os.path.exists(path):
        return header, entries
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            close()
            into = None
        elif line.startswith("#"):
            continue
        else:
            m = re.match(rf"(msgctxt|msgid_plural|msgid|msgstr(?:\[\d\])?)\s+({STRING})$", line)
            if m:
                into = m.group(1)
                fields[into] = m.group(2)[1:-1]
            elif re.match(rf"{STRING}$", line) and into:
                fields[into] += line[1:-1]
            else:
                raise SystemExit(f"{path}: not understood: {line}")
    close()
    return header, entries


def write(path, header, entries, found):
    with open(path, "w", encoding="utf-8") as out:
        out.write('msgid ""\nmsgstr ""\n')
        for line in re.findall(r".*?\\n|.+$", header):
            out.write(f'"{line}"\n')
        for (context, one, plural), files in found.items():
            first, second = entries.get((context, one, plural), ("", ""))
            out.write("\n#: " + " ".join(files) + "\n")
            if context:
                out.write(f'msgctxt "{context}"\n')
            out.write(f'msgid "{one}"\n')
            if plural:
                out.write(f'msgid_plural "{plural}"\nmsgstr[0] "{first}"\nmsgstr[1] "{second}"\n')
            else:
                out.write(f'msgstr "{first}"\n')


def main():
    found = extract()
    update = sys.argv[1:] == ["--update"]
    if update:
        head = "Content-Type: text/plain; charset=UTF-8\\nPlural-Forms: nplurals=2; plural=(n != 1);\\n"
        write("lang/pseudoscope.pot", "Language: \\nX-Language-Name: \\n" + head, {}, found)
    problems = 0
    for path in sorted(glob.glob("lang/*.po")):
        header, entries = read(path)
        if update:
            write(path, header, entries, found)
        missing = [k for k in found if not entries.get(k, ("", ""))[0] or (k[2] and not entries[k][1])]
        gone = [k for k in entries if k not in found]
        for context, one, plural in missing:
            print(f'{path}: no translation of "{one}"' + (f" ({context})" if context else "") + f"   [{found[context, one, plural][0]}]")
        if not update:
            for context, one, plural in gone:
                print(f'{path}: "{one}" is no longer in the program')
            problems += len(gone)
        problems += len(missing)
        print(f"{path}: {len(found) - len(missing)} of {len(found)} texts translated" + (f", {len(gone)} no longer used" if gone and not update else ""))
    sys.exit(bool(problems))


main()
