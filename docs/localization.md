# Languages

Pseudoscope is written in English and speaks English, Italian, Spanish and Portuguese:
the interface, the errors of a program, the messages of the commands and `--help`.
Italian is where it was born; Spanish and Portuguese are the languages of the
pseudocodes it reads, PSeInt, VisuAlg and Portugol.

```sh
pseudoscope --lang=es examples          # for once
export PSEUDOSCOPE_LANG=it              # for good
```

Without either, it follows the system (`LC_ALL`, `LC_MESSAGES`, `LANG`), and speaks
English where it knows nothing better. What it reads does not change: the keywords of
P-like are English, those of PSeInt Spanish and those of VisuAlg and Portugol
Portuguese, whatever the language of the messages. Neither does what it writes: the
translations, `tokens` and `ast`.

## How it works

Every text is written in English where it is used, inside `T("…")`. Each language is a
gettext catalog, `lang/CODE.po`, that pairs those texts with their translation.
`tools/po2c` turns the catalogs into one table that is compiled into the program, so
there are no files to install and nothing to find at run time.

| In the source | For |
|---|---|
| `T("text")` | a text |
| `TN("%d line", "%d lines", n)` | one and many |
| `TC("short", "play")` | the same English, said differently in two places |
| `N_("text")` | a text in a table, translated where it is shown with `tr()` |

The messages of the translator (`diag_error`) and of the machine (`fail`) are plain
English texts too: they are translated where they are shown.

## Adding a language

1. Copy `lang/pseudoscope.pot` to `lang/CODE.po` (`fr.po`, `de.po`…) and fill its
   header: `Language: fr` and `X-Language-Name: Français`.
2. Translate each `msgstr`. Any gettext editor will do, and so will a text editor.
3. `make`: the language is there, under its code and its name (`--lang=fr`,
   `--lang=Français`, `LANG=fr_FR.UTF-8`).
4. `make test`: see below.

A translation takes the same arguments as the text it translates, in the same order;
`%.0s` drops one it has no use for. Anything else stops the build, with the line of
the catalog, rather than crash a program later.

## Keeping them whole

`python3 tests/messages.py` compares the catalogs with the sources: it lists the texts a
language lacks and those it still has that the program no longer says. With `--update`
it rewrites the catalogs in the order of the sources, adding the new texts with an
empty translation, and remakes the template.

`python3 tests/fit.py bin/pseudoscope` draws every screen in every language at four
sizes of window, from 80x24, and refuses a text that is cut, that has lines with no
room, or that runs out of the window. So a translation that is too long for its place
is found before anybody sees it: shorten it, or give it more room in the code. The keys
that the bottom line leaves out where it is too short are counted, not refused: `?`
lists them all.

Both are part of `make test`.

## Writing a text that translates

- Whole sentences, not pieces glued together: the order of words differs.
- A count and its noun in one `TN`, never `"%d %s"` with a translated word.
- No width in `%-12s` for a translated word: put the next thing at its column, or
  measure with `text_width()`, which counts characters and not bytes.
- If a text may not fit, let it wrap (`print_wrapped`) or go below, like the heading of
  a card does; do not count on the English being short.
