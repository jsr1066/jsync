# Scan
`Scan(Database&, Peer&)::plan()` compares, for every directory in the sync table, the local
files (L), the server files (S, via REQ-SCAN) and the stored state of the last sync (D, the
files table). It only plans: it returns a list of actions (read from server, write to
server, delete local, delete server, conflict) and changes nothing.

A file has *changed* on a side if its size or mtime differs from D.

| L | S | D | result |
|---|---|---|---|
| ✓ | ✓ | ✓ | neither changed: none; only L changed: write; only S changed: read; both changed: conflict. If L and S are already equal: none |
| ✓ | ✓ | – | both new: none if size+mtime equal, else conflict |
| ✓ | – | – | write to server |
| – | ✓ | – | read from server |
| ✓ | – | ✓ | deleted on server: L unchanged → delete local; L changed → conflict |
| – | ✓ | ✓ | deleted locally: S unchanged → delete server; S changed → conflict |
| – | – | ✓ | deleted on both sides: none (the files row is cleaned up by the executor) |

Excluded files are absent from L and S, and their files rows are removed by
`Database::set_excludes`, so they are not mistaken for deletions.

Single-file sync items are not handled yet.

## Known issue
`FileOps::rename` does not preserve mtime, but the files table stores one mtime per file.
After a transfer the destination's mtime differs from the source's, so the size+mtime test
can misreport changes. To be fixed with the executor (preserve mtime on rename, or store both).
