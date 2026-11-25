# Diagrams

Diagrams written as text are opened like programs, and walked through: a design can be
followed step by step before any of it exists.

| Extension | Language |
|---|---|
| `.puml` `.plantuml` `.iuml` | [PlantUML](https://plantuml.com) |
| `.mmd` `.mermaid` | [Mermaid](https://mermaid.js.org) |

| Diagram | PlantUML | Mermaid | In the interface |
|---|---|---|---|
| sequence | yes | `sequenceDiagram` | message by message; Tab takes another branch of `alt` |
| state | yes | `stateDiagram` | from state to state, choosing the arrow |
| activity, flowchart | yes | `flowchart`, `graph` | from box to box |
| use case, component | yes | | from box to box |
| class | yes | `classDiagram` | class by class, with its relations |

→ goes on, ← comes back, Space plays by itself (taking the arrows least used, so that
everything gets seen), `0` restarts.

`pseudoscope check FILE` reports what could not be read, `pseudoscope ast FILE` prints
what was understood. Colours, positions and other aspects of the drawing are skipped.
Other kinds of diagram (Gantt, mind maps, entity-relationship and so on) are not read.

Diagrams can also come from a program: see *Watching* in [systems.md](systems.md).
