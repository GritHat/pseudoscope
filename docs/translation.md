# Translating

```sh
pseudoscope program.pscope -o program.c         # C, as always
pseudoscope program.pscope -o Program.java      # the language is that of the name
pseudoscope translate --to=python program.pscope
pseudoscope run --to=rust program.pscope        # translate, compile, execute
pseudoscope test --to=cpp program.pscope        # its tests, translated
```

| `--to` | Language | Run with | Classes |
|---|---|---|---|
| `c` | C | `cc` | no inheritance |
| `cpp` | C++17 | `c++` | several base classes, abstract classes |
| `java` | Java 16+ | `java` | one base class, interfaces |
| `rust` | Rust 2021 | `rustc` | interfaces only (traits) |
| `ts` | TypeScript | `tsc` and `node`, or `node` alone | one base class, interfaces |
| `js` | JavaScript | `node` | one base class |
| `python` | Python 3.9+ | `python3` | several base classes, abstract classes |

`--cc` (or `$CXX`, `$RUSTC`, `$JAVA`, `$NODE`, `$PYTHON`) names another compiler or
interpreter. In the interface `T` changes the language of the translation shown.

A language is never made to say what it cannot: the translation is refused, with the
line and the reason, rather than written wrong.

```
shapes.pscope:56:31: error: in Java a class extends one class only: 'Sticker' has both
'Circle' and 'Tagged' (C++ and Python allow it; or make one an interface)
```

## How things are written

| Pseudocode | C++ | Java | Rust | TypeScript, JavaScript | Python |
|---|---|---|---|---|---|
| `integer`, `real` | `int`, `double` | `int`, `double` | `i32`, `f64` | `number` | `int`, `float` |
| `array [lo..hi] of t` | `std::vector` | `t[]` | `Vec` | array | `list` |
| `record`, `class` | `struct` | `static class` | `struct`, `impl` | `class` | `class` |
| `t*` to a record | pointer | the object | raw pointer | the object, or `null` | the object, or `None` |
| `t*` to a number | pointer | refused | raw pointer | refused | refused |
| `out`, `in/out` number | `t &` | given back | given back | given back | given back |
| `print(x, y)` | `std::cout` | `printf` | `println!` | `console.log` | `print` |

Arrays always begin at 0 there, so `a[i]` of an array `[1..n]` becomes `a[i - 1]`. A
record assigned or passed by value is copied where the language would share it
(`.copy()`, `.clone()`, `copy.deepcopy`).

**Given back.** Only C and C++ have parameters by reference for plain values. In the
other languages a subprogram returns what it changed together with its result:

```pascal
procedure divide(in a, b: integer; out q, r: integer)
```
```python
def divide(a: int, b: int) -> tuple[int, int]:
    ...
    return q, r

q, r = divide(17, 5)
```

In Java the values travel in a `record`; in Rust and TypeScript in a tuple. Such a call
inside a larger expression is done first, into a variable; inside the condition of a
loop it is refused (it would have to be done again and again): write it on its own.

**Loops.** `for i := a to b` is `for (i = a; i <= b; i++)` where the language has it.
In Python and Rust it is `for i in range(a, b + 1)` and `for i in a..=b` when nothing
depends on `i` after the loop, and a `while` otherwise.

**Numbers.** Reals are `double` everywhere but in C (`float`): the last digits may
differ. The integer division and the remainder are those of C (towards zero; the sign
of the dividend), also in Python, where `//` and `%` are not.

**Rust.** Programs with global variables, pointers, components or tests are written
with `unsafe`, as C would do; the others are not.

## Classes

```pascal
abstract class Shape
    var id: integer
    abstract function area(): real
    procedure describe()
    begin
        print(id, area())
    end describe
end Shape

interface Named
    function label(): integer
end Named

class Square extends Shape implements Named
    var side: real
    function area(): real
    begin
        area := side * side
    end area
    function label(): integer
    begin
        label := 400 + id
    end label
end Square
```

A class extends one or more classes (`extends A, B`); `implements` is the same word,
for interfaces. A method written again replaces the inherited one, and the object
chooses: through a pointer (`Shape*`) or a parameter by reference (`inout s: Shape`),
`s.area()` is that of what `s` really is. `super.describe()` and `Shape.describe()`
call the inherited one. There can be no variables of an abstract class or of an
interface, only pointers and parameters by reference. See [language.md](language.md).

## Components and tests

The simulation, with its time, the messages on their way and those that get lost, is
written out in the translation, with the same sequence of random numbers: a test gives
the same result translated as it does with `pseudoscope test`.

```sh
pseudoscope test --to=java examples/systems/election.pscope
```

A translated program runs its `main`; given the argument `test`, its tests.

## What is not translated

- What comes from the libraries of the other pseudocodes other than numbers and plain
  texts: files, drawings, conversions (see [dialects.md](dialects.md)). They run here.
- `expect a -> b: m` when `m` is a method and not a message.
- Bounds checks (`-b`) are for C; the other languages have their own.
- Functions that are not in the file: only C can leave them to the linker.

`make targets` translates every test program and every example to every language
whose tools are installed, runs them, and compares what they print.
