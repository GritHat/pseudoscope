#include "plike/target.h"

/* What a translated program brings along: only what it uses. */

static void cpp(unsigned n, long long seed, StrBuf *out) {
    strbuf_puts(out, "#include <iostream>\n#include <string>\n#include <vector>\n");
    if (n & N_MATH) strbuf_puts(out, "#include <cmath>\n");
    if (n & (N_TEXT | N_INPUT | N_FAIL | N_SIM)) strbuf_puts(out, "#include <cstdio>\n#include <cstdlib>\n");
    if (n & N_SIM) strbuf_puts(out, "#include <any>\n#include <functional>\n");
    strbuf_puts(out, "\n");
    if (n & (N_INPUT | N_FAIL))
        strbuf_puts(out,
            "[[noreturn]] static void fail(const char *message, int line) {\n"
            "    std::cout.flush();\n"
            "    std::cerr << \"runtime error at line \" << line << \": \" << message << \"\\n\";\n"
            "    std::exit(1);\n"
            "}\n\n");
    if (n & N_INPUT)
        strbuf_puts(out,
            "template <class T> static void read(T &x, int line) {\n"
            "    if (!(std::cin >> x)) fail(\"invalid input\", line);\n"
            "}\n\n"
            "static void read(bool &x, int line) {\n"
            "    int n;\n"
            "    read(n, line);\n"
            "    x = n != 0;\n"
            "}\n\n"
            "static void read(std::string &x, int line) {\n"
            "    if (!std::getline(std::cin >> std::ws, x)) fail(\"invalid input\", line);\n"
            "}\n\n");
    if (n & N_TEXT)
        strbuf_puts(out,
            "// a number as short as it can be written\n"
            "static std::string text(double x) {\n"
            "    char buffer[40];\n"
            "    std::snprintf(buffer, sizeof buffer, \"%.10g\", x);\n"
            "    return buffer;\n"
            "}\n\n");
    if (n & N_CASE)
        strbuf_puts(out,
            "static std::string upper(std::string s) {\n"
            "    for (char &c : s) c = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;\n"
            "    return s;\n"
            "}\n\n"
            "static std::string lower(std::string s) {\n"
            "    for (char &c : s) c = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;\n"
            "    return s;\n"
            "}\n\n"
            "// from one position up to another, not included\n"
            "static std::string substring(const std::string &s, int from, int to) {\n"
            "    int length = (int)s.size();\n"
            "    if (from < 0) from = 0;\n"
            "    if (to > length) to = length;\n"
            "    return to > from ? s.substr((size_t)from, (size_t)(to - from)) : std::string();\n"
            "}\n\n");
    if (n & (N_RANDOM | N_SIM)) {
        strbuf_printf(out, "static unsigned seed_ = %uu;\n\n", (unsigned)seed);
        strbuf_puts(out,
            "// the same numbers every time: a program can be run again and studied\n"
            "static int random(int lo, int hi) {\n"
            "    unsigned n = 0;\n"
            "    if (hi <= lo) return lo;\n"
            "    for (int i = 0; i < 2; i++) {\n"
            "        seed_ = seed_ * 1103515245u + 12345u;\n"
            "        n = n << 15 | (seed_ >> 16 & 0x7fff);\n"
            "    }\n"
            "    return (int)(lo + (long long)(n % (unsigned)((long long)hi - lo + 1)));\n"
            "}\n\n");
    }
}

