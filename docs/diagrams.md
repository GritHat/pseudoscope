# Diagrams

Diagrams written as text are opened like programs, and walked through: a design can be
followed step by step before any of it exists.

| Extension | Language |
|---|---|
| `.puml` `.plantuml` `.iuml` `.pu` | [PlantUML](https://plantuml.com) |
| `.mmd` `.mermaid` | [Mermaid](https://mermaid.js.org) |
| `.dot` `.gv` | [Graphviz](https://graphviz.org) |
| `.md` `.markdown` | Markdown: its fenced blocks |
| `.tex` | LaTeX: its environments |

A file without an extension is a diagram if it begins like one.

| Diagram | PlantUML | Mermaid | In the interface |
|---|---|---|---|
| sequence | yes | `sequenceDiagram` | message by message; Tab takes another branch of `alt` |
| state | yes | `stateDiagram` | from state to state, choosing the arrow |
| activity, flowchart | yes | `flowchart`, `graph` | from box to box |
| use case, component | yes | | from box to box |
| class | yes | `classDiagram` | class by class, with its relations |
| entities and relationships | yes | `erDiagram` | entity by entity: how many of each for each |
| plan (Gantt) | `@startgantt` | `gantt` | day by day: what is done, what is going on, what waits for what |
| mind map | `@startmindmap` | `mindmap` | branch by branch |
| work breakdown | `@startwbs` | | branch by branch |
| timeline, journey | | `timeline`, `journey` | period by period |
| pie | | `pie` | part by part, with what they add up to |
| graph | | | Graphviz: from node to node |

→ goes on, ← comes back, Space plays by itself (taking the arrows least used, so that
everything gets seen), `0` restarts.

`pseudoscope check FILE` reports what could not be read, `pseudoscope ast FILE` prints
what was understood. Colours, positions and other aspects of the drawing are skipped.

## Documents

A Markdown or LaTeX file is opened like a folder: of the programs and diagrams it
holds, under the headings they are found at, and of its outline, which is a tree.

| In | What is read |
|---|---|
| Markdown | blocks fenced with ` ``` ` or `~~~` and named `mermaid`, `plantuml`, `dot`, `pseudocode` (or `pscope`, `plike`), `pseint`, `visualg`, `portugol`, `cambridge`; headings |
| LaTeX | `algorithmic` (algpseudocode, algorithmic); `lstlisting` and `minted` in the languages above; `plantuml`, `mermaid`; `ganttchart` (pgfgantt); `\section` and its kin |

```sh
pseudoscope ast notes.md              # its outline, and what it holds
pseudoscope check notes.md            # everything it holds
pseudoscope exec notes.md#2           # the second thing it holds
pseudoscope ui notes.md#2             # the same, in the interface
pseudoscope run --to=python paper.tex # the only program of a paper
```

Errors are given at the lines of the document. An algorithm typeset with
`\Function`, `\State`, `\If`, `\For`, `\While`, `\Return` and mathematics
(`\gets`, `\leq`, `\bmod`...) is run like any other; types are worked out from how
things are used. `algorithm2e` is not read.

Diagrams can also come from a program: see *Watching* in [systems.md](systems.md).
