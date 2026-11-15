# Documentation

| Folder | Contents | Lifecycle |
|---|---|---|
| [`features/`](features/README.md) | One page per major feature: what it does, spec, design, diagrams. | Living: updated as the feature evolves; each change is logged in its *History* table. |
| [`adr/`](adr/README.md) | One architectural decision per file: context, alternatives, choice. | Editable while *Proposed*; immutable once *Accepted* (implemented): a changed decision gets a new ADR that supersedes the old one. |

Feature pages describe **how things are**; ADRs explain **why**.

Diagrams use [Mermaid](https://mermaid.js.org/): plain text, diffable, rendered by GitHub.