static void java(unsigned n, long long seed, StrBuf *out) {
    if (n & (N_INPUT | N_FAIL | N_SIM))
        strbuf_puts(out,
            "static void fail(String message, int line) {\n"
            "    System.out.flush();\n"
            "    System.err.println(\"runtime error at line \" + line + \": \" + message);\n"
            "    System.exit(1);\n"
            "}\n\n");
    if (n & N_INPUT)
        strbuf_puts(out,
            "static final java.io.InputStream input = new java.io.BufferedInputStream(System.in);\n\n"
            "static int nextChar() {\n"
            "    try {\n"
            "        return input.read();\n"
            "    } catch (java.io.IOException e) {\n"
            "        return -1;\n"
            "    }\n"
            "}\n\n"
            "static int firstChar() {\n"
            "    int c = nextChar();\n"
            "    while (c == ' ' || c == '\\n' || c == '\\t' || c == '\\r') c = nextChar();\n"
            "    return c;\n"
            "}\n\n"
            "static String token(int line) {\n"
            "    StringBuilder s = new StringBuilder();\n"
            "    for (int c = firstChar(); c > ' '; c = nextChar()) s.append((char) c);\n"
            "    if (s.length() == 0) fail(\"invalid input\", line);\n"
            "    return s.toString();\n"
            "}\n\n"
            "static int readInt(int line) {\n"
            "    try {\n"
            "        return Integer.parseInt(token(line));\n"
            "    } catch (NumberFormatException e) {\n"
            "        fail(\"invalid input\", line);\n"
            "        return 0;\n"
            "    }\n"
            "}\n\n"
            "static double readReal(int line) {\n"
            "    try {\n"
            "        return Double.parseDouble(token(line));\n"
            "    } catch (NumberFormatException e) {\n"
            "        fail(\"invalid input\", line);\n"
            "        return 0;\n"
            "    }\n"
            "}\n\n"
            "static boolean readBool(int line) {\n"
            "    return readInt(line) != 0;\n"
            "}\n\n"
            "static char readChar(int line) {\n"
            "    int c = firstChar();\n"
            "    if (c < 0) fail(\"invalid input\", line);\n"
            "    return (char) c;\n"
            "}\n\n"
            "static String readText(int line) {\n"
            "    StringBuilder s = new StringBuilder();\n"
            "    for (int c = firstChar(); c >= 0 && c != '\\n'; c = nextChar()) s.append((char) c);\n"
            "    if (s.length() == 0) fail(\"invalid input\", line);\n"
            "    return s.toString();\n"
            "}\n\n");
    if (n & N_TEXT)
        strbuf_puts(out,
            "// a number as short as it can be written\n"
            "static String text(double x) {\n"
            "    String s = String.format(java.util.Locale.ROOT, \"%.10g\", x);\n"
            "    int e = s.indexOf('e');\n"
            "    String digits = e < 0 ? s : s.substring(0, e);\n"
            "    if (digits.contains(\".\")) digits = digits.replaceAll(\"0+$\", \"\").replaceAll(\"\\\\.$\", \"\");\n"
            "    return e < 0 ? digits : digits + s.substring(e);\n"
            "}\n\n");
    if (n & (N_RANDOM | N_SIM)) {
        strbuf_printf(out, "static int seed = %d;\n\n", (int)(unsigned)seed);
        strbuf_puts(out,
            "// the same numbers every time: a program can be run again and studied\n"
            "static int random(int lo, int hi) {\n"
            "    if (hi <= lo) return lo;\n"
            "    int n = 0;\n"
            "    for (int i = 0; i < 2; i++) {\n"
            "        seed = seed * 1103515245 + 12345;\n"
            "        n = n << 15 | (seed >>> 16 & 0x7fff);\n"
            "    }\n"
            "    return (int) (lo + n % ((long) hi - lo + 1));\n"
            "}\n\n");
    }
}

