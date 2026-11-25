# The interface

`plike` opens the current folder, `plike ui PATH` a folder or a program. It needs a
terminal of at least 80x24 characters, with 256 colours (on the Linux console it makes do
with 16; `PLIKE_BASIC=1` forces that).

## Finding the way

Enter goes deeper, Esc comes back up, everywhere. The position is always in the first
line, with the levels on the right.

    files > map > code > C                         exploring (blue)
    program > function > statement > memory        running (yellow)

`r` runs: from `main` the whole program; from another subprogram, only that one, after
asking for its values. Tab changes how arrays are filled: random, increasing, decreasing,
all equal.

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
| 1 2 3 4, Enter, Esc | level |
| Tab | scrolls to another variable |
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

`plike ui PATH --keys="<down><enter>r" --size=100x30` presses the keys without a terminal
and prints the screen.
