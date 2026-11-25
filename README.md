# plike

Translates P-like, a Pascal-like pseudocode, to C.

It started as a first-year project, to run the pseudocode used in our
lectures. That pseudocode never had a fixed syntax, so the point of the translator
is to accept all of the spellings: types in the parameter list or in the body,
`A[i,j]`, `A[i][j]` or `A(i,j)`, `and`, `.AND.` or `&&`, `end f` or `endfunction`,
`in/out` or `inout`, and so on.

```pascal
procedure BubbleSort (in: n; in/out: A)
    var i, j, n : integer
    var A : array [1..n] of real
    var temp : real
    var ordinato : logical
    begin
        ordinato := FALSE
        i := 1
        while (i<n) AND (NOT ordinato) do
            ordinato := TRUE
            for j := 1 to n-i do
                if A(j) > A(j+1) then
                    temp := A(j)
                    A(j) := A(j+1)
                    A(j+1) := temp
                    ordinato := FALSE
                endif
            endfor
            i := i+1
        endwhile
    end
end BubbleSort
```

becomes

```c
void BubbleSort(int n, float A[n]) {
    int i;
    int j;
    float temp;
    bool ordinato;
    ordinato = false;
    i = 1;
    while (i < n && !ordinato) {
        ordinato = true;
        for (j = 1; j <= n - i; j++) {
            if (A[j - 1] > A[j]) {
                temp = A[j - 1];
                A[j - 1] = A[j];
                A[j] = temp;
                ordinato = false;
            }
        }
        i = i + 1;
    }
}
```

## Build

Needs a C11 compiler, make and a POSIX system. The tests also need Python 3.

```sh
git clone https://github.com/GritHat/plike-translator
cd plike-translator
make                 # bin/plike
make install         # optional, PREFIX=/usr/local
```

## The interface

```sh
plike                                     # or: plike ui folder-or-file
```

Made for those who are learning: it shows a program at the level of detail you choose,
and lets you go deeper, or back up, at any moment.

- **Explore**: files, then the map of the program (subprograms as boxes with what goes in
  and out, records as diagrams), the code, and its translation to C.
- **Run**: step by step, forwards *and backwards*, or by itself. The level decides both
  what you see and how long a step is:

| Level | You see | A step is |
|---|---|---|
| Program | the calls, and who waits for whom | a call or a return |
| Function | the code and its data: cells, grids, linked lists, trees | a statement |
| Statement | an expression turning into its value | one operation |
| Memory | the C code, addresses, bytes | a read or a write |

Any subprogram can be tried by itself, with values you type or have generated: there is
no need for a main. Every access is checked, and errors say what happened. `?` lists the
keys. The interface is in Italian; `--lang=en` for English. See
[docs/interface.md](docs/interface.md).

## Use

```sh
plike exec program.plike                  # execute, without a C compiler
plike program.plike -o program.c          # translate
plike run program.plike                   # compile and execute
plike build program.plike -o program      # compile
plike check program.plike                 # only report errors
plike ast program.plike                   # syntax tree (--dot for Graphviz)
plike tokens program.plike
```

`exec`, `run` and `build` start from a procedure called `main`, without parameters. `-` reads
the program from the standard input. `plike --help` lists all the options; these
choose the dialect:

| Option | Values | Default |
|---|---|---|
| `-a`, `--assignment` | `colon-equals`, `equals` | `colon-equals` |
| `-i`, `--indexing` | `one`, `zero` | `one` |
| `-p`, `--params` | `decl`, `body`, `mixed` | `mixed` |
| `--operators` | `standard`, `dotted`, `mixed` | `mixed` |
| `-m`, `--mixed-arrays` | `true`, `false` | `true` |

`-b` adds bounds checks to the generated program.

Errors show where they are:

```
sum.plike:6:15: error: the array has 1 dimension but 2 indices were given
    6 |         s := A(i,j)
      |               ^
```

## Documentation

- [The language](docs/language.md)
- [How the translator works](docs/design.md)
- [Examples](examples)

## Tests

```sh
make test       # unit tests, end-to-end tests, screens, syntax matrix
make sanitize   # the same, with the translator under ASan and UBSan
make fuzz       # mutated programs, random keys
```

## License

MIT