static void script(Lang lang, unsigned n, long long seed, StrBuf *out) {
    bool ts = lang == LANG_TS;
    /* TypeScript: what Node gives, said without needing its type definitions */
    if (ts) strbuf_puts(out, "export {}; // a module: its names are its own\n");
    if (ts && (n & (N_INPUT | N_FAIL | N_SIM | N_PROCESS))) strbuf_puts(out, "declare const process: {\n    exit(code: number): never;\n    stdout: { write(text: string): void };\n    argv: string[];\n    getBuiltinModule(name: string): any;\n}; // what Node gives\n");
    if (ts) strbuf_puts(out, "\n");
    if (n & N_INPUT) strbuf_puts(out, ts ? "const fs = process.getBuiltinModule(\"node:fs\");\n\n" : "const fs = require(\"node:fs\");\n\n");
    if (n & (N_INPUT | N_FAIL | N_SIM))
        strbuf_printf(out,
            "function fail(message%s, line%s)%s {\n"
            "    console.error(`runtime error at line ${line}: ${message}`);\n"
            "    process.exit(1);\n"
            "}\n\n", ts ? ": string" : "", ts ? ": number" : "", ts ? ": never" : "");
    if (n & N_INPUT) {
        const char *num = ts ? ": number" : "", *str = ts ? ": string" : "", *boolean = ts ? ": boolean" : "";
        strbuf_printf(out,
            "let input%s = \"\", inputAt = -1;\n\n"
            "function skipBlanks()%s {\n"
            "    if (inputAt < 0) {\n"
            "        input = fs.readFileSync(0, \"utf8\");\n"
            "        inputAt = 0;\n"
            "    }\n"
            "    while (inputAt < input.length && \" \\n\\t\\r\".includes(input[inputAt])) inputAt++;\n"
            "}\n\n"
            "function token(line%s)%s {\n"
            "    skipBlanks();\n"
            "    const start = inputAt;\n"
            "    while (inputAt < input.length && !\" \\n\\t\\r\".includes(input[inputAt])) inputAt++;\n"
            "    if (inputAt === start) fail(\"invalid input\", line);\n"
            "    return input.slice(start, inputAt);\n"
            "}\n\n"
            "function readInt(line%s)%s {\n"
            "    const n = Number(token(line));\n"
            "    if (!Number.isInteger(n)) fail(\"invalid input\", line);\n"
            "    return n;\n"
            "}\n\n"
            "function readReal(line%s)%s {\n"
            "    const x = Number(token(line));\n"
            "    if (Number.isNaN(x)) fail(\"invalid input\", line);\n"
            "    return x;\n"
            "}\n\n"
            "function readBool(line%s)%s {\n"
            "    return readInt(line) !== 0;\n"
            "}\n\n"
            "function readChar(line%s)%s {\n"
            "    skipBlanks();\n"
            "    if (inputAt >= input.length) fail(\"invalid input\", line);\n"
            "    return input[inputAt++];\n"
            "}\n\n"
            "function readText(line%s)%s {\n"
            "    skipBlanks();\n"
            "    const start = inputAt;\n"
            "    while (inputAt < input.length && input[inputAt] !== \"\\n\") inputAt++;\n"
            "    if (inputAt === start) fail(\"invalid input\", line);\n"
            "    return input.slice(start, inputAt);\n"
            "}\n\n",
            str, ts ? ": void" : "", num, str, num, num, num, num, num, boolean, num, str, num, str);
    }
    if (n & N_TEXT)
        strbuf_printf(out,
            "// a number as short as it can be written\n"
            "function text(x%s)%s {\n"
            "    const [digits, exponent] = x.toPrecision(10).split(\"e\");\n"
            "    const short = digits.includes(\".\") ? digits.replace(/0+$/, \"\").replace(/\\.$/, \"\") : digits;\n"
            "    return exponent === undefined ? short : `${short}e${exponent}`;\n"
            "}\n\n", ts ? ": number" : "", ts ? ": string" : "");
    if (n & (N_RANDOM | N_SIM)) {
        strbuf_printf(out, "let seed = %u;\n\n", (unsigned)seed);
        strbuf_printf(out,
            "// the same numbers every time: a program can be run again and studied\n"
            "function random(lo%s, hi%s)%s {\n"
            "    if (hi <= lo) return lo;\n"
            "    let n = 0;\n"
            "    for (let i = 0; i < 2; i++) {\n"
            "        seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;\n"
            "        n = (n << 15) | ((seed >>> 16) & 0x7fff);\n"
            "    }\n"
            "    return lo + (n %% (hi - lo + 1));\n"
            "}\n\n", ts ? ": number" : "", ts ? ": number" : "", ts ? ": number" : "");
    }
}

