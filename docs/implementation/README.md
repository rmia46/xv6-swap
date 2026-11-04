# Implementation Notes (`docs/implementation/`)

Detailed design notes, internal algorithms, lock protocols, and implementation walkthroughs.

## Sections
- `swap-layer.md`: How the disk driver interacts with swap buffers.
- `page-table-encoding.md`: Format of PTEs when pages are marked present vs swapped out.
- `policy-api.md`: Function signatures for replacement policy registration and victim selection.
