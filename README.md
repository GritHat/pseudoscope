# Pseudoscope

*[Italiano](README.it.md)*

Pseudocode and designs, brought to life in the terminal. Write an algorithm, a set of
classes, a distributed system or a UML diagram; watch it run step by step, forwards and
backwards, from the outside or in every detail; test it; translate it to C.

[![Demo](docs/demo/poster.png)](docs/demo/pseudoscope.mp4)

It started as `plike-translator`, a first-year project to run the pseudocode used in our
lectures. That pseudocode never had a fixed syntax, so all the spellings are accepted:
`A[i,j]`, `A[i][j]` or `A(i,j)`, `and`, `.AND.` or `&&`, `end f` or `endfunction`, and so
on. It now also reads PSeInt, VisuAlg, Portugol, Cambridge pseudocode, PlantUML and
Mermaid.

## Build

Needs a C11 compiler, make and a POSIX system. The tests also need Python 3.

```sh
git clone https://github.com/GritHat/plike-translator
cd plike-translator
make                 # bin/pseudoscope
make install         # optional, PREFIX=/usr/local
```

## Start

```sh
bin/pseudoscope                           # the current folder
bin/pseudoscope ui examples/sorting.plike
tools/pseudoscope-window examples         # in a new window, whatever the terminal
```

Enter goes deeper, Esc comes back up, `r` runs, `t` tests, `?` lists the keys. It is in
Italian; `--lang=en` for English. It works in kitty, GNOME Terminal, Ptyxis, Konsole,
xterm and the like, in tmux and screen, on the Linux console, and without colours or
Unicode: see [docs/interface.md](docs/interface.md).

## One program, five levels

The level decides what you see and how long a step is.

| Level | You see | A step is |
|---|---|---|
| System | sequence, state, timing and other diagrams, live | a message |
| Program | the calls, and who waits for whom | a call or a return |
| Function | the code, or its activity diagram, and its data: arrays, lists, trees | a statement |
| Statement | an expression turning into its value | one operation |
| Memory | the C code, addresses, bytes | a read or a write |

Any step can be undone. Any subprogram can be tried by itself, with values you type or
have generated. Every access is checked, and errors say what happened.

## From the first program to distributed systems

| | Try | Look at |
|---|---|---|
| Sorting | `examples/sorting.plike` | Function level, `v` for bars, ← to go back |
| Recursion | `examples/fib.plike` | Program level |
| Lists and trees | `examples/list.plike`, `tree.plike` | pointers as arrows |
| Graphs | `examples/advanced/dijkstra.plike` | the tests (`t`), which also check a property |
| Classes | `examples/advanced/stack_class.plike` | Objects |
| A design without code | `examples/design/shop.plike` | Sequence |
| A protocol | `examples/systems/stop_and_wait.plike` | what happens when packets get lost |
| End to end | `examples/systems/login.plike` | a test, while it runs |
| Consensus | `examples/systems/election.plike` | Timing: who leads, and when |
| Find the mistake | `examples/exercises/two_leaders.plike` | which rule is missing? The tests show it |
| UML | `examples/uml` | diagrams, walked through |

Components exchange messages over links that delay and lose them; tests say what must
happen from one end to the other:

```pascal
test "the right password logs in"
begin
    step "the user types the password"
    send Click(2, 2222) to client
    run
    expect client -> server: Login
    expect server -> client: Welcome
    check client.state = LoggedIn
end test
```

## Command line

```sh
pseudoscope exec program.plike            # execute, without a C compiler
pseudoscope test program.plike            # run its tests
pseudoscope program.plike -o program.c    # translate to C
pseudoscope run program.plike             # compile and execute
pseudoscope build program.plike -o program
pseudoscope check program.psc             # only report errors
pseudoscope ast diagram.puml              # what was understood (--dot for Graphviz)
```

`exec`, `run` and `build` start from a procedure called `main`, without parameters. `-`
reads the standard input. `pseudoscope --help` lists the options; these choose the
spelling of P-like:

| Option | Values | Default |
|---|---|---|
| `-a`, `--assignment` | `colon-equals`, `equals` | `colon-equals` |
| `-i`, `--indexing` | `one`, `zero` | `one` |
| `-p`, `--params` | `decl`, `body`, `mixed` | `mixed` |
| `--operators` | `standard`, `dotted`, `mixed` | `mixed` |
| `-m`, `--mixed-arrays` | `true`, `false` | `true` |

`-b` adds bounds checks to the generated program. The old name, `plike`, still works.

## Limits

Classes have no inheritance. Components and tests are run, not translated to C. Of the
diagrams, the kinds listed in [docs/diagrams.md](docs/diagrams.md) are read.

## Documentation

- [The interface](docs/interface.md)
- [The language](docs/language.md)
- [Systems: classes, components, tests](docs/systems.md)
- [Other pseudocodes](docs/dialects.md)
- [Diagrams](docs/diagrams.md)
- [How it works](docs/design.md)

## Tests

```sh
make test       # unit tests, end-to-end tests, screens, syntax matrix
make sanitize   # the same, with the program under ASan and UBSan
make fuzz       # mutated programs and diagrams, random keys
```

## License

MIT