static void python(unsigned n, long long seed, StrBuf *out) {
    bool any = false;
    if (n & N_SIM) strbuf_puts(out, "from __future__ import annotations\n\n");
    if (n & N_COPY) strbuf_puts(out, "import copy\n"), any = true;
    if (n & N_MATH) strbuf_puts(out, "import math\n"), any = true;
    if (n & (N_INPUT | N_FAIL | N_SIM)) strbuf_puts(out, "import sys\n"), any = true;
    if (n & N_ABC) strbuf_puts(out, "from abc import ABC, abstractmethod\n"), any = true;
    if (any) strbuf_puts(out, "\n\n");
    if (n & (N_INPUT | N_FAIL | N_SIM))
        strbuf_puts(out,
            "def fail(message, line):\n"
            "    sys.stdout.flush()\n"
            "    print(f\"runtime error at line {line}: {message}\", file=sys.stderr)\n"
            "    sys.exit(1)\n\n\n");
    if (n & N_INPUT)
        strbuf_puts(out,
            "_input = None\n"
            "_at = 0\n\n\n"
            "def _skip_blanks():\n"
            "    global _input, _at\n"
            "    if _input is None:\n"
            "        _input = sys.stdin.read()\n"
            "    while _at < len(_input) and _input[_at] in \" \\n\\t\\r\":\n"
            "        _at += 1\n\n\n"
            "def _token(line):\n"
            "    global _at\n"
            "    _skip_blanks()\n"
            "    start = _at\n"
            "    while _at < len(_input) and _input[_at] not in \" \\n\\t\\r\":\n"
            "        _at += 1\n"
            "    if _at == start:\n"
            "        fail(\"invalid input\", line)\n"
            "    return _input[start:_at]\n\n\n"
            "def read_int(line):\n"
            "    try:\n"
            "        return int(_token(line))\n"
            "    except ValueError:\n"
            "        fail(\"invalid input\", line)\n\n\n"
            "def read_real(line):\n"
            "    try:\n"
            "        return float(_token(line))\n"
            "    except ValueError:\n"
            "        fail(\"invalid input\", line)\n\n\n"
            "def read_bool(line):\n"
            "    return read_int(line) != 0\n\n\n"
            "def read_char(line):\n"
            "    global _at\n"
            "    _skip_blanks()\n"
            "    if _at >= len(_input):\n"
            "        fail(\"invalid input\", line)\n"
            "    _at += 1\n"
            "    return _input[_at - 1]\n\n\n"
            "def read_text(line):\n"
            "    global _at\n"
            "    _skip_blanks()\n"
            "    start = _at\n"
            "    while _at < len(_input) and _input[_at] != \"\\n\":\n"
            "        _at += 1\n"
            "    if _at == start:\n"
            "        fail(\"invalid input\", line)\n"
            "    return _input[start:_at]\n\n\n");
    if (n & N_TEXT)
        strbuf_puts(out,
            "def text(x):\n"
            "    \"\"\"A number as short as it can be written.\"\"\"\n"
            "    return \"%.10g\" % x\n\n\n");
    if (n & N_MOD)
        strbuf_puts(out,
            "def mod(a, b):\n"
            "    \"\"\"The remainder with the sign of a, as in the pseudocode (% follows b).\"\"\"\n"
            "    return a - b * int(a / b)\n\n\n");
    if (n & (N_RANDOM | N_SIM)) {
        strbuf_printf(out, "_seed = %u\n\n\n", (unsigned)seed);
        strbuf_puts(out,
            "def random(lo, hi):\n"
            "    \"\"\"The same numbers every time: a program can be run again and studied.\"\"\"\n"
            "    global _seed\n"
            "    if hi <= lo:\n"
            "        return lo\n"
            "    n = 0\n"
            "    for _ in range(2):\n"
            "        _seed = (_seed * 1103515245 + 12345) & 0xFFFFFFFF\n"
            "        n = (n << 15) | ((_seed >> 16) & 0x7FFF)\n"
            "    return lo + n % (hi - lo + 1)\n\n\n");
    }
}

