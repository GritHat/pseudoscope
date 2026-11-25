# The language

Keywords may be written in any case. Names are case-sensitive, but when a name is not
declared and one that differs only in case is, that one is used, with a warning.
Semicolons are optional. Comments are `// ...` and `/* ... */`.

## Programs

A file holds procedures, functions, global variables and record types, in any order;
they can be used before they are declared.

```pascal
procedure name(parameters)
    declarations
begin
    statements
end name
```

A body can be closed by `end name`, `endprocedure` (`endfunction`), `end` followed by
either of them, or `end end`.

A function gives its result type after the parameters, before `function`, or among its
declarations:

```pascal
function f(in x: real): integer
integer function f(in x: real)
function f(x)
    var x: real
    var f: integer
```

It returns the value assigned to its own name, or given to `return`. The name can be
read like any variable; followed by parentheses, it is a recursive call. A function
that is not in the file is a warning, and is declared `extern` in the output. `sqrt`,
`sin`, `cos`, `tan`, `exp`, `log`, `floor`, `ceil`, `abs`, `fabs` and `pow` come from
the C library.

## Parameters

| Mode | Spellings | Meaning |
|---|---|---|
| in | `in`, nothing | a copy |
| out | `out` | the caller's variable |
| in/out | `in/out`, `inout`, `in out` | the caller's variable |

Arrays are always the caller's. `mode:` with a colon holds up to the next mode or `;`,
without it for one parameter (and those sharing its type):

```pascal
(in a: integer, out b, c: real)      // b and c are out
(in: n, A; out: found, where)        // types in the body
(a, n)                               // both in
(in A[n][n]: array of real, in n: integer)
```

`out` parameters are pointers in C; they are dereferenced automatically. `@x` is the
pointer itself.

## Types

`integer` (C `int`), `real` (`float`), `logical` (`bool`), `character` (`char`),
pointers (`integer*`, `*integer`, `*x: integer`), arrays and records.

```pascal
var a: array[10] of integer
var b[10]: array of integer          // the same
var c: array[1..n, 0..m] of real
var d: array[3][4] of real
var e: 2d array[3,4] of real
var f(10): array of logical
```

`[lo..hi]` uses these bounds. `[n]` has n elements starting from 1, or from 0 with
`--indexing=zero`. Parameters may leave out the first size: `A[]`, `array of t`.

```pascal
type point : record
    x, y : real
    next : point*
end
```

`p.x` also works through a pointer, like `p->x`.

## Statements

```pascal
x := e
if c then ... elseif c then ... else ... endif
while c do ... endwhile
repeat ... until c
for i := a to b step s do ... endfor
begin ... end
return e
print(a, b)        // blanks in between, a new line at the end
read(a, b)
name(arguments)
var x: integer     // in the middle of a body
```

The limit and the step of a `for` are evaluated at every iteration. With a negative
step the loop counts down.

A `*` at the beginning of a line starts a new statement (`*p := 1`), unless
parentheses are open.

## Expressions

From the tightest:

| Operators | Notes |
|---|---|
| `a[i]` `a(i)` `f(x)` `.` `->` | |
| `-` `+` `not` `!` `~` `*p` `&x` `@x` | unary |
| `*` `/` `%` `mod` | `/` between integers drops the remainder |
| `+` `-` | |
| `<<` `>>` | |
| `<` `>` `<=` `>=` | |
| `=` `==` `eq` `equal` `equals`, `!=` `<>` `ne` `notequal` `notequals` | |
| `&` | |
| `^` | |
| `\|` | |
| `and` `&&` | |
| `or` `\|\|` | |

Word operators also exist between dots: `.and.`, `.not.`, `.eq.`, `.mod.`, `.true.`.

Numbers: `12`, `0xFF`, `0o17`, `017` (octal), `0b101`, `1.5`, `2.`, `1e-3`.
Text: `"string"`, `'c'`, with the C escapes.

## Changes from version 1

- `-o` is the output file; `--operators` replaces its old meaning.
- `--mixed-arrays` takes `true` or `false`. `--debug` is gone: see `ast` and `tokens`.
- Shifts bind tighter than comparisons, as in C.
- Ranges include both ends with `--indexing=zero` too, and `[n]` is exactly n elements.
- Programs that cannot work are refused: undeclared variables, wrong argument or index
  counts, local arrays without a size, and so on.
