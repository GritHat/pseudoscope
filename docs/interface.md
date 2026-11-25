# The interface

`pseudoscope` opens the current folder, `pseudoscope ui PATH` a folder, a program or a
diagram. `pseudoscope-window` does it in a new window, in the terminal it is run from or
the first one installed (`PSEUDOSCOPE_TERMINAL` chooses).

## Terminals

At least 80x24 characters. The rest adapts:

| Terminal | Colours | Characters |
|---|---|---|
| kitty, GNOME Terminal, Ptyxis, Konsole, Alacritty, foot, WezTerm, xterm, tmux, screen... | 256, or 16 | Unicode |
| Linux console | 16 | those of code page 437 |
| a locale that is not UTF-8 | as above | ASCII |
| vt100, `NO_COLOR` | none: bold and reverse | as above |

`PSEUDOSCOPE_COLORS` (`256`, `16`, `0`) and `PSEUDOSCOPE_GLYPHS` (`unicode`, `console`,
`ascii`) decide instead.

## Finding the way

Enter goes deeper, Esc comes back up, everywhere. The position is always in the first
line, with the levels on the right.

    files > map > code > C                         exploring (blue)
    system > program > function > statement > memory        running (yellow)

`r` runs: from `main` the whole program; from another subprogram, only that one, after
asking for its values. Tab changes how arrays are filled: random, increasing, decreasing,
all equal. `t` runs the tests of the program.

Systems and the diagrams of the System level are in [systems.md](systems.md), the
diagrams that were only drawn in [diagrams.md](diagrams.md).

## Running

| Key | |
|---|---|
| → ← | one step forward, one back |
| Space | plays, pauses |
| ↑ ↓ | faster, slower |
| n | over a call |
| u | out of the subprogram |
| g | to the end, or to the next breakpoint |
| 0 | from the beginning |
| b | breakpoint on the current line |
| 1 2 3 4 5, Enter, Esc | level |
| Tab | another variable; at the System level, another view |
| f | the function as code or as an activity diagram |
| m | another component (States) |
| PgUp PgDn | scrolls the sequence |
| v | arrays as cells or as bars |
| o | what was printed |
| c | everything that happened |
| x | back to exploring |

## Reading the screen

| Colour | Meaning |
|---|---|
| blue | being read; what goes in |
| green | just written; results, what comes out |
| yellow | where we are |
| violet | procedures, functions, calls |
| sky | types, records |
| orange | pointers |
| red | errors, false |
| · | no value yet |

Each variable used as an index has a colour of its own, the same in the code and in the
marker ▼ over the array.

Pointers to a record that points to its own kind are drawn as a chain, or as a tree if it
has two such fields.

## For the tests

`pseudoscope ui PATH --keys="<down><enter>r" --size=100x30` presses the keys without a terminal
and prints the screen.