static void rust(unsigned n, long long seed, StrBuf *out) {
    strbuf_puts(out, "#![allow(unknown_lints, unused, non_snake_case, non_camel_case_types, non_upper_case_globals, static_mut_refs, unused_unsafe, dangerous_implicit_autorefs)]\n"
                     "#![allow(clippy::all)]\n\n");
    if (n & N_INPUT) strbuf_puts(out, "use std::io::Read;\n\n");
    if (n & (N_INPUT | N_FAIL | N_SIM))
        strbuf_puts(out,
            "fn fail(message: &str, line: i32) -> ! {\n"
            "    eprintln!(\"runtime error at line {}: {}\", line, message);\n"
            "    std::process::exit(1)\n"
            "}\n\n");
    if (n & N_INPUT)
        strbuf_puts(out,
            "thread_local! {\n"
            "    static INPUT: std::cell::RefCell<(Vec<u8>, usize, bool)> = std::cell::RefCell::new((Vec::new(), 0, false));\n"
            "}\n\n"
            "// the next piece of the input: a word, or up to the end of the line\n"
            "fn token(line: i32, whole_line: bool, one: bool) -> String {\n"
            "    INPUT.with(|cell| {\n"
            "        let mut input = cell.borrow_mut();\n"
            "        if !input.2 {\n"
            "            std::io::stdin().read_to_end(&mut input.0).ok();\n"
            "            input.2 = true;\n"
            "        }\n"
            "        while input.1 < input.0.len() && input.0[input.1].is_ascii_whitespace() {\n"
            "            input.1 += 1;\n"
            "        }\n"
            "        let start = input.1;\n"
            "        while input.1 < input.0.len() {\n"
            "            let c = input.0[input.1];\n"
            "            if c == b'\\n' || (!whole_line && c.is_ascii_whitespace()) || (one && input.1 > start) {\n"
            "                break;\n"
            "            }\n"
            "            input.1 += 1;\n"
            "        }\n"
            "        if input.1 == start {\n"
            "            fail(\"invalid input\", line);\n"
            "        }\n"
            "        String::from_utf8_lossy(&input.0[start..input.1]).to_string()\n"
            "    })\n"
            "}\n\n"
            "fn read_int(line: i32) -> i32 {\n"
            "    token(line, false, false).parse().unwrap_or_else(|_| fail(\"invalid input\", line))\n"
            "}\n\n"
            "fn read_real(line: i32) -> f64 {\n"
            "    token(line, false, false).parse().unwrap_or_else(|_| fail(\"invalid input\", line))\n"
            "}\n\n"
            "fn read_bool(line: i32) -> bool {\n"
            "    read_int(line) != 0\n"
            "}\n\n"
            "fn read_char(line: i32) -> char {\n"
            "    token(line, false, true).chars().next().unwrap_or(' ')\n"
            "}\n\n"
            "fn read_text(line: i32) -> String {\n"
            "    token(line, true, false)\n"
            "}\n\n");
    if (n & N_TEXT)
        strbuf_puts(out,
            "// a number as short as it can be written\n"
            "fn text(x: f64) -> String {\n"
            "    if x == 0.0 {\n"
            "        return String::from(\"0\");\n"
            "    }\n"
            "    let exponent = x.abs().log10().floor() as i32;\n"
            "    if exponent < -4 || exponent >= 10 {\n"
            "        return format!(\"{:e}\", x);\n"
            "    }\n"
            "    let s = format!(\"{:.*}\", (9 - exponent).max(0) as usize, x);\n"
            "    if s.contains('.') {\n"
            "        s.trim_end_matches('0').trim_end_matches('.').to_string()\n"
            "    } else {\n"
            "        s\n"
            "    }\n"
            "}\n\n");
    if (n & N_CASE)
        strbuf_puts(out,
            "// from one position up to another, not included\n"
            "fn substring(s: &str, from: i32, to: i32) -> String {\n"
            "    let from = from.max(0) as usize;\n"
            "    s.chars().skip(from).take((to.max(0) as usize).saturating_sub(from)).collect()\n"
            "}\n\n");
    if (n & (N_RANDOM | N_SIM)) {
        strbuf_printf(out, "thread_local! {\n    static SEED: std::cell::Cell<u32> = std::cell::Cell::new(%u);\n}\n\n", (unsigned)seed);
        strbuf_puts(out,
            "// the same numbers every time: a program can be run again and studied\n"
            "fn random(lo: i32, hi: i32) -> i32 {\n"
            "    if hi <= lo {\n"
            "        return lo;\n"
            "    }\n"
            "    let mut n: u32 = 0;\n"
            "    for _ in 0..2 {\n"
            "        let seed = SEED.with(|s| s.get()).wrapping_mul(1103515245).wrapping_add(12345);\n"
            "        SEED.with(|s| s.set(seed));\n"
            "        n = n << 15 | (seed >> 16 & 0x7fff);\n"
            "    }\n"
            "    (lo as i64 + (n as i64) % (hi as i64 - lo as i64 + 1)) as i32\n"
            "}\n\n");
    }
}

void emit_prelude(Lang lang, unsigned needs, long long seed, StrBuf *out) {
    switch (lang) {
        case LANG_CPP: cpp(needs, seed, out); break;
        case LANG_JAVA: java(needs, seed, out); break;
        case LANG_RUST: rust(needs, seed, out); break;
        case LANG_PYTHON: python(needs, seed, out); break;
        case LANG_TS: case LANG_JS: script(lang, needs, seed, out); break;
        default: break;
    }
}
